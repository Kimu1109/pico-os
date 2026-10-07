-- ブロック: ワールドのデータ・生成・保存(チャンク読み込み)
-- 元: TheScienceElf/Blocks-TI-84 の world / worldgen / world_io(MIT)
--
-- 世界は 1辺 W = K*8 マス(新しく作るワールドは K=128 で 1024 x 16 x 1024)。全部は持てないので、
-- 8x8 の柱(高さ16)を1つの「チャンク」にし、見えている所のまわりのチャンクだけを読み込んで持つ。
-- チャンクは文字列1本(C[cx*K+cz])で、(lx, y, lz) のブロック番号は y*64 + lx*8 + lz + 1 バイト目。
-- 上の空気だけの段は持たない(短い文字列で、範囲の外は空気)。読み込んでいないチャンクは空気として扱う。
-- 地形は種(seed)と位置だけで決まる(同じ種ならいつ作っても同じチャンクになる)ので、ファイルに書くのは
-- 書き換えたチャンクだけ。書き換えたチャンクは読み込みの範囲から外れるとき・終了するときに書き出す。
--
-- ファイル: <スロット>/world.dat = "BLK2" 種類(1) K(2) 種(4) カーソルx(2) y(1) z(2) 選んでいるブロック(1)
--   の17バイト + 書き出したチャンクの印(K*K ビット)。<スロット>/c_<cx>_<cz>.dat = チャンクの文字列そのまま。
local M = {}

local H = 16
M.H = H

local B = {
    AIR = 0, WATER = 1, STONE = 2, GRASS = 3, DIRT = 4, COBBLE = 5, PLANKS = 6, BRICKS = 7,
    SLABS = 8, WOOD = 9, LEAVES = 10, SAND = 11, BOOKS = 12, TNT = 13, CRAFTING = 14,
    FURNACE = 15, JUKEBOX = 16, SPONGE = 17, GRAVEL = 18, MOSS = 19, COAL_ORE = 20,
    IRON_ORE = 21, BEDROCK = 22, IRON = 23, GOLD = 24,
}
M.B = B
M.COUNT = 24          -- ブロックの種類(水を含む)
M.KINDS = { [0] = "自然", "平ら", "デモ", "空" }
M.NEW_K = 128          -- 新しく作るワールドの1辺のチャンク数(1024マス)

local byte, char, sub, rep = string.byte, string.char, string.sub, string.rep
local WATER_LEVEL = 5
local AIR64 = rep("\0", 64)
local MAGIC = "BLK2"
local HEAD = 17

-- 今のワールド
local K, W = 6, 48
local C = {}          -- 読み込んだチャンク(キー cx*K+cz → 文字列)
local D = {}          -- 書き換えた(まだ書き出していない)チャンク
local kind, seed = 3, 0
local saved = ""      -- 書き出したチャンクの印
local dir = nil       -- スロットのディレクトリ
local queue, qpos = {}, 1
M.C = C

local function sync()
    M.K, M.W, M.kind = K, W, kind
end
sync()

local function chunk_path(cx, cz) return dir .. "/c_" .. cx .. "_" .. cz .. ".dat" end

