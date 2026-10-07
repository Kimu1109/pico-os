-- ブロック: 斜め上から見た(アイソメトリック)ワールドの描画と、タップ位置からのブロックの引き当て
--
-- ブロック(x, y, z) の絵(32x31)の左上は 画面の (OX + 16*(x - z), OY - 8*(x + z) - 16*y)。
-- x が増えると右上、z が増えると左上、y が増えると真上へずれる。見えるのは 上面・左面(-x側)・右面(-z側)。
-- 描く順(画家のアルゴリズム): s = x + z の大きい(奥の)順、同じ s の中は y の小さい順。
-- 同じ s の柱どうしは横に32pxずつ離れていて重ならないので、この順で手前が後から描かれる。
--
-- 影(元と同じ形): 太陽は (-1, +1, +1) の向き。日の当たりうる上面と左面を、光から見た三角形2つに分け
-- (上面は x+z が一定の線=画面の横の中央線で奥/手前、左面は y=z の線で上/下)、三角形ごとに影を決める。
-- 元は「光から見た三角形の格子に一番光に近いブロックの深さを書く」作りだが、三角形の中の1点から
-- 太陽へ向かう直線が通るマスを調べるのと同じ答えになる。直線は1歩(-1,+1,+1)ごとに3マスを通り、
-- 2つの三角形で2マスを共有するので、1歩あたり4マスを見る(top_shadow / left_shadow)。
-- 影の色は明るさ半分(faces.pimg に影の絵として持つ)。右面(-z側)は太陽の反対を向くのでいつも影。
-- 水は影を作らず、影も受けない。
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

V.top = H - 1          -- 空気でない一番上の高さ(これより上を光はたどらない。render が毎回求める)

-- 上面の影: 奥半分・手前半分がそれぞれ影なら true。たどるマス(k歩目、高さ y+1+k):
--   A=(x-k, z+k) 両方 / B=(x-k, z+1+k) 奥 / C=(x-1-k, z+k) 手前 / D=(x-1-k, z+1+k) 両方
local function top_shadow(x, y, z)
    local far, near = false, false
    local k = 0
    while y + 1 + k <= V.top do
        local layer = L[y + 2 + k]
        local x0, x1, z0, z1 = x - k, x - 1 - k, z + k, z + 1 + k
        if x0 < 0 or z0 >= N then break end
        if byte(layer, x0 * N + z0 + 1) >= 2 then return true, true end
        if not far and z1 < N and byte(layer, x0 * N + z1 + 1) >= 2 then far = true end
        if x1 >= 0 then
            if z1 < N and byte(layer, x1 * N + z1 + 1) >= 2 then return true, true end
            if not near and byte(layer, x1 * N + z0 + 1) >= 2 then near = true end
        end
        if far and near then break end
        k = k + 1
    end
    return far, near
end

-- 左面の影: 上半分・下半分がそれぞれ影なら true。たどるマス(k歩目、x-1-k):
--   E=(y+k, z+k) 両方 / F=(y+1+k, z+k) 上 / G=(y+k, z+1+k) 下 / J=(y+1+k, z+1+k) 両方
local function left_shadow(x, y, z)
    local up, low = false, false
    local k = 0
    while true do
        local xx, y0, z0 = x - 1 - k, y + k, z + k
        if xx < 0 or y0 > V.top or z0 >= N then break end
        local i0 = xx * N + z0 + 1
        local la, lb = L[y0 + 1], L[y0 + 2]
        if byte(la, i0) >= 2 then return true, true end
        local z1ok = z0 + 1 < N
        if z1ok and not low and byte(la, i0 + 1) >= 2 then low = true end
        if lb and y0 + 1 <= V.top then
            if z1ok and byte(lb, i0 + 1) >= 2 then return true, true end
            if not up and byte(lb, i0) >= 2 then up = true end
        end
        if up and low then break end
        k = k + 1
    end
    return up, low
end
V.top_shadow, V.left_shadow = top_shadow, left_shadow

-- 空気でない一番上の高さ
local function find_top()
    local air = L.air
    for y = H, 1, -1 do
        if L[y] ~= air then return y - 1 end
    end
    return -1
end

function V.block_pos(x, y, z)
    return V.OX + 16 * (x - z), V.OY - 8 * (x + z) - 16 * y
end

-- 1個のブロックをまるごと(上・左・右の面、日なた)描く。選ぶ画面とボタンの絵に使う
function V.draw_icon(id, px, py)
    local sy = (id - 1) * ROW
    draw_part(V.img, px, py, 0, sy, 32, 15)
    draw_part(V.img, px, py + 8, 128, sy, 16, 23)
    draw_part(V.img, px + 16, py + 8, 192, sy, 16, 23)
end

local function draw_cursor()
    local bx, by = V.block_pos(V.cx, V.cy, V.cz)
    local sy = CURSOR_ROW * ROW
    draw_part(V.img, bx, by, 0, sy, 32, 15)
    draw_part(V.img, bx, by + 8, 128, sy, 16, 23)
    draw_part(V.img, bx + 16, by + 8, 192, sy, 16, 23)
end

-- 画面の矩形 (cx, cy, cw, ch) の中だけを描き直す。Canvas の render の中から呼ぶ
function V.render(cx, cy, cw, ch)
    local x0, y0, x1, y1 = cx, cy, cx + cw, cy + ch
    fill_rect(cx, cy, cw, ch, V.sky)
    local img, OX, OY = V.img, V.OX, V.OY
    V.top = find_top()
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
                                local f, n = top_shadow(x, y, z)
                                draw_part(img, bx, by, f and (n and 32 or 64) or (n and 96 or 0), sy, 32, 15)
                            end
                            if l < 2 then
                                local u, w = left_shadow(x, y, z)
                                draw_part(img, bx, by + 8, u and (w and 144 or 160) or (w and 176 or 128), sy, 16, 23)
                            end
                            if r < 2 then
                                draw_part(img, bx + 16, by + 8, 192, sy, 16, 23)
                            end
                        else
                            -- 水: 空気に面した所だけ、市松模様で半分透けた面を描く
                            if a == 0 then draw_part(img, bx, by, 0, 0, 32, 15) end
                            if l == 0 then draw_part(img, bx, by + 8, 128, 0, 16, 23) end
                            if r == 0 then draw_part(img, bx + 16, by + 8, 192, 0, 16, 23) end
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

-- (x, y, z) を置いた/壊したとき: そのブロックと、影が変わりうる面。
-- 太陽へ向かう直線が P を通るのは、上面なら P + {(0,-1,0),(0,-1,-1),(1,-1,0),(1,-1,-1)} + k(1,-1,-1)、
-- 左面なら P + {(1,0,0),(1,-1,0),(1,0,-1),(1,-1,-1)} + k(1,-1,-1) のブロック。k ごとにその8個の絵を
-- 覆う矩形(左上が P の絵から (32k, 16k-8)、64x63)を描き直す(P 自身と、面の見え方が変わる隣も入る)
function V.dirty_edit(x, y, z)
    local bx, by = V.block_pos(x, y, z)
    for k = 0, y + 1 do
        dirty(bx + 32 * k, by - 8 + 16 * k, 64, 63)
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
