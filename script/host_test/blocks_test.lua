-- Luaアプリ「ブロック」(pc/sdcard/lua/apps/ブロック/。Blocks-TI-84 の移植)の画面の流れと操作を確かめる:
-- ワールドを作る/開く/前の版から移す → チャンクの読み込み → 遊ぶ → 保存して戻る、置く/壊すの規則、
-- カーソルの移動と視点、タップ位置からのカーソル、ブロックを選ぶ画面、松明と昼/夜、村人と羊(人や物)。
-- ワールドと描画は C++ のエンジン pico.iso(src/iso/Iso_World)が受け持つので、ここでは pico.iso を
-- 呼ばれ方を記録するだけの偽物(辞書で持つ平らなワールド)に差し替える。エンジンの中身は iso_world_test、
-- Lua からの呼び方と権限は lua_ext_test。lua_script_test から run.sh が呼ぶ。
local APP = ROOT_DIR .. "/pc/sdcard/lua/apps/ブロック/"
local fails = 0
local function check(c, msg)
    print((c and "[ OK ] " or "[FAIL] ") .. msg)
    if not c then fails = fails + 1 end
end

-- ---- 偽物の pico.iso: 平らな(y=0 が草の)ワールドを辞書で持つ ----
local calls = {}
local function rec(name, ...) calls[#calls + 1] = { name, ... } end
local function count(name)
    local n = 0
    for _, c in ipairs(calls) do if c[1] == name then n = n + 1 end end
    return n
end
local saves = {}           -- dir → { x, y, z, cur, blocks }
local world = nil          -- 開いているワールド
local origin = { 0, 0 }
local function key(x, y, z) return x .. "," .. y .. "," .. z end
local iso = {
    sky = function(c) rec("sky", c) end,
    sunlight = function(on) rec("sunlight", on) end,
    set_image = function(h) rec("set_image", h) end,
    view = function(...) rec("view", ...) end,
    create = function(d, kind, seed)
        rec("create", d, kind, seed)
        world = { dir = d, kind = kind, blocks = {}, pending = 3 }
        return 512, 1, 512
    end,
    open = function(d)
        rec("open", d)
        local s = saves[d]
        if not s then return nil, "形式が違います" end
        world = { dir = d, kind = 1, blocks = {}, pending = 3 }
        for k, v in pairs(s.blocks) do world.blocks[k] = v end
        return s.x, s.y, s.z, s.cur
    end,
    info = function(path)
        local d = path:match("^(.*)/world%.dat$")
        if d and saves[d] then return 1, 1024 end
        return nil
    end,
    migrate = function(old, d)
        rec("migrate", old, d)
        saves[d] = { x = 3, y = 4, z = 5, cur = 24, blocks = {} }
        return true
    end,
    save = function(x, y, z, cur)
        rec("save", x, y, z, cur)
        local b = {}
        for k, v in pairs(world.blocks) do b[k] = v end
        saves[world.dir] = { x = x, y = y, z = z, cur = cur, blocks = b }
        return true
    end,
    close = function() rec("close"); world = nil end,
    size = function() if world then return 1024, 16, world.kind end return nil end,
    get = function(x, y, z)
        if not world then return 0 end
        local v = world.blocks[key(x, y, z)]
        if v then return v end
        return y == 0 and 3 or 0
    end,
    set = function(x, y, z, b) world.blocks[key(x, y, z)] = b end,
    pump = function(n)
        rec("pump", n)
        local d = math.min(n, world.pending)
        world.pending = world.pending - d
        return d
    end,
    pending = function() return world and world.pending or 0 end,
    stats = function() return { chunks = 30, bytes = 61264, faces = 0, pending = 0 } end,
    origin = function(ox, oy)
        if ox then origin = { ox, oy }; rec("origin", ox, oy) end
        return origin[1], origin[2]
    end,
    cursor = function(...) rec("cursor", ...) end,
    render = function() rec("render") end,
    draw_icon = function(...) rec("draw_icon", ...) end,
    block_pos = function(x, y, z)
        return origin[1] + 16 * (x - z), origin[2] - 8 * (x + z) - 16 * y
    end,
    pick = function(px, py) return 20, 0, 20, "top" end,
    dirty_block = function(...) rec("dirty_block", ...) end,
    dirty_edit = function(...) rec("dirty_edit", ...) end,
}
-- 人や物
local ents, nent, tap_id = {}, 0, nil
function iso.entity_add(img, x, y, z, o)
    nent = nent + 1
    ents[nent] = { img = img, x = x, y = y, z = z, o = o or {} }
    return nent
end
function iso.entity_move(id, x, y, z) local e = assert(ents[id]); e.x, e.y, e.z = x, y, z end
function iso.entity_set(id, o) local e = assert(ents[id]); for k, v in pairs(o) do e.o[k] = v end end
function iso.entity_remove(id) local had = ents[id] ~= nil; ents[id] = nil; return had end
function iso.entity_at() return tap_id end
function iso.ground(x, z, y)
    y = y or 16
    for yy = math.min(15, math.floor(y + 0.001) - 1), 0, -1 do
        local b = iso.get(math.floor(x), yy, math.floor(z))
        if b ~= 0 and b ~= 25 then return yy + 1 end
    end
    return nil
end

-- ---- 偽物の pico ----
local files = {}
local handlers = {}
local afters = {}
local nid = 0
local renders = 0
pico = setmetatable({
    iso = iso,
    content_rect = function() return 0, 20, 240, 300 end,
    create = function() nid = nid + 1 return nid end,
    on = function(id, ev, fn) handlers[id .. ev] = fn end,
    after = function(_, fn) afters[#afters + 1] = fn return #afters end,
    millis = function() return 0 end,
    pad_down = function() return false end,
    pad_pressed = function() return false end,
    get_touch = function() return 0, 0, false end,
    image_load = function() return 7 end,
    app_dir = function() return "/app" end,
    path_join = function(...) return table.concat({ ... }, "/") end,
    memory_info = function() return { lua_used = collectgarbage("count") * 1024, lua_budget = 204800 } end,
    get_draw_area = function() return 0, 20, 240, 204 end,
    invalidate = function() renders = renders + 1 end,
    sd_exists = function(p) return files[p] ~= nil end,
    sd_remove = function(p) files[p] = nil return true end,
}, { __index = function() return function() end end })

local loaded = {}
function require(n)
    if not loaded[n] then loaded[n] = dofile(APP .. n .. ".lua") end
    return loaded[n]
end

TEST = {}
dofile(APP .. "main.lua")
local E = TEST.env
check(E.mode() == "title", "起動するとワールドを選ぶ画面")
check(count("set_image") == 1 and calls[1][2] == 7, "faces.pimg をエンジンへ渡す")
check(count("view") == 1, "表示範囲をエンジンへ渡す")

local function run_afters()
    while #afters > 0 do table.remove(afters, 1)() end
end
local function frames(n) for _ = 1, n do loop(16) end end

-- ---- 作る → 読み込む → 遊ぶ ----
E.slot(2)
E.start(1)
run_afters()
check(count("create") == 1, "新しいワールドを作る")
for _, c in ipairs(calls) do
    if c[1] == "create" then check(c[2] == "/app/worlds/B" and c[3] == 1, "スロットBのディレクトリに平らなワールド") end
end
check(E.mode() == "load", "作ったらチャンクを読み込む画面")
local cx, cy, cz = E.cursor()
check(cx == 512 and cy == 1 and cz == 512, "カーソルは始めの位置")
local ox, oy = E.origin()
local bx, by = iso.block_pos(cx, cy, cz)
check(bx == 0 + 120 - 16 and by == 20 + 102 - 16, "カーソルを表示の真ん中に置く")
frames(1)
check(E.mode() == "play", "読み込み終わると遊ぶ画面へ")
check(count("pump") >= 1, "毎フレーム読み込みを進める")

-- ---- 村人と羊 ----
local mobs = E.mobs
local function nents() local n = 0; for _ in pairs(ents) do n = n + 1 end return n end
check(mobs.count() == 5 and nents() == 5, "読み込み終わるとカーソルのまわりに村人3人と羊2匹")
local all_ground = true
for _, e in pairs(ents) do
    if e.y ~= 1 or math.abs(e.x - 512) > 7 or math.abs(e.z - 512) > 7 then all_ground = false end
end
check(all_ground, "地面(y=1)の上、カーソルの近くに立つ")
local start_pos = {}
for id, e in pairs(ents) do start_pos[id] = { e.x, e.z } end
frames(120)
local moved, still_ground = 0, true
for id, e in pairs(ents) do
    if e.x ~= start_pos[id][1] or e.z ~= start_pos[id][2] then moved = moved + 1 end
    if e.y ~= 1 then still_ground = false end
end
check(moved >= 3 and still_ground, "歩き回る(平らな所では地面の上のまま)")
-- 壁で囲うと出られない
local id1 = next(ents)
local e1 = ents[id1]
local bx0, bz0 = math.floor(e1.x), math.floor(e1.z)
for dx = -2, 2 do for dz = -2, 2 do
    if math.abs(dx) == 2 or math.abs(dz) == 2 then
        for y = 1, 3 do iso.set(bx0 + dx, y, bz0 + dz, 2) end
    end
end end
frames(300)
check(math.abs(ents[id1].x - (bx0 + 0.5)) < 2 and math.abs(ents[id1].z - (bz0 + 0.5)) < 2 and ents[id1].y == 1,
      "壁(3段)の中からは出られない")
-- 1段の段差は登る
for dx = -1, 1 do for dz = -1, 1 do iso.set(bx0 + dx, 1, bz0 + dz, 2) end end
frames(200)
check(ents[id1].y == 2, "1段の段差は登る")
-- タップすると跳ねて、地面へ戻る
tap_id = id1
E.pick_to(100, 100, false)
tap_id = nil
local cx2, cy2, cz2 = E.cursor()
check(cx2 == 512 and cy2 == 1 and cz2 == 512, "村人をタップしてもカーソルは動かない")
frames(5)
local up = ents[id1].y
frames(60)
check(up > 2 and ents[id1].y == 2, "タップすると跳ねて、地面へ戻る")
for dx = -2, 2 do for dz = -2, 2 do for y = 1, 3 do iso.set(bx0 + dx, y, bz0 + dz, 0) end end end
-- 歩くコマ
local sx_seen = {}
for _ = 1, 60 do frames(1); for _, e in pairs(ents) do if e.o.sx then sx_seen[e.o.sx] = true end end end
check(sx_seen[0] and sx_seen[12], "村人は歩くと2コマを切り替える")

-- ---- 置く/壊す ----
E.set_cursor(10, 1, 10)
E.set_cur(6)
E.act()
check(iso.get(10, 1, 10) == 6, "空気の所に置く")
check(count("dirty_edit") == 1, "置いたら影が変わる所を描き直す")
E.act()
check(iso.get(10, 1, 10) == 0, "ブロックの所で押すと壊す")
iso.set(10, 1, 10, 1)
E.act()
check(iso.get(10, 1, 10) == 6, "水の所に置くと置き換える")
E.set_cur(1)
E.act()
check(iso.get(10, 1, 10) == 0, "水を持っているときはブロックを消す")
E.act()
check(iso.get(10, 1, 10) == 1, "水を持って空気の所で押すと水を置く")

-- ---- カーソル・視点 ----
E.center_on(0, 0, 0)
E.set_cursor(0, 0, 0)
E.move(-1, -1, -1)
cx, cy, cz = E.cursor()
check(cx == 0 and cy == 0 and cz == 0, "カーソルは世界の外へ出ない")
local n0 = count("dirty_block")
E.move(1, 1, 1)
cx, cy, cz = E.cursor()
check(cx == 1 and cy == 1 and cz == 1, "カーソルが動く")
check(count("dirty_block") == n0 + 2, "前と今のカーソルの1マスだけ描き直す")
E.set_cursor(1023, 15, 1023)
E.move(1, 1, 1)
cx, cy, cz = E.cursor()
check(cx == 1023 and cy == 15 and cz == 1023, "反対の端でも世界の外へ出ない")
E.center_on(5, 0, 5)
E.set_cursor(5, 0, 5)
local o1 = { E.origin() }
for _ = 1, 8 do E.move(1, 0, -1) end
local o2 = { E.origin() }
check(o1[1] ~= o2[1], "カーソルが表示の端へ寄ったら視点を真ん中へ戻す")
local before = { E.origin() }
E.scroll(32, -16)
local after = { E.origin() }
check(after[1] == before[1] + 32 and after[2] == before[2] - 16, "ドラッグ/矢印で視点が動く")

-- タップ: 面の手前へ
E.pick_to(100, 100, false)
cx, cy, cz = E.cursor()
check(cx == 20 and cy == 1 and cz == 20, "上面のタップはその上へカーソル")
E.pick_to(100, 100, true)
cx, cy, cz = E.cursor()
check(cx == 20 and cy == 0 and cz == 20, "長押しはそのブロックへカーソル")

-- ---- ブロックを選ぶ画面 ----
E.open_select()
check(E.mode() == "select", "ブロックを選ぶ画面")
E.choose(3)
check(E.mode() == "play", "選ぶと遊ぶ画面へ戻る")

-- 選ぶ画面は7列: 松明(25)を含む25種類が4段に収まる
check(#E.order == 25 and E.order[#E.order] == 1 and E.order[24] == 25, "選べるのは25種類(松明の次に水)")
check(E.select_at(1 + 34 * 3 + 5, 28 + 42 * 3 + 5) == 25 and E.select_at(1 + 5, 28 + 5) == 1
      and E.select_at(1 + 34 * 6 + 5, 28 + 5) == 7, "7列 x 4段のタップ位置からブロックを選ぶ")
check(E.select_at(1 + 34 * 6 + 5, 28 + 42 * 3 + 5) == nil, "並びの外は選ばない")
check(28 + 42 * 3 + 38 <= 204, "4段とも表示に収まる")

-- 松明を置く/取る
E.set_cursor(30, 1, 30)
E.set_cur(25)
E.act()
check(iso.get(30, 1, 30) == 25, "松明を置ける")
E.act()
check(iso.get(30, 1, 30) == 0, "松明を取れる")

-- 昼/夜
local function last(name)
    for i = #calls, 1, -1 do if calls[i][1] == name then return calls[i] end end
end
E.set_night(true)
check(E.night() and last("sunlight")[2] == false and last("sky")[2] == 0, "夜にすると日の光を消して空を暗く")
E.set_night(false)
check(not E.night() and last("sunlight")[2] == true and last("sky")[2] == 7, "昼に戻すと空の色も戻る")

-- ---- 保存して戻る → 読む ----
E.set_cursor(513, 3, 511)
E.set_cur(5)
E.act()
E.save_and_quit()
run_afters()
check(E.mode() == "title" and count("save") == 1 and count("close") == 1, "保存して閉じ、ワールドを選ぶ画面へ")
check(nents() == 0 and E.mobs.count() == 0, "閉じると村人と羊も片付ける")
local s = saves["/app/worlds/B"]
check(s and s.x == 513 and s.y == 3 and s.z == 511 and s.cur == 5, "カーソルの位置と選んだブロックを保存する")
E.start(nil)
run_afters()
check(count("open") == 1 and E.mode() == "load", "保存したワールドを開く")
frames(1)
cx, cy, cz = E.cursor()
check(E.mode() == "play" and cx == 513 and cy == 3 and cz == 511 and iso.get(513, 3, 511) == 5, "続きから")

-- ---- 前の版のファイルからの移し替え ----
E.save_and_quit()
run_afters()
files["/app/worlds/world_C.dat"] = "BLK1..."
E.slot(3)
E.start(nil)
run_afters()
check(count("migrate") == 1, "前の版のファイルしか無ければ移してから開く")
frames(1)
cx, cy, cz = E.cursor()
check(E.mode() == "play" and cx == 3 and cy == 4 and cz == 5, "移したワールドで遊べる")

-- 開けないワールドは理由を出して選ぶ画面へ
E.save_and_quit()
run_afters()
saves["/app/worlds/D"] = nil
E.slot(4)
E.start(nil)
run_afters()
check(E.mode() == "title", "開けないワールドは選ぶ画面へ戻る")

-- main.lua は16KiB、require のモジュールは32KiBまで
local function size(name)
    local f = assert(io.open(APP .. name, "rb"))
    local n = #f:read("a")
    f:close()
    return n
end
check(size("main.lua") <= 16384 and size("game.lua") <= 32768 and size("mobs.lua") <= 32768,
      "main.lua は16KiB、game.lua・mobs.lua は32KiB以内 (" .. size("main.lua") .. ", " .. size("game.lua") .. ", "
      .. size("mobs.lua") .. ")")

print(fails == 0 and "blocks_test: 全部通りました" or ("blocks_test: " .. fails .. " 件失敗"))
if fails > 0 then os.exit(1) end
