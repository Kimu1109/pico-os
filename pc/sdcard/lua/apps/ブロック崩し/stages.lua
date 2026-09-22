-- ブロック崩しのステージ定義。main.lua単体でスクリプト読み込み上限(16KiB)へ
-- 収めるため、ステージ生成ロジックをここへ分離した(main.luaがpico.sd_read+load()で
-- 実行時に読み込む)。20ステージぶんのマス目を生の配列で持つ代わりに、形状ごとの
-- 生成関数(r,c)->tierを並べているので、この程度の行数で収まる。

local COLS = 8

-- ブロックの色(=速度)は行番号から1〜6を巡回させる(元祖のブロック崩しの
-- 「上の段ほど速い」に寄せた配色。tierの意味はmain.lua側のTIER_COLOR/TIER_MULTを参照)
local function rowTier(r) return ((r - 1) % 6) + 1 end

local function stage(rows, cell) return { rows = rows, cell = cell } end

local function full(rows)
    return stage(rows, function(r, c) return rowTier(r) end)
end

local function checker(rows, odd)
    local m = odd and 1 or 0
    return stage(rows, function(r, c)
        if (r + c) % 2 == m then return rowTier(r) end
        return 0
    end)
end

local function border(rows)
    return stage(rows, function(r, c)
        if r == 1 or r == rows or c == 1 or c == COLS then return rowTier(r) end
        return 0
    end)
end

local function stripes(rows, gap)
    return stage(rows, function(r, c)
        if c % gap ~= 0 then return rowTier(r) end
        return 0
    end)
end

local function pyramid(rows)
    return stage(rows, function(r, c)
        local half = (COLS - r) / 2
        if c > half and c <= COLS - half then return rowTier(r) end
        return 0
    end)
end

local function invPyramid(rows)
    return stage(rows, function(r, c)
        local half = (r - 1) / 2
        if c > half and c <= COLS - half then return rowTier(r) end
        return 0
    end)
end

local function columns2(rows)
    return stage(rows, function(r, c)
        if c <= 3 or c >= COLS - 2 then return rowTier(r) end
        return 0
    end)
end

local function cross(rows)
    local midr, midc = math.ceil(rows / 2), math.ceil(COLS / 2)
    return stage(rows, function(r, c)
        if c == midc or (r >= midr - 1 and r <= midr + 1) then return rowTier(r) end
        return 0
    end)
end

local function diamond(rows)
    local cy, cx = (rows + 1) / 2, (COLS + 1) / 2
    local rad = math.min(cx, cy)
    return stage(rows, function(r, c)
        local d = math.abs(r - cy) + math.abs(c - cx) * (cy / cx)
        if d <= rad then return rowTier(r) end
        return 0
    end)
end

local function rings(rows)
    local cy, cx = (rows + 1) / 2, (COLS + 1) / 2
    return stage(rows, function(r, c)
        local d = math.floor(math.max(math.abs(r - cy), math.abs(c - cx)))
        if d % 2 == 0 then return rowTier(r) end
        return 0
    end)
end

-- 決定論的な「穴あきレンガ壁」。同じstage()呼び出しは何度も起きないので
-- randomseedを呼びっぱなしにして問題ない
local function randomHoles(rows, seed, density)
    math.randomseed(seed)
    local grid = {}
    for r = 1, rows do
        grid[r] = {}
        for c = 1, COLS do
            grid[r][c] = (math.random() < density) and rowTier(r) or 0
        end
    end
    return stage(rows, function(r, c) return grid[r][c] end)
end

return {
    cols = COLS,
    list = {
        full(3),
        checker(4, false),
        border(4),
        stripes(4, 3),
        full(4),
        pyramid(5),
        invPyramid(5),
        columns2(5),
        checker(5, true),
        cross(5),
        border(6),
        stripes(6, 2),
        diamond(6),
        randomHoles(6, 1001, 0.8),
        pyramid(7),
        rings(7),
        checker(7, false),
        randomHoles(7, 2002, 0.85),
        diamond(8),
        randomHoles(8, 3003, 0.95),
    },
}
