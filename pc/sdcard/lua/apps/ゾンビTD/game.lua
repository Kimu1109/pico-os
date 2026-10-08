-- ゾンビTD: 斜め上から見た箱庭(pico.iso)のタワーディフェンス。仕様は ZOMBIE_TD.md。
-- 今は「作る順番」の2と3: 種からのマップの生成(ベースを平らに・出現位置・道があるかの確認)・カメラ・
-- ゾンビ(出現・流れの場をたどる・押し合い・ベースへの攻撃)。兵士・建物・ウェーブ・お金はまだ無い。
-- 下のボタンでゾンビを呼び、実機の速さ(fps と Lua の処理時間)を測るための画面でもある。
--   上の行: ベースの耐久・出ているゾンビ(と順番待ち)・fps・1フレームの処理時間(ms)
--   [戻る] [+5] [+40] [x1/x3](速さ) [中央](ベースへ) [作直](別の種でマップを作り直す)
--   地図のドラッグ=カメラ。コントローラーの十字・キーボードの矢印でも動かせる。A=+5、B=+40、HOME=戻る。
--   5秒ごとにシリアルへ "[TD] fps=.. lua=..ms render=..ms alive=.." を出す。
-- マップ・流れの場・押し合い・弾・描画は C++ のエンジン(src/iso/)。ここは流れと操作だけ。
local iso = pico.iso
local PAL = require("palette")
local zombies = require("zombies")

local W_CHUNKS = 7                 -- 7x7 チャンク = 56x56 マス(全部を読み込んだままにできる上限)
local BASE_HP = 2000              -- 数値は仮(ウェーブとお金を入れる段で決め直す)
local MAX_TRIES = 8                -- 出現位置からベースへ道が無い種は飛ばす(この回数まで)
local LOG_MS = 5000
-- ゾンビの道の規則(流れの場)。水には入らない。段差は1段まで登り、2段まで降りる。高低差は少し嫌う
local FLOW_RULES = { max_up = 1, max_down = 2, height = 2, up_cost = 0.5, diagonal = true }
local STAND_RULES = { height = 2 }

for i, c in ipairs(PAL.colors) do pico.set_palette(i, c[1], c[2], c[3]) end
iso.sky(PAL.sky)
local DIR = pico.app_dir()
local faces = pico.image_load(pico.path_join(DIR, "faces.pimg"))
local units = pico.image_load(pico.path_join(DIR, "units.pimg"))
if faces then iso.set_image(faces) else pico.show_error("faces.pimg を読めません") end
if not units then pico.show_error("units.pimg を読めません") end
local MAP_DIR = pico.path_join(DIR, "map")   -- 何も書かない(地形は種から作るので保存しない)

local CX, CY, CW, CH = pico.content_rect()
local HUD_H, PANEL_H = 20, 34
local VY = CY + HUD_H
local VH = CH - HUD_H - PANEL_H
local PY = VY + VH
iso.view(CX, VY, CW, VH)

local hud = pico.create("Canvas")
local view = pico.create("Canvas")
local panel = pico.create("Canvas")
pico.set(hud, "x", CX); pico.set(hud, "y", CY); pico.set(hud, "w", CW); pico.set(hud, "h", HUD_H)
pico.set(view, "x", CX); pico.set(view, "y", VY); pico.set(view, "w", CW); pico.set(view, "h", VH)
pico.set(panel, "x", CX); pico.set(panel, "y", PY); pico.set(panel, "w", CW); pico.set(panel, "h", PANEL_H)

local mode = "load"        -- load(マップを作っている)/ play / over(ベースが壊れた)
local load_msg = ""
local stage = nil          -- load の中: pump / flow
local seed, tries = 0, 0
local W = 56
local base = nil           -- {id=, x=, y=, z=, hp=, hit=}
local arena = nil
local speed = 1
local OX, OY = 0, 0

-- ---------------------------------------------------------------- カメラ

local function set_origin(x, y)
    -- 画面の真ん中に見える地面(高さ6あたり)の点がマップの中に収まるように抑える
    local u = (CX + CW / 2 - x - 16) / 16                 -- x - z
    local s = (y + 32 - 16 * 6 - (VY + VH / 2)) / 8       -- x + z
    local cu = math.max(-W, math.min(W, u))
    local cs = math.max(0, math.min(2 * W, s))
    x = math.floor(x - (cu - u) * 16 + 0.5)
    y = math.floor(y + (cs - s) * 8 + 0.5)
    if x == OX and y == OY then return end
    OX, OY = x, y
    iso.origin(x, y)
    pico.invalidate(view)