local function is_saved(n)
    local v = byte(saved, n // 8 + 1) or 0
    return (v >> (n % 8)) & 1 == 1
end

local function mark_saved(n)
    local i = n // 8 + 1
    saved = sub(saved, 1, i - 1) .. char(byte(saved, i) | (1 << (n % 8))) .. sub(saved, i + 1)
end

-- ---------------------------------------------------------------- 読み書き

-- (x, y, z) のブロック。世界の外と読み込んでいないチャンクは空気
local function get(x, y, z)
    if x < 0 or z < 0 or y < 0 or x >= W or z >= W then return 0 end
    local c = C[(x >> 3) * K + (z >> 3)]
    if not c then return 0 end
    return byte(c, y * 64 + (x & 7) * 8 + (z & 7) + 1) or 0
end
M.get = get

-- チャンク(cx, cz) の文字列(読み込んでいなければ nil、世界の外も nil)
function M.chunk(cx, cz)
    if cx < 0 or cz < 0 or cx >= K or cz >= K then return nil end
    return C[cx * K + cz]
end

-- ---------------------------------------------------------------- 地形の生成

-- 種と整数3つから決まる 0〜2^32-1 の値
local function hash(a, b, c)
    local h = (a * 73856093 ~ b * 19349663 ~ c * 83492791 ~ seed) & 0xffffffff
    h = ((h ~ (h >> 15)) * 0x2c1b3c6d) & 0xffffffff
    h = ((h ~ (h >> 12)) * 0x297a2d39) & 0xffffffff
    return h ~ (h >> 15)
end
M.hash = hash

-- 元と同じ: 8マスごとの格子に高さ(3〜12)を決め、間は双一次補間
local function grid(gx, gz) return 3 + hash(gx, gz, 0) % 10 end
local function height(x, z)
    local gx, gz, lx, lz = x // 8, z // 8, x % 8, z % 8
    return (grid(gx, gz) * (8 - lx) * (8 - lz) + grid(gx + 1, gz) * lx * (8 - lz)
          + grid(gx, gz + 1) * (8 - lx) * lz + grid(gx + 1, gz + 1) * lx * lz) // 64
end
M.height = height

-- 両端を含む直方体を、チャンク(原点 ox, oz)の段の文字列 lay へ塗る(チャンクの外は切り捨てる)
local function paint(lay, ox, oz, x0, y0, z0, x1, y1, z1, b)
    if x0 < ox then x0 = ox end
    if z0 < oz then z0 = oz end
    if x1 > ox + 7 then x1 = ox + 7 end
    if z1 > oz + 7 then z1 = oz + 7 end
    if y0 < 0 then y0 = 0 end
    if y1 > H - 1 then y1 = H - 1 end
    if x0 > x1 or z0 > z1 then return end
    local run = rep(char(b), z1 - z0 + 1)
    for y = y0, y1 do
        local s = lay[y + 1]
        for x = x0, x1 do
            local i = (x - ox) * 8 + (z0 - oz) + 1
            s = sub(s, 1, i - 1) .. run .. sub(s, i + #run)
        end
        lay[y + 1] = s
    end
end

-- (x, y, z) を根元にした木を ops(直方体の並び)へ足す
local function tree_ops(ops, x, y, z)
    local L, WD = B.LEAVES, B.WOOD
    ops[#ops + 1] = { x - 2, y + 3, z - 2, x + 2, y + 4, z + 2, L }
    ops[#ops + 1] = { x - 1, y + 5, z - 1, x + 1, y + 5, z + 1, L }
    ops[#ops + 1] = { x + 1, y + 6, z, x + 1, y + 6, z, L }
    ops[#ops + 1] = { x - 1, y + 6, z, x - 1, y + 6, z, L }
    ops[#ops + 1] = { x, y + 6, z + 1, x, y + 6, z + 1, L }
    ops[#ops + 1] = { x, y + 6, z - 1, x, y + 6, z - 1, L }
    ops[#ops + 1] = { x, y + 6, z, x, y + 6, z, L }
    ops[#ops + 1] = { x, y, z, x, y + 5, z, WD }
end

-- ops の1つは {x0, y0, z0, x1, y1, z1, b} か、それを7バイトにした文字列(demo.lua)
local function apply_ops(lay, ox, oz, ops)
    for _, o in ipairs(ops) do
        local x0, y0, z0, x1, y1, z1, b
        if type(o) == "string" then x0, y0, z0, x1, y1, z1, b = byte(o, 1, 7)
        else x0, y0, z0, x1, y1, z1, b = o[1], o[2], o[3], o[4], o[5], o[6], o[7] end
        if x1 >= ox and x0 <= ox + 7 and z1 >= oz and z0 <= oz + 7 then
            paint(lay, ox, oz, x0, y0, z0, x1, y1, z1, b)
        end
    end
end

-- 段の文字列を、上の空気だけの段を落として1本にする
local function join(lay)
    local t = H
    while t > 0 and lay[t] == AIR64 do t = t - 1 end
    return table.concat(lay, "", 1, t)
end

-- 自然: 水面(5)より低い所は水、水辺は砂、石の1割は石炭/鉄鉱石、チャンク3つに1本くらい木
local hs, sd, row = {}, {}, {}
local function gen_natural(cx, cz)
    local ox, oz = cx * 8, cz * 8
    -- まわり2マスまでの高さ(砂の判定に使う)
    local hh = {}
    for x = -2, 9 do
        for z = -2, 9 do hh[(x + 2) * 12 + z + 3] = height(ox + x, oz + z) end
    end
    local top = WATER_LEVEL
    for lx = 0, 7 do
        for lz = 0, 7 do
            local h = hh[(lx + 2) * 12 + lz + 3]
            local s = 0
            if h == WATER_LEVEL - 1 then s = 1
            elseif h == WATER_LEVEL then
                for bx = lx, lx + 4 do
                    for bz = lz, lz + 4 do
                        if hh[bx * 12 + bz + 1] < WATER_LEVEL then s = 1 end
                    end
                end
            end
            local i = lx * 8 + lz + 1
            hs[i], sd[i] = h, s
            if h > top then top = h end
        end
    end
    local lay = {}
    for y = 0, H - 1 do
        if y > top then lay[y + 1] = AIR64
        else
            for lx = 0, 7 do
                for lz = 0, 7 do
                    local i = lx * 8 + lz + 1
                    local h = hs[i]
                    local v
                    if y == 0 then v = B.BEDROCK
                    elseif y > h then v = (y <= WATER_LEVEL) and B.WATER or B.AIR
                    elseif y == h and sd[i] == 1 then v = B.SAND
                    elseif y == h and h >= WATER_LEVEL then v = B.GRASS
                    elseif h > 3 and y <= h - 3 then
                        local r = hash(ox + lx, y, oz + lz) % 20
                        v = (r == 0 and B.COAL_ORE) or (r == 1 and B.IRON_ORE) or B.STONE
                    else v = B.DIRT end
                    row[i] = v
                end
            end
            lay[y + 1] = char(table.unpack(row, 1, 64))
        end
    end
    -- 木: 葉が隣のチャンクへはみ出さないよう、根元はチャンクの中の 2〜5
    local r = hash(cx, cz, 7)
    if r % 3 == 0 then
        local lx, lz = 2 + (r >> 4) % 4, 2 + (r >> 8) % 4
        local i = lx * 8 + lz + 1
        local h = hs[i]
        if sd[i] == 0 and h >= WATER_LEVEL and h <= H - 8 then
            local ops = {}
            tree_ops(ops, ox + lx, h + 1, oz + lz)
            ops[#ops + 1] = { ox + lx, h, oz + lz, ox + lx, h, oz + lz, B.DIRT }
            apply_ops(lay, ox, oz, ops)
        end
    end
    return join(lay)
end

-- デモの家などの並び(demo.lua。デモのワールドを開くときだけ読み込む。M.demo に入れる)
M.demo = nil
M.tree_ops = tree_ops

local GRASS64 = rep(char(B.GRASS), 64)

local function generate(cx, cz)
    if kind == 0 then return gen_natural(cx, cz) end
    if kind == 3 then return "" end
    if kind == 2 and cx < 6 and cz < 6 and M.demo then
        local lay = { GRASS64 }
        for y = 2, H do lay[y] = AIR64 end
        apply_ops(lay, cx * 8, cz * 8, M.demo)
        return join(lay)
    end
    return GRASS64
end
M.generate = generate

-- ---------------------------------------------------------------- チャンクの読み込み

local function valid(s)
    return s and #s % 64 == 0 and #s <= 64 * H and not s:find("[\25-\255]")
end

-- チャンクを読み込む(書き出したものはファイルから、無ければ生成)。文字列を返す
local function load_chunk(cx, cz)
    local n = cx * K + cz
    local c = C[n]
    if c then return c end
    if dir and is_saved(n) then
        c = pico.sd_read_part(chunk_path(cx, cz), 0, 64 * H)
        if not valid(c) then
            pico.log("ブロック: チャンク " .. cx .. "," .. cz .. " を読めません。作り直します")
            c = nil
        end
    end
    c = c or generate(cx, cz)
    C[n] = c
    return c
end
M.load_chunk = load_chunk

local function write_chunk(n)
    if not dir then return false end
    local cx, cz = n // K, n % K
    if not pico.sd_write(chunk_path(cx, cz), C[n]) then return false end
    D[n] = nil
    if not is_saved(n) then mark_saved(n) end
    return true
end

local function unload(n)
    if D[n] then write_chunk(n) end
    C[n] = nil
    D[n] = nil
end

function M.set(x, y, z, b)
    if x < 0 or z < 0 or y < 0 or x >= W or z >= W or y >= H then return end
    local cx, cz = x >> 3, z >> 3
    local n = cx * K + cz
    local c = load_chunk(cx, cz)
    local i = y * 64 + (x & 7) * 8 + (z & 7) + 1
    if #c < y * 64 + 64 then c = c .. rep("\0", y * 64 + 64 - #c) end
    c = sub(c, 1, i - 1) .. char(b) .. sub(c, i + 1)
    while #c > 0 and sub(c, -64) == AIR64 do c = sub(c, 1, #c - 64) end
    C[n] = c
    D[n] = true
end

-- 見えている範囲(u = x - z、s = x + z の範囲)にかかるチャンクを読み込む予定に入れ、
-- それより2チャンク以上離れたチャンクは手放す(1チャンクぶんは残して、行き来で読み直さない)(書き換えたものは書き出してから)
function M.window(umin, umax, smin, smax)
    local amin, amax = -((7 - umin) // 8), (umax + 7) // 8
    local bmin, bmax = -((14 - smin) // 8), smax // 8
    for n in pairs(C) do
        local cx, cz = n // K, n % K
        local a, b = cx - cz, cx + cz
        if a < amin - 1 or a > amax + 1 or b < bmin - 1 or b > bmax + 1 then unload(n) end
    end
    local q, dist = {}, {}
    local ac, bc = (amin + amax) / 2, (bmin + bmax) / 2
    for b = bmin, bmax do
        for a = amin, amax do
            if (a + b) % 2 == 0 then
                local cx, cz = (a + b) // 2, (b - a) // 2
                if cx >= 0 and cz >= 0 and cx < K and cz < K then
                    local n = cx * K + cz
                    if not C[n] then
                        q[#q + 1] = n
                        dist[n] = math.abs(a - ac) + math.abs(b - bc)
                    end
                end
            end
        end
    end
    table.sort(q, function(p, r) return dist[p] < dist[r] end)
    queue, qpos = q, 1
end

-- 予定のチャンクを最大 max 個読み込む(読み込んだ数を返す)。ms を過ぎたら途中でやめる
function M.pump(max, ms)
    local t0 = pico.millis()
    local done = 0
    while qpos <= #queue and done < max do
        local n = queue[qpos]
        qpos = qpos + 1
        if not C[n] then
            load_chunk(n // K, n % K)
            done = done + 1
            if ms and pico.millis() - t0 >= ms then break end
        end
    end
    return done
end

function M.pending() return #queue - qpos + 1 end

function M.loaded_count()
    local k, bytes = 0, 0
    for _, c in pairs(C) do k = k + 1; bytes = bytes + #c end
    return k, bytes
end

-- 読み込んだチャンクの中で、空気でない一番上の高さ
function M.top()
    local t = 0
    for _, c in pairs(C) do if #c > t then t = #c end end
    return t // 64 - 1
end

-- 下から見て最初の空気のマス
function M.first_air(x, z)
    load_chunk(x >> 3, z >> 3)
    local y = 0
    while y < H - 1 and get(x, y, z) ~= B.AIR do y = y + 1 end
    return y
end

-- ---------------------------------------------------------------- ワールドを開く・作る・保存する

function M.close()
    for n in pairs(C) do C[n] = nil end
    for n in pairs(D) do D[n] = nil end
    queue, qpos = {}, 1
    dir, saved, kind = nil, "", 3
    K, W = 6, 48
    sync()
end

local function begin(d, k, kk, sd0)
    M.close()
    dir, kind, K, W, seed = d, k, kk, kk * 8, sd0
    saved = rep("\0", (K * K + 7) // 8)
    sync()
end

-- 新しいワールド。始めのカーソルの位置を返す
function M.create(d, k, sd0, kk)
    begin(d, k, kk or M.NEW_K, sd0)
    if k == 2 then return 0, 1, 0 end
    local x, z = W // 2, W // 2
    return x, M.first_air(x, z), z
end

local function u16(v) return char(v >> 8 & 255, v & 255) end
local function r16(s, i) return byte(s, i) * 256 + byte(s, i + 1) end

-- 見出しだけ読む(ワールドを選ぶ画面に出す)。種類, 1辺のマス数 / 読めなければ nil
function M.info(path)
    local h = pico.sd_read_part(path, 0, HEAD)
    if not h or #h < HEAD or sub(h, 1, 4) ~= MAGIC then return nil end
    return byte(h, 5), r16(h, 6) * 8
end

-- 開く。読めたら カーソルの位置(テーブル)と選んでいたブロック、読めなければ nil, 理由
function M.open(d)
    local path = d .. "/world.dat"
    local h = pico.sd_read_part(path, 0, HEAD)
    if not h or #h < HEAD or sub(h, 1, 4) ~= MAGIC then return nil, "形式が違います" end
    local k, kk = byte(h, 5), r16(h, 6)
    if k > 3 or kk < 1 or kk > 1024 then return nil, "壊れています" end
    local sd0 = ((byte(h, 8) << 24) | (byte(h, 9) << 16) | (byte(h, 10) << 8) | byte(h, 11))
    local px, py, pz, cur = r16(h, 12), byte(h, 14), r16(h, 15), byte(h, 17)
    local bits = pico.sd_read_part(path, HEAD, (kk * kk + 7) // 8)
    if not bits or #bits ~= (kk * kk + 7) // 8 then return nil, "途中で切れています" end
    begin(d, k, kk, sd0)
    saved = bits
    if cur < 1 or cur > M.COUNT then cur = B.STONE end
    return { x = math.min(px, W - 1), y = math.min(py, H - 1), z = math.min(pz, W - 1) }, cur
end

-- 書き換えたチャンクと見出しを書き出す
function M.save(p, cur)
    if not dir then return false end
    local ok = true
    for n in pairs(D) do
        if not write_chunk(n) then ok = false end
    end
    local head = MAGIC .. char(kind) .. u16(K)
        .. char(seed >> 24 & 255, seed >> 16 & 255, seed >> 8 & 255, seed & 255)
        .. u16(p.x) .. char(p.y) .. u16(p.z) .. char(cur)
    if not pico.sd_write(dir .. "/world.dat", head .. saved) then ok = false end
    return ok
end

-- 書き換えたチャンクの集まり(キー cx*kk+cz → 文字列)をそのまま持つワールドにする(migrate.lua が使う)
function M.adopt(d, kk, chunks)
    begin(d, 3, kk, 0)
    for n, c in pairs(chunks) do C[n], D[n] = c, true end
end

return M
