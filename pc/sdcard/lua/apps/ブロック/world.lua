-- ブロック: ワールドのデータ・生成・保存
-- 元: TheScienceElf/Blocks-TI-84 の world / worldgen / world_io(MIT)
--
-- 世界は N x H x N(48 x 16 x 48)。高さ y ごとに N*N バイトの文字列1本(L[y+1])で持ち、
-- (x, z) のブロック番号は その文字列の x*N + z + 1 バイト目。Luaのテーブルに1マス1要素で持つと
-- 約590KB、64bit整数に8マスずつ詰めても約74KBになり、Luaの予算(200KB)に収まらないため。
-- 書き換えはその高さの文字列を作り直す(2.3KB)。空気だけの高さは同じ文字列を共有する。
local M = {}

local N, H = 48, 16
local NN = N * N
M.N, M.H = N, H

local B = {
    AIR = 0, WATER = 1, STONE = 2, GRASS = 3, DIRT = 4, COBBLE = 5, PLANKS = 6, BRICKS = 7,
    SLABS = 8, WOOD = 9, LEAVES = 10, SAND = 11, BOOKS = 12, TNT = 13, CRAFTING = 14,
    FURNACE = 15, JUKEBOX = 16, SPONGE = 17, GRAVEL = 18, MOSS = 19, COAL_ORE = 20,
    IRON_ORE = 21, BEDROCK = 22, IRON = 23, GOLD = 24,
}
M.B = B
M.COUNT = 24          -- ブロックの種類(水を含む)

local L = {}
M.L = L

local byte, char, sub, rep = string.byte, string.char, string.sub, string.rep
local WATER_LEVEL = 5
local AIR_LAYER = rep("\0", NN)

function M.clear()
    for y = 1, H do L[y] = AIR_LAYER end
end
M.clear()

local function get(x, y, z)
    if x < 0 or z < 0 or y < 0 or x >= N or z >= N or y >= H then return 0 end
    return byte(L[y + 1], x * N + z + 1)
end
M.get = get

local function set(x, y, z, b)
    if x < 0 or z < 0 or y < 0 or x >= N or z >= N or y >= H then return end
    local s, i = L[y + 1], x * N + z + 1
    L[y + 1] = sub(s, 1, i - 1) .. char(b) .. sub(s, i + 1)
end
M.set = set

-- 両端を含む直方体を塗る(世界の外は切り捨てる)
local function fill(x0, y0, z0, x1, y1, z1, b)
    x0, z0, y0 = math.max(x0, 0), math.max(z0, 0), math.max(y0, 0)
    x1, z1, y1 = math.min(x1, N - 1), math.min(z1, N - 1), math.min(y1, H - 1)
    if x0 > x1 or z0 > z1 then return end
    local c = char(b)
    for y = y0, y1 do
        local s = L[y + 1]
        if z0 == 0 and z1 == N - 1 then
            s = sub(s, 1, x0 * N) .. rep(c, (x1 - x0 + 1) * N) .. sub(s, (x1 + 1) * N + 1)
        else
            local run = rep(c, z1 - z0 + 1)
            for x = x0, x1 do
                local i = x * N + z0 + 1
                s = sub(s, 1, i - 1) .. run .. sub(s, i + z1 - z0 + 1)
            end
        end
        L[y + 1] = s
    end
end
M.fill = fill

-- (x, y, z) を根元にした木
local function add_tree(x, y, z)
    fill(x - 2, y + 3, z - 2, x + 2, y + 4, z + 2, B.LEAVES)
    fill(x - 1, y + 5, z - 1, x + 1, y + 5, z + 1, B.LEAVES)
    set(x + 1, y + 6, z, B.LEAVES)
    set(x - 1, y + 6, z, B.LEAVES)
    set(x, y + 6, z + 1, B.LEAVES)
    set(x, y + 6, z - 1, B.LEAVES)
    set(x, y + 6, z, B.LEAVES)
    fill(x, y, z, x, y + 5, z, B.WOOD)