end

local function center_on(x, y, z)
    set_origin(CX + CW // 2 - 16 - 16 * (x - z), VY + VH // 2 - 16 + 8 * (x + z) + 16 * y)
end

local function scroll(dx, dy) set_origin(OX + dx, OY + dy) end

-- ---------------------------------------------------------------- 計測

local perf = { frames = 0, lua_us = 0, render_us = 0, t0 = 0, fps = 0, lua_ms = 0, render_ms = 0 }
local function us_since(t) return (pico.micros() - t) % 4294967296 end

local function perf_tick()
    perf.frames = perf.frames + 1
    local now = pico.millis()
    if perf.t0 == 0 then perf.t0 = now end
    local span = now - perf.t0
    if span >= LOG_MS then
        perf.fps = perf.frames * 1000 / span
        perf.lua_ms = perf.lua_us / perf.frames / 1000
        perf.render_ms = perf.render_us / perf.frames / 1000
        pico.log(string.format("[TD] fps=%.1f lua=%.2fms render=%.2fms alive=%d wait=%d shots=%d",
            perf.fps, perf.lua_ms, perf.render_ms, zombies.alive(), zombies.waiting(), iso.shot_count()))
        perf.frames, perf.lua_us, perf.render_us, perf.t0 = 0, 0, 0, now
        pico.invalidate(hud)
    end
end

-- ---------------------------------------------------------------- マップを作る

local function goals_around(bx, bz)
    -- ベースのまわり(中心から2マス離れた輪の16柱)がゾンビの目的地
    local g = {}
    for dx = -2, 2 do
        for dz = -2, 2 do
            if math.max(math.abs(dx), math.abs(dz)) == 2 then g[#g + 1] = { bx + dx, bz + dz } end
        end
    end
    return g
end

local function mem_log(what)
    collectgarbage("collect")
    local m = pico.memory_info()
    pico.log(string.format("[TD] %s: ヒープの空き %d + 未使用 %d バイト、Lua %d/%d バイト",
        what, m.heap_free, m.heap_headroom or 0, m.lua_used, m.lua_budget))
end

local function gen_fail(msg)
    mem_log("失敗")
    pico.show_error(msg)
    load_msg = "作れませんでした([作直]でやり直し)"
    stage = nil
    pico.invalidate(view)
end

local function gen_start()
    zombies.clear()
    iso.shot_clear()
    iso.flow_clear()
    iso.close()
    base = nil
    local bx, by, bz = iso.create(MAP_DIR, 4, seed, W_CHUNKS)
    if not bx then
        mode = "load"
        return gen_fail("マップを作れません: " .. tostring(by))
    end
    W = iso.size()
    iso.keep_all(true)
    arena = iso.arena()
    center_on(bx, by, bz)
    load_msg = "マップを作っています..."
    stage = "pump"
    mode = "load"
    pico.invalidate(view)
    pico.invalidate(hud)
end

local function new_map(s)
    seed = s or (pico.millis() * 7919 + math.random(0, 0xffff)) % 0x7fffffff
    tries = 0
    gen_start()
end

local function update_base_bar()
    iso.entity_set(base.id, { bar = math.max(0, base.hp) * 100 // BASE_HP })
end

local function start_play()
    local a = arena.base
    base = { x = a.x, y = a.y, z = a.z, hp = BASE_HP }
    base.id = iso.entity_add(units, a.x + 0.5, a.y, a.z + 0.5, {
        sx = 80, sy = 0, w = 44, h = 40, r = 1.4, height = 2.5, crowd = "fixed", tag = 2,
        bar = 100, bar_color = 2,
    })
    function base.hit(dmg)
        if mode ~= "play" then return end
        base.hp = base.hp - dmg
        update_base_bar()
        pico.invalidate(hud)
        if base.hp <= 0 then
            mode = "over"
            pico.invalidate(view)
            pico.log(string.format("[TD] ベースが壊れました(種 %d、出したゾンビ %d)", seed, zombies.stats.spawned))
        end
    end
    zombies.init(units, arena.spawns, base, STAND_RULES)
    mode = "play"
    pico.invalidate(view)
    pico.invalidate(hud)
    collectgarbage("collect")
    local m = pico.memory_info()
    local st = iso.stats()
    pico.log(string.format("[TD] 種 %d: チャンク %d 個 (置き場 %d バイト)、Lua %d/%d バイト、ヒープの空き %d + 未使用 %d バイト",
        seed, st.chunks, st.bytes, m.lua_used, m.lua_budget, m.heap_free, m.heap_headroom or 0))
end

local function gen_step()
    if stage == "pump" then
        iso.pump(64, 30)
        if iso.pending() == 0 then
            load_msg = "道を調べています..."
            pico.invalidate(view)
            mem_log("チャンクを読み込んだ")
            local ok, err = iso.flow_build(goals_around(arena.base.x, arena.base.z), FLOW_RULES)
            if not ok then return gen_fail("道を作れません: " .. tostring(err)) end
            stage = "flow"
        end
    elseif stage == "flow" then
        local done = iso.flow_step(4000)
        local info = iso.flow_info()
        if info.failed then return gen_fail("道を作れません: メモリが足りません") end
        if done then
            -- 出現位置のどれかからベースへ届かない種は使わない
            local ok = true
            for _, s in ipairs(arena.spawns) do
                if not iso.flow_get(s.x, s.z) then ok = false end
            end
            if ok or tries >= MAX_TRIES then
                if not ok then pico.log("[TD] 道のある種が見つかりませんでした。このまま使います") end
                start_play()
            else
                pico.log(string.format("[TD] 種 %d は出現位置から道がありません。次の種へ", seed))
                tries = tries + 1
                seed = (seed + 1) % 0x7fffffff
                gen_start()
            end
        end
    end
end

-- ---------------------------------------------------------------- 描画

local function box(x, y, w, h, label, on)
    pico.fill_rect(x, y, w, h, on and PAL.mid or PAL.light)
    pico.draw_rect(x, y, w, h, 0)
    if label then pico.draw_text(x + w // 2, y + (h - 16) // 2, label, 0, 0, "center") end
end

pico.on(hud, "render", function()
    pico.fill_rect(CX, CY, CW, HUD_H, PAL.dark)
    if mode == "load" or not base then
        pico.draw_text(CX + 4, CY + 2, "種 " .. seed, 15, 0)
        return
    end
    pico.draw_text(CX + 4, CY + 2, string.format("基地%d", math.max(0, base.hp)), base.hp > BASE_HP // 4 and 15 or 12, 0)
    pico.draw_text(CX + 66, CY + 2, string.format("Z%d+%d", zombies.alive(), zombies.waiting()), 15, 0)
    pico.draw_text(CX + CW - 2, CY + 2, string.format("%.0ffps %.1fms", perf.fps, perf.lua_ms + perf.render_ms), 14, 0, "right")
end)

pico.on(view, "render", function()
    if mode == "load" then
        local x, y, w, h = pico.get_draw_area()
        pico.fill_rect(x, y, w, h, PAL.sky)
        box(CX + 30, VY + VH // 2 - 20, CW - 60, 40, load_msg)
        return
    end
    local t = pico.micros()
    iso.render()
    perf.render_us = perf.render_us + us_since(t)
    if mode == "over" then
        box(CX + 30, VY + VH // 2 - 30, CW - 60, 60)
        pico.draw_text(CX + CW // 2, VY + VH // 2 - 24, "ベースが壊れました", 12, 0, "center")
        pico.draw_text(CX + CW // 2, VY + VH // 2 + 2, "タップでもう一度", 0, 0, "center")
    end
end)

local BUTTONS = {
    { id = "back", label = "戻る" }, { id = "z5", label = "+5" }, { id = "z40", label = "+40" },
    { id = "speed" }, { id = "center", label = "中央" }, { id = "remake", label = "作直" },
}
local BW = CW // #BUTTONS
for i, b in ipairs(BUTTONS) do b.x = (i - 1) * BW; b.w = (i == #BUTTONS) and (CW - b.x) or BW end
local held = nil

pico.on(panel, "render", function()
    pico.fill_rect(CX, PY, CW, PANEL_H, PAL.dark)
    for _, b in ipairs(BUTTONS) do
        box(CX + b.x + 1, PY + 2, b.w - 2, PANEL_H - 4, b.label or ("x" .. speed), held == b)
    end
end)

-- ---------------------------------------------------------------- 操作

-- ゾンビを n 匹呼ぶ(ノーマル6:遠距離2.5:重量級1.5 の割合)
local function call(n)
    if mode ~= "play" then return end
    for _ = 1, n do
        local r = math.random()
        zombies.queue(r < 0.6 and "normal" or (r < 0.85 and "ranged" or "heavy"))
    end
    pico.invalidate(hud)
end

local function restart()
    -- 同じマップでやり直す(ゾンビと弾を消してベースを直す)
    zombies.clear()
    iso.shot_clear()
    base.hp = BASE_HP
    update_base_bar()
    mode = "play"
    pico.invalidate(view)
    pico.invalidate(hud)
end

local function press(b)
    if b.id == "back" then pico.pop()
    elseif b.id == "z5" then call(5)
    elseif b.id == "z40" then call(40)
    elseif b.id == "speed" then speed = speed == 1 and 3 or 1; pico.invalidate(panel)
    elseif b.id == "center" then if base then center_on(base.x, base.y, base.z) end
    elseif b.id == "remake" then if mode ~= "load" or stage == nil then new_map() end end
end

local function button_at(lx)
    for _, b in ipairs(BUTTONS) do if lx >= b.x and lx < b.x + b.w then return b end end
end
pico.on(panel, "press_start", function(_, _, _, lx)
    held = button_at(lx)
    pico.invalidate(panel)
end)
pico.on(panel, "press_end", function(_, _, _, lx)
    local b = held
    held = nil
    pico.invalidate(panel)
    if b and b == button_at(lx) then press(b) end
end)
pico.on(panel, "press_out", function() held = nil; pico.invalidate(panel) end)

local touch = nil
pico.on(view, "press_start", function(_, x, y) touch = { x = x, y = y, drag = false } end)
pico.on(view, "press_move", function(_, x, y, _, _, dx, dy)
    if not touch or mode == "load" then return end
    if not touch.drag and math.abs(x - touch.x) + math.abs(y - touch.y) > 8 then
        touch.drag = true
        dx, dy = x - touch.x, y - touch.y
    end
    if touch.drag then scroll(dx, dy) end
end)
pico.on(view, "press_end", function()
    local t = touch
    touch = nil
    if t and not t.drag and mode == "over" and base then restart() end
end)
pico.on(view, "press_out", function() touch = nil end)

pico.on_back(function() pico.pop(); return true end)

pico.on_key(function(key)
    if key == "left" then scroll(32, 0)
    elseif key == "right" then scroll(-32, 0)
    elseif key == "up" then scroll(0, 16)
    elseif key == "down" then scroll(0, -16)
    elseif key == "c" and base then center_on(base.x, base.y, base.z)
    elseif key == "z" then call(5)
    elseif key == "x" then call(40)
    else return false end
    return true
end)

-- ---------------------------------------------------------------- 毎フレーム

function loop(dt)
    perf_tick()
    if mode == "load" then gen_step(); return end
    local t0 = pico.micros()
    if mode == "play" then
        local d = math.min(dt, 50) / 1000 * speed
        zombies.update(d)
        for _, h in ipairs(iso.shots_step(d)) do
            if h.target == base.id and not h.lost then base.hit(h.tag) end
        end
        if (perf.frames % 15) == 0 then pico.invalidate(hud) end
    end
    -- コントローラー: 十字=カメラ、A=+5、B=+40
    local h = (pico.pad_down("left") and 1 or 0) - (pico.pad_down("right") and 1 or 0)
    local v = (pico.pad_down("up") and 1 or 0) - (pico.pad_down("down") and 1 or 0)
    if h ~= 0 or v ~= 0 then scroll(h * 6, v * 4) end
    if pico.pad_pressed("a") then call(5) end
    if pico.pad_pressed("b") then call(40) end
    perf.lua_us = perf.lua_us + us_since(t0)
end

math.randomseed(pico.millis())
new_map()

if TEST then TEST.env = { mode = function() return mode end, base = function() return base end,
    call = call, restart = restart, press = press, buttons = BUTTONS, zombies = zombies,
    seed = function() return seed end, new_map = new_map, scroll = scroll, center_on = center_on,
    origin = function() return OX, OY end, perf = perf, speed = function() return speed end } end
