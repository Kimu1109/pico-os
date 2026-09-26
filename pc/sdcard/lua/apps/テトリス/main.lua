-- テトリス風ゲーム。盤面/NEXT・HOLD/操作ボタンはそれぞれCanvas 1枚で、render()で直接描く。
-- ミノの絵は blocks.pimg(12x12のタイルを横に8枚: I O T S Z J L ゴースト)から
-- pico.draw_image_part()で切り出す。作り直すときは script/generate_tetris_blocks.py。
-- 操作: タッチ(下のボタン・盤面タップ=右回転・HOLD枠タップ)と外部コントローラー。
-- ※LuaSceneが読むのは16KiBまで(日本語コメントは1文字3バイト)

local DIR = "/lua/apps/テトリス/"
local W, H, HID = 10, 20, 2          -- 盤面の幅/高さ + 見えない上の段
local T = 12                         -- タイルの大きさ(px)
local cx, cy = pico.content_rect()
local BX, BY = cx + 5, cy + 3        -- 盤面の左上(見える1段目)
local SX, SY, SW, SH = cx + 130, cy + 2, 106, 208
local PY, PH = cy + 246, 54

-- 入力のビット(タッチとコントローラーを同じ形にまとめる)
local LEFT, RIGHT, DOWN, HARD, CW, CCW, HOLD, PAUSE, BACK = 1, 2, 4, 8, 16, 32, 64, 128, 256
local PADMAP = { left = LEFT, right = RIGHT, down = DOWN, up = HARD, a = CW, x = CW, b = CCW, y = CCW,
    l = HOLD, r = HOLD, zl = HOLD, zr = HOLD, start = PAUSE, home = BACK }
local BTNS = { LEFT, DOWN, RIGHT, HARD, CCW, CW } -- 下の操作ボタン(左から、40px幅)

-- ミノの形・壁蹴りの表と操作ボタンの絵は lib.lua(16KiBに収めるため分けた)
local LIB
do
    local code = pico.sd_read(DIR .. "lib.lua")
    local chunk = code and load(code, "lib")
    local ok, r = false, nil
    if chunk then ok, r = pcall(chunk) end
    if ok then LIB = r else error("lib.lua を読めません: " .. tostring(r)) end
end
local ROT, KJ, KI, COL = LIB.ROT, LIB.KJ, LIB.KI, LIB.COL

local img = pico.image_load(DIR .. "blocks.pimg")
if not img then pico.log("テトリス: blocks.pimg を読めません。色だけで描きます") end

-- ---- ゲームの状態 ----
local g = {}          -- 盤面 g[y*W+x+1] (y=0..H+HID-1)。0=空、1〜7=ミノ
local cur = {}        -- 画面に出している見た目(見える20段、8=ゴースト)
local state = "title" -- title / play / pause / clear / over
local p, rot, px, py  -- 落下中のミノ
local queue, bag = {}, {}
local hold, held_used = 0, false
local score, lines, level = 0, 0, 1
local hiscore = tonumber(pico.sd_read(DIR .. "hiscore.txt") or "") or 0
local fall_t, lock_t, resets, low_y = 0, 0, 0, 0
local das_dir, das_t = 0, 0
local clear_rows, clear_t = {}, 0
local bgm = true
local prev_in, latch, touch_btn = 0, 0, 0
local side_dirty, board_all = true, true

local board_id, side_id, pad_id, pause_btn

local function se(freq, ms, wave, vol, env)
    pico.sound_play(2, freq, ms, { wave = wave or "pulse25", volume = vol or 8, envelope = env or 0 })
end

local function music(on)
    if on and bgm then
        local ok, err = pico.music_play(DIR .. "bgm.mml")
        if not ok then pico.log("テトリス: BGM " .. tostring(err)) end
    else
        pico.music_stop()
    end
end

local function fits(pp, r, x, y)
    local c = ROT[pp][r]
    for i = 1, 8, 2 do
        local bx, by = x + c[i], y + c[i + 1]
        if bx < 0 or bx >= W or by >= H + HID then return false end
        if by >= 0 and g[by * W + bx + 1] ~= 0 then return false end
    end
    return true
end

local function ghostY()
    local y = py
    while fits(p, rot, px, y + 1) do y = y + 1 end
    return y
end

local function nextPiece()
    if #bag == 0 then
        bag = { 1, 2, 3, 4, 5, 6, 7 }
        for i = 7, 2, -1 do
            local j = math.random(i)
            bag[i], bag[j] = bag[j], bag[i]
        end
    end
    return table.remove(bag)
end