end

-- 下から見て最初の空気のマス
local function first_air(x, z)
    local y = 0
    while y < H - 1 and get(x, y, z) ~= B.AIR do y = y + 1 end
    return y
end

-- 自然なワールド: 8マスごとの格子に高さ(3〜12)を決めて間を補間し、水面(5)より低い所は水。
-- 水辺は砂、木を12本まで、石の10%を石炭/鉄鉱石に。
function M.gen_natural()
    local STEP = 8
    local GS = N // STEP + 1
    local grid = {}
    for i = 0, GS * GS - 1 do grid[i] = math.random(3, 12) end
    -- 高さの地図(N*N バイトの文字列)
    local rows, row = {}, {}
    for x = 0, N - 1 do
        local gx, lx = x // STEP, x % STEP
        for z = 0, N - 1 do
            local gz, lz = z // STEP, z % STEP
            local h = grid[gx * GS + gz] * (STEP - lx) * (STEP - lz)
                    + grid[(gx + 1) * GS + gz] * lx * (STEP - lz)
                    + grid[gx * GS + gz + 1] * (STEP - lx) * lz
                    + grid[(gx + 1) * GS + gz + 1] * lx * lz
            row[z + 1] = h // (STEP * STEP)
        end
        rows[x + 1] = char(table.unpack(row, 1, N))
    end
    local hmap = table.concat(rows)
    -- 水辺の砂: 頂上が水面の高さ(4か5)で、まわり±2マス(上下±1)に水がある土/草。
    -- 高さ4は真上が水なので必ず砂、高さ5はまわりに高さ4以下の柱があれば砂
    local sand = {}
    for x = 0, N - 1 do
        for z = 0, N - 1 do
            local i = x * N + z + 1
            local h = byte(hmap, i)
            if h == WATER_LEVEL - 1 then
                sand[i] = true
            elseif h == WATER_LEVEL then
                for bx = math.max(0, x - 2), math.min(N - 1, x + 2) do
                    for bz = math.max(0, z - 2), math.min(N - 1, z + 2) do
                        if byte(hmap, bx * N + bz + 1) < WATER_LEVEL then sand[i] = true end
                    end
                end
            end
        end
    end
    -- 高さごとに1本の文字列を作る(石の10%は石炭/鉄鉱石)
    local random = math.random
    for y = 0, H - 1 do
        for x = 0, N - 1 do
            for z = 0, N - 1 do
                local i = x * N + z + 1
                local h = byte(hmap, i)
                local v
                if y == 0 then v = B.BEDROCK
                elseif y > h then v = (y <= WATER_LEVEL) and B.WATER or B.AIR
                elseif y == h and sand[i] then v = B.SAND
                elseif y == h and h >= WATER_LEVEL then v = B.GRASS
                elseif h > 3 and y <= h - 3 then
                    local r = random(0, 19)
                    v = (r == 0 and B.COAL_ORE) or (r == 1 and B.IRON_ORE) or B.STONE
                else v = B.DIRT end
                row[z + 1] = v
            end
            rows[x + 1] = char(table.unpack(row, 1, N))
        end
        L[y + 1] = table.concat(rows)
    end
    -- 木
    for _ = 1, 12 do
        local x = 2 + random(0, N - 6)
        local z = 2 + random(0, N - 6)
        local y = H - 8
        if get(x, y + 1, z) == B.AIR then
            while y > 0 and get(x, y, z) == B.AIR do y = y - 1 end
            if get(x, y, z) == B.GRASS then
                add_tree(x, y + 1, z)
                set(x, y, z, B.DIRT)
            end
        end
    end
    local px, pz = N // 2, N // 2
    return px, first_air(px, pz), pz
end

-- 平らなワールド: 草の床だけ
function M.gen_flat()
    M.clear()
    fill(0, 0, 0, N - 1, 0, N - 1, B.GRASS)
    return N // 2, 1, N // 2
