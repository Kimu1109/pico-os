-- ブロック: 斜め上から見た(アイソメトリック)ワールドの描画と、タップ位置からのブロックの引き当て
--
-- ブロック(x, y, z) の絵(32x31)の左上は 画面の (OX + 16*(x - z), OY - 8*(x + z) - 16*y)。
-- x が増えると右上、z が増えると左上、y が増えると真上へずれる。見えるのは 上面・左面(-x側)・右面(-z側)。
-- 描く順(画家のアルゴリズム): s = x + z の大きい(奥の)順、同じ s の中は y の小さい順。
-- 同じ s の柱どうしは横に32pxずつ離れていて重ならないので、この順で手前が後から描かれる。
--
-- 影: 太陽は (-1, +1, +1) の向き。面の手前のマスから太陽の方へたどり、不透明なブロックに当たれば影。
-- 右面(-z側)は太陽の反対を向くのでいつも影。水は影を作らず、影も受けない。
local world = require("world")

local V = {}

local N, H = world.N, world.H
local L = world.L
local byte = string.byte
local ROW = 23                     -- faces.pimg の1種類ぶんの段の高さ
local CURSOR_ROW = 24              -- カーソルの段(ブロック番号25の位置)

V.img = nil
V.sky = 11
V.OX, V.OY = 0, 0
V.x, V.y, V.w, V.h = 0, 20, 240, 204      -- 画面上の表示範囲
V.cx, V.cy, V.cz = 0, 0, 0                -- カーソル
V.show_cursor = true

local draw_part = pico.draw_image_part
local fill_rect = pico.fill_rect

-- (x, y, z) の空いているマスから太陽の方へたどって、日が当たるか
local function sunlit(x, y, z)
    while true do
        x = x - 1; y = y + 1; z = z + 1
        if x < 0 or y >= H or z >= N then return true end
        if byte(L[y + 1], x * N + z + 1) >= 2 then return false end
    end
end
V.sunlit = sunlit

function V.block_pos(x, y, z)
    return V.OX + 16 * (x - z), V.OY - 8 * (x + z) - 16 * y
end

-- 1個のブロックをまるごと(上・左・右の面、日なた)描く。選ぶ画面とボタンの絵に使う
function V.draw_icon(id, px, py)
    local sy = (id - 1) * ROW
    draw_part(V.img, px, py, 0, sy, 32, 15)
    draw_part(V.img, px, py + 8, 64, sy, 16, 23)
    draw_part(V.img, px + 16, py + 8, 96, sy, 16, 23)
end

local function draw_cursor()
    local bx, by = V.block_pos(V.cx, V.cy, V.cz)
    local sy = CURSOR_ROW * ROW
    draw_part(V.img, bx, by, 0, sy, 32, 15)
    draw_part(V.img, bx, by + 8, 64, sy, 16, 23)
    draw_part(V.img, bx + 16, by + 8, 96, sy, 16, 23)
end