local function gameOver()
    state = "over"
    board_all, side_dirty = true, true
    music(false)
    se(160, 700, "pulse50", 10, -2)
    if score > hiscore then
        hiscore = score
        pico.sd_write(DIR .. "hiscore.txt", tostring(hiscore))
    end
end

local function spawn(kind)
    if not kind then
        kind = table.remove(queue, 1)
        queue[#queue + 1] = nextPiece()
    end
    p, rot, px, py = kind, 0, 3, 1
    fall_t, lock_t, resets, low_y = 0, 0, 0, py
    side_dirty = true
    if not fits(p, rot, px, py) then gameOver() end
end

local function newGame()
    for i = 1, W * (H + HID) do g[i] = 0 end
    bag, queue = {}, {}
    for i = 1, 5 do queue[i] = nextPiece() end
    hold, held_used = 0, false
    score, lines, level = 0, 0, 1
    state = "play"
    board_all, side_dirty = true, true
    spawn()
    music(true)
end

local function interval()
    local lv = math.min(level, 20)
    return math.max((0.8 - (lv - 1) * 0.007) ^ (lv - 1) * 1000, 16)
end

-- 接地中に動かせたら固定までの猶予を戻す(15回まで)
local function moved()
    if not fits(p, rot, px, py + 1) and resets < 15 then
        lock_t = 0
        resets = resets + 1
    end
end

local function shift(dx)
    if fits(p, rot, px + dx, py) then
        px = px + dx
        moved()
        return true
    end
    return false
end

local function rotate(dir)
    if p == 2 then return end
    local to = (rot + dir) % 4
    local k = (p == 1) and KI or KJ
    local tbl, sg = k[rot], 1
    if dir < 0 then tbl, sg = k[to], -1 end
    for i = 1, 10, 2 do
        local dx, dy = tbl[i] * sg, -tbl[i + 1] * sg
        if fits(p, to, px + dx, py + dy) then
            rot, px, py = to, px + dx, py + dy
            moved()
            se(1400, 25, "pulse12", 5)
            return
        end
    end
end

local function lock()
    local c = ROT[p][rot]
    local visible = false
    for i = 1, 8, 2 do
        local y = py + c[i + 1]
        g[y * W + px + c[i] + 1] = p
        if y >= HID then visible = true end
    end
    held_used = false
    clear_rows = {}
    for y = 0, H + HID - 1 do
        local full = true
        for x = 0, W - 1 do
            if g[y * W + x + 1] == 0 then full = false break end
        end
        if full then clear_rows[#clear_rows + 1] = y end
    end
    local n = #clear_rows
    if n > 0 then
        score = score + ({ 100, 300, 500, 800 })[n] * level
        lines = lines + n
        level = 1 + lines // 10
        state, clear_t = "clear", 0
        if n == 4 then se(1760, 300, "pulse25", 12, -2) else se(880 + n * 220, 180, "pulse50", 10, -3) end
        side_dirty = true
        p = nil
        return
    end
    se(3000, 60, "noise", 6, -1)
    if not visible then p = nil gameOver() return end -- 見える段より上だけで固まった: 終わり
    spawn()
end

local function finishClear()
    for _, row in ipairs(clear_rows) do
        for y = row, 1, -1 do
            for x = 1, W do g[y * W + x] = g[(y - 1) * W + x] end
        end
        for x = 1, W do g[x] = 0 end
    end
    clear_rows = {}
    state = "play"
    spawn()
end

local function doHold()
    if held_used then return end
    held_used = true
    local k = hold
    hold = p
    se(600, 40, "pulse25", 6)
    if k == 0 then spawn() else spawn(k) end
    held_used = true
end

-- ---- 盤面の見た目の差分をdirtyにする ----
local function compose()
    local x0, y0, x1, y1 = W, H, -1, -1
    local gy, cells = -1, nil
    if p and state ~= "over" then gy, cells = ghostY(), ROT[p][rot] end
    for y = 0, H - 1 do
        for x = 0, W - 1 do
            local i = y * W + x + 1
            local v = g[(y + HID) * W + x + 1]
            if state == "clear" then
                for _, r in ipairs(clear_rows) do if r == y + HID then v = 9 end end
            end
            if cells and v == 0 then
                for k = 1, 8, 2 do
                    local cxx = px + cells[k]
                    if cxx == x then
                        if py + cells[k + 1] == y + HID then v = p
                        elseif gy + cells[k + 1] == y + HID and v == 0 then v = 8 end
                    end
                end
            end
            if cur[i] ~= v then
                cur[i] = v
                if x < x0 then x0 = x end
                if x > x1 then x1 = x end
                if y < y0 then y0 = y end
                if y > y1 then y1 = y end
            end
        end
    end
    if board_all then
        pico.invalidate(board_id)
        board_all = false
    elseif x1 >= 0 then
        pico.mark_dirty(BX + x0 * T, BY + y0 * T, (x1 - x0 + 1) * T, (y1 - y0 + 1) * T)
    end
end

-- ---- 描画 ----
local function tile(v, x, y)
    if v == 9 then
        pico.fill_rect(x, y, T, T, 15)
    elseif img then
        pico.draw_image_part(img, x, y, (v - 1) * T, 0, T, T)
    elseif v == 8 then
        pico.draw_rect(x, y, T, T, 8)
    else
        pico.fill_rect(x, y, T, T, COL[v])
    end
end

local textW = LIB.textW

local function center(s, x, w, y, color)
    pico.draw_text(x + (w - textW(s)) // 2, y, s, color, 0)
end

local function renderBoard()
    local x, y, w, h = pico.get_draw_area()
    if w <= 0 then x, y, w, h = BX, BY, W * T, H * T end
    local c0, c1 = math.max(0, (x - BX) // T), math.min(W - 1, (x + w - 1 - BX) // T)
    local r0, r1 = math.max(0, (y - BY) // T), math.min(H - 1, (y + h - 1 - BY) // T)
    for r = r0, r1 do
        for c = c0, c1 do
            local v = cur[r * W + c + 1]
            if v and v > 0 then tile(v, BX + c * T, BY + r * T) end
        end
    end
    pico.draw_rect(BX - 1, BY - 1, W * T + 2, H * T + 2, 7)

    local l1, l2
    if state == "title" then l1, l2 = "テトリス", "タップで開始"
    elseif state == "pause" then l1, l2 = "一時停止中", "タップで再開"
    elseif state == "over" then l1, l2 = "GAME OVER", "タップでもう一度" end
    if l1 then
        pico.fill_rect(BX + 4, BY + 90, W * T - 8, 56, 0)
        pico.draw_rect(BX + 4, BY + 90, W * T - 8, 56, 15)
        center(l1, BX, W * T, BY + 98, 14)
        center(l2, BX, W * T, BY + 122, 15)
    end
end

local function mini(kind, x, y, w, h) LIB.mini(ROT, T, tile, kind, x, y, w, h) end

local function renderSide()
    pico.draw_text(SX + 2, SY, "HOLD", 0, 0)
    pico.draw_rect(SX, SY + 18, 52, 32, held_used and 8 or 0)
    mini(hold, SX, SY + 18, 52, 32)
    pico.draw_text(SX + 2, SY + 54, "BGM", 0, 0)
    pico.draw_text(SX + 2, SY + 72, bgm and "ON" or "OFF", bgm and 2 or 8, 0)
    pico.draw_rect(SX, SY + 52, 52, 40, 7)

    pico.draw_text(SX + 56, SY, "NEXT", 0, 0)
    pico.draw_rect(SX + 54, SY + 18, 52, 92, 0)
    for i = 1, 3 do mini(queue[i], SX + 54, SY + 20 + (i - 1) * 30, 52, 28) end

    local rows = { { "SCORE", score }, { "LEVEL", level }, { "LINES", lines }, { "HI", hiscore } }
    for i, r in ipairs(rows) do
        local y = SY + 116 + (i - 1) * 23
        pico.draw_text(SX + 2, y, r[1], 8, 0)
        local v = tostring(r[2])
        pico.draw_text(SX + SW - 2 - textW(v), y, v, 0, 0)
    end
end

local function renderPad()
    LIB.drawPad(prev_in, cx, PY, PH, BTNS, LEFT, RIGHT, DOWN, HARD, CW)
end

-- ---- ウィジェット ----
local function canvas(x, y, w, h, bg, fn)
    local id = pico.create("Canvas")
    pico.set(id, "x", x); pico.set(id, "y", y)
    pico.set(id, "w", w); pico.set(id, "h", h)
    pico.set(id, "background_color", bg)
    pico.on(id, "render", fn)
    return id
end

board_id = canvas(BX - 1, BY - 1, W * T + 2, H * T + 2, 0, renderBoard)
side_id = canvas(SX, SY, SW, SH, 15, renderSide)
pad_id = canvas(cx, PY, 240, PH, 15, renderPad)

local function button(text, x, fn)
    local id = pico.create("Button")
    pico.set(id, "x", x); pico.set(id, "y", cy + 214)
    pico.set(id, "w", 50); pico.set(id, "h", 28)
    pico.set(id, "font_size", 0); pico.set(id, "text", text)
    pico.on(id, "press_start", fn)
    return id
end

local function setPause(on)
    if on and state == "play" then
        state = "pause"
        music(false)
    elseif not on and state == "pause" then
        state = "play"
        music(true)
    else
        return
    end
    board_all = true
    pico.set(pause_btn, "text", on and "再開" or "停止")
end

local function leave()
    if score > hiscore then pico.sd_write(DIR .. "hiscore.txt", tostring(score)) end
    pico.music_stop()
    pico.pop()
end

pause_btn = button("停止", SX, function() setPause(state == "play") end)
button("戻る", SX + 56, leave)

-- 盤面のタップ: 遊んでいる間は右回転、それ以外は開始/再開
pico.on(board_id, "press_start", function()
    if state == "play" then latch = latch | CW
    elseif state == "pause" then setPause(false)
    elseif state == "title" or state == "over" then newGame() end
end)

pico.on(side_id, "press_start", function()
    local _, y = pico.get_touch()
    if y < SY + 52 then
        latch = latch | HOLD
    elseif y < SY + 92 then
        bgm = not bgm
        side_dirty = true
        music(state == "play")
    end
end)

-- 下の操作ボタン。押したまま指を滑らせると隣のボタンへ移る
local function padTouch()
    local x = pico.get_touch()
    local b = BTNS[math.max(1, math.min(6, (x - cx) // 40 + 1))]
    if b ~= touch_btn then latch = latch | b end
    touch_btn = b
end
pico.on(pad_id, "press_start", padTouch)
pico.on(pad_id, "press_move", padTouch)
pico.on(pad_id, "press_end", function() touch_btn = 0 end)
pico.on(pad_id, "press_out", function() touch_btn = 0 end)

-- ---- 毎フレーム ----
local function readInput()
    local held = touch_btn
    if pico.pad_connected() then
        for name, bit in pairs(PADMAP) do
            if pico.pad_down(name) then held = held | bit end
        end
    end
    local pressed = (held & ~prev_in) | latch
    latch = 0
    if held ~= prev_in then pico.invalidate(pad_id) end
    prev_in = held
    return held, pressed
end

local function play(dt, held, pressed)
    if pressed & HOLD ~= 0 then doHold() end
    if not p or state ~= "play" then return end
    if pressed & CW ~= 0 then rotate(1) end
    if pressed & CCW ~= 0 then rotate(-1) end

    -- 左右: 押した瞬間に1マス、170ms押し続けたら50msごと
    if pressed & LEFT ~= 0 then das_dir, das_t = -1, 0 shift(-1)
    elseif pressed & RIGHT ~= 0 then das_dir, das_t = 1, 0 shift(1) end
    local want = das_dir < 0 and LEFT or RIGHT
    if das_dir ~= 0 and held & want == 0 then
        das_dir, das_t = 0, 0
        if held & LEFT ~= 0 then das_dir = -1 elseif held & RIGHT ~= 0 then das_dir = 1 end
    elseif das_dir ~= 0 then
        das_t = das_t + dt
        while das_t >= 170 do
            das_t = das_t - 50
            if not shift(das_dir) then das_t = 169 break end
        end
    end

    if pressed & HARD ~= 0 then
        local gy = ghostY()
        score = score + (gy - py) * 2
        py = gy
        side_dirty = true
        lock()
        return
    end

    local iv = interval()
    local soft = held & DOWN ~= 0
    if soft then iv = math.min(iv, 30) end
    fall_t = fall_t + dt
    while fall_t >= iv do
        fall_t = fall_t - iv
        if fits(p, rot, px, py + 1) then
            py = py + 1
            if py > low_y then low_y, resets = py, 0 end
            if soft then score = score + 1 side_dirty = true end
        else
            fall_t = 0
            break
        end
    end
    if not fits(p, rot, px, py + 1) then
        lock_t = lock_t + dt
        if lock_t >= 500 then lock() end
    else
        lock_t = 0
    end
end

function loop(dt)
    if dt > 100 then dt = 100 end
    local held, pressed = readInput()

    if pressed & BACK ~= 0 then leave() return end
    if state == "play" then
        if pressed & PAUSE ~= 0 then setPause(true)
        else play(dt, held, pressed) end
    elseif state == "clear" then
        clear_t = clear_t + dt
        if clear_t >= 200 then finishClear() end
    elseif pressed & (PAUSE | CW | HARD) ~= 0 then
        if state == "pause" then setPause(false) else newGame() end
    end

    compose()
    if side_dirty then
        pico.invalidate(side_id)
        side_dirty = false
    end
end

for i = 1, W * (H + HID) do g[i] = 0 end