end

-- デモのワールド: 家・東屋・ピラミッド・池・木
function M.gen_demo()
    M.clear()
    fill(0, 0, 0, N - 1, 0, N - 1, B.GRASS)
    fill(32, 1, 0, N - 1, 4, 16, B.WATER)
    fill(1, 1, N - 2, 1, H - 1, N - 2, B.BRICKS)
    add_tree(8, 1, 7)
    add_tree(23, 1, 15)
    add_tree(4, 1, 26)
    add_tree(16, 1, 23)
    add_tree(28, 1, 28)
    -- 家
    fill(18, 0, 3, 24, 0, 9, B.PLANKS)
    fill(18, 1, 3, 24, 1, 9, B.WATER)
    fill(25, 1, 3, 25, 3, 9, B.BRICKS)
    fill(18, 1, 10, 24, 3, 10, B.BRICKS)
    fill(17, 1, 3, 17, 1, 9, B.SAND)
    fill(17, 1, 10, 17, 3, 10, B.SLABS)
    fill(25, 1, 10, 25, 3, 10, B.SLABS)
    fill(25, 1, 2, 25, 3, 2, B.SLABS)
    set(18, 2, 3, B.TNT)
    set(19, 1, 3, B.BOOKS)
    set(24, 3, 5, B.COBBLE)
    -- 東屋
    fill(N - 4, 1, 3, N - 4, 3, 3, B.SLABS)
    fill(N - 10, 1, 3, N - 10, 3, 3, B.SLABS)
    fill(N - 4, 1, 9, N - 4, 3, 9, B.SLABS)
    fill(N - 10, 1, 9, N - 10, 3, 9, B.SLABS)
    fill(N - 10, 4, 3, N - 4, 4, 9, B.SLABS)
    fill(N - 9, 4, 4, N - 8, 4, 5, B.WATER)
    fill(N - 6, 4, 4, N - 5, 4, 5, B.WATER)
    fill(N - 9, 4, 7, N - 8, 4, 8, B.WATER)
    fill(N - 6, 4, 7, N - 5, 4, 8, B.WATER)
    -- ピラミッド
    for i = 1, 8 do
        fill(N - 1 - i, 9 - i, N - 1 - i, N - 1, 9 - i, N - 1, B.GOLD)
    end
    return 0, 1, 0
end

-- ---------------------------------------------------------------- 保存
-- 形式: "BLK1" N H px py pz 選んでいるブロック(各1バイト) の10バイトの後に、高さ0から順に N*N バイトずつ。

local MAGIC = "BLK1"

function M.save(path, p, cur)
    if not pico.sd_write(path, MAGIC .. char(N, H, p.x, p.y, p.z, cur)) then return false end
    for y = 1, H do
        if not pico.sd_write(path, L[y], true) then return false end
    end
    return true
end

-- 読めたら プレイヤーの位置(テーブル)と選んでいたブロックを返す。読めなければ nil, 理由
function M.load(path)
    local head = pico.sd_read_part(path, 0, 10)
    if not head or #head < 10 or sub(head, 1, 4) ~= MAGIC then return nil, "形式が違います" end
    local n, h, px, py, pz, cur = byte(head, 5, 10)
    if n ~= N or h ~= H then return nil, "大きさが違います" end
    for y = 1, H do
        local s = pico.sd_read_part(path, 10 + (y - 1) * NN, NN)
        if not s or #s ~= NN then M.clear(); return nil, "途中で切れています" end
        if s:find("[\25-\255]") then M.clear(); return nil, "壊れています" end
        L[y] = (s == AIR_LAYER) and AIR_LAYER or s
    end
    if cur < 1 or cur > M.COUNT then cur = B.STONE end
    return { x = math.min(px, N - 1), y = math.min(py, H - 1), z = math.min(pz, N - 1) }, cur
end

return M
