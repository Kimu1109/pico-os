-- ブロック: デモのワールドの家・東屋・ピラミッド・池・木(元の gen_demo と同じ。左下の 48x48 の中)。
-- 直方体 (x0, y0, z0, x1, y1, z1, ブロック) を7バイトの文字列にした並びを返す(テーブルのままだと
-- 読み込んだ後もずっと約8KBを使うため)。デモのワールドを開くときだけ読み込む(game.lua)。
local world = require("world")
local B, H = world.B, world.H
local N = 48
local o = {}
local function f(x0, y0, z0, x1, y1, z1, b) o[#o + 1] = { x0, y0, z0, x1, y1, z1, b } end
local function s(x, y, z, b) f(x, y, z, x, y, z, b) end
f(32, 1, 0, N - 1, 4, 16, B.WATER)
f(1, 1, N - 2, 1, H - 1, N - 2, B.BRICKS)
for _, t in ipairs({ { 8, 7 }, { 23, 15 }, { 4, 26 }, { 16, 23 }, { 28, 28 } }) do
    world.tree_ops(o, t[1], 1, t[2])
end
-- 家
f(18, 0, 3, 24, 0, 9, B.PLANKS)
f(18, 1, 3, 24, 1, 9, B.WATER)
f(25, 1, 3, 25, 3, 9, B.BRICKS)
f(18, 1, 10, 24, 3, 10, B.BRICKS)
f(17, 1, 3, 17, 1, 9, B.SAND)
f(17, 1, 10, 17, 3, 10, B.SLABS)
f(25, 1, 10, 25, 3, 10, B.SLABS)
f(25, 1, 2, 25, 3, 2, B.SLABS)
s(18, 2, 3, B.TNT)
s(19, 1, 3, B.BOOKS)
s(24, 3, 5, B.COBBLE)
-- 東屋
f(N - 4, 1, 3, N - 4, 3, 3, B.SLABS)
f(N - 10, 1, 3, N - 10, 3, 3, B.SLABS)
f(N - 4, 1, 9, N - 4, 3, 9, B.SLABS)
f(N - 10, 1, 9, N - 10, 3, 9, B.SLABS)
f(N - 10, 4, 3, N - 4, 4, 9, B.SLABS)
f(N - 9, 4, 4, N - 8, 4, 5, B.WATER)
f(N - 6, 4, 4, N - 5, 4, 5, B.WATER)
f(N - 9, 4, 7, N - 8, 4, 8, B.WATER)
f(N - 6, 4, 7, N - 5, 4, 8, B.WATER)
-- ピラミッド
for i = 1, 8 do f(N - 1 - i, 9 - i, N - 1 - i, N - 1, 9 - i, N - 1, B.GOLD) end
for i, t in ipairs(o) do o[i] = string.char(table.unpack(t)) end
return o