-- 画面の矩形 (cx, cy, cw, ch) の中だけを描き直す。Canvas の render の中から呼ぶ
function V.render(cx, cy, cw, ch)
    local x0, y0, x1, y1 = cx, cy, cx + cw, cy + ch
    fill_rect(cx, cy, cw, ch, V.sky)
    local img, OX, OY = V.img, V.OX, V.OY
    -- 矩形にかかる柱の範囲(u = x - z, s = x + z)
    local umin = (x0 - OX - 32) // 16 + 1
    local umax = -((OX - x1) // 16) - 1
    local smin = (OY - 16 * (H - 1) - y1) // 8 + 1
    local smax = -((y0 - OY - 31) // 8) - 1
    if smin < 0 then smin = 0 end
    if smax > 2 * N - 2 then smax = 2 * N - 2 end
    for s = smax, smin, -1 do
        local base = OY - 8 * s
        local ylo = (base - y1) // 16 + 1
        local yhi = -((y0 - base - 31) // 16) - 1
        if ylo < 0 then ylo = 0 end
        if yhi > H - 1 then yhi = H - 1 end
        if ylo <= yhi then
            local ulo, uhi = umin, umax
            if ulo < -s then ulo = -s end
            if ulo < s - 2 * N + 2 then ulo = s - 2 * N + 2 end
            if uhi > s then uhi = s end
            if uhi > 2 * N - 2 - s then uhi = 2 * N - 2 - s end
            if (ulo - s) % 2 ~= 0 then ulo = ulo + 1 end
            for u = ulo, uhi, 2 do
                local x, z = (s + u) // 2, (s - u) // 2
                local i = x * N + z + 1
                local bx = OX + 16 * u
                for y = ylo, yhi do
                    local layer = L[y + 1]
                    local b = byte(layer, i)
                    if b ~= 0 then
                        local a = (y < H - 1) and byte(L[y + 2], i) or 0
                        local l = (x > 0) and byte(layer, i - N) or 0
                        local r = (z > 0) and byte(layer, i - 1) or 0
                        local by = base - 16 * y
                        if b >= 2 then
                            local sy = (b - 1) * ROW
                            if a < 2 then
                                draw_part(img, bx, by, sunlit(x, y + 1, z) and 0 or 32, sy, 32, 15)
                            end
                            if l < 2 then
                                draw_part(img, bx, by + 8, sunlit(x - 1, y, z) and 64 or 80, sy, 16, 23)
                            end
                            if r < 2 then
                                draw_part(img, bx + 16, by + 8, 96, sy, 16, 23)
                            end
                        else
                            -- 水: 空気に面した所だけ、市松模様で半分透けた面を描く
                            if a == 0 then draw_part(img, bx, by, 0, 0, 32, 15) end
                            if l == 0 then draw_part(img, bx, by + 8, 64, 0, 16, 23) end
                            if r == 0 then draw_part(img, bx + 16, by + 8, 96, 0, 16, 23) end
                        end
                    end
                end
            end
        end
    end
    if V.show_cursor then
        local bx, by = V.block_pos(V.cx, V.cy, V.cz)
        if bx < x1 and bx + 32 > x0 and by < y1 and by + 31 > y0 then draw_cursor() end
    end
end

-- ---------------------------------------------------------------- 描き直す範囲

local function dirty(x, y, w, h)
    -- 表示範囲の外は描き直さない(下の操作パネルを巻き込まない)
    local ax0, ay0 = math.max(x, V.x), math.max(y, V.y)
    local ax1, ay1 = math.min(x + w, V.x + V.w), math.min(y + h, V.y + V.h)
    if ax0 < ax1 and ay0 < ay1 then pico.mark_dirty(ax0, ay0, ax1 - ax0, ay1 - ay0) end
end

function V.dirty_block(x, y, z)
    local bx, by = V.block_pos(x, y, z)
    dirty(bx, by, 32, 31)
end

-- (x, y, z) を置いた/壊したとき: そのブロックと、影が変わりうる面(太陽と反対の方へ並ぶ面)
function V.dirty_edit(x, y, z)
    V.dirty_block(x, y, z)
    local bx, by = V.block_pos(x, y, z)
    for k = 0, H do
        -- 上面: (x+k, y-1-k, z-k)、左面: (x+1+k, y-k, z-k)
        if y - 1 - k >= 0 and x + k < N and z - k >= 0 then
            dirty(bx + 32 * k, by + 16 + 16 * k, 32, 15)
        end
        if y - k >= 0 and x + 1 + k < N and z - k >= 0 then
            dirty(bx + 16 + 32 * k, by + 16 * k, 16, 23)
        end
    end
end

-- ---------------------------------------------------------------- タップ位置の引き当て

-- 絵(32x31)の中の点 (lx, ly) が どの面か。"top" / "left" / "right"、外なら nil
local function face_at(lx, ly)
    if lx < 0 or lx > 31 or ly < 0 or ly > 30 then return nil end
    if ly <= 14 then
        local half = (ly <= 7) and 2 * (ly + 1) or 2 * (15 - ly)
        if lx >= 16 - half and lx < 16 + half then return "top" end
    end
    if lx < 16 then
        local i = lx // 2
        if ly >= 8 + i and ly <= 23 + i then return "left" end
    else
        local i = (lx - 16) // 2
        if ly >= 15 - i and ly <= 30 - i then return "right" end
    end
    return nil
end

-- 画面の点に見えている一番手前のブロックと、その面を返す(何も無ければ nil)
function V.pick(px, py)
    local OX, OY = V.OX, V.OY
    local u0 = (px - OX) // 16
    for s = 0, 2 * N - 2 do
        local u = ((u0 - s) % 2 == 0) and u0 or (u0 - 1)
        local x, z = (s + u) // 2, (s - u) // 2
        if x >= 0 and z >= 0 and x < N and z < N then
            local base = OY - 8 * s
            local bx = OX + 16 * u
            for y = H - 1, 0, -1 do
                local by = base - 16 * y
                if py >= by and py <= by + 30 and world.get(x, y, z) ~= 0 then
                    local f = face_at(px - bx, py - by)
                    if f then return x, y, z, f end
                end
            end
        end
    end
    return nil
end

return V
