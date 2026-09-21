-- マインスイーパー(Minesweeper)。9x9マス・地雷10個の初級相当。
-- src/lua/LuaAppScanner.cppがこのディレクトリを走査してランチャへ登録するので
-- App_List.cppには一切手を加えていない。
--
-- タップ位置の座標をLuaへ渡すpico.* APIは無い(pico.onのコールバックは
-- WidgetIdしか受け取らない)ため、マス目1つ1つを別々のButtonウィジェットにして
-- 「どのボタンが押されたか」でどのマスかを判定する構成にした。81個のButtonは
-- OSヒープを食う(1個あたり約300B強)が、CLAUDE.mdの実測(Markdownシーン単体で
-- 40KB超)と比べても妥当な範囲。
--
-- Button::getLocalRect()は文字まわりの余白(TEXT_SPACING)ぶんl_rectより
-- 右・下に大きく描画/当たり判定される。GridContainerのgapをその分より
-- 小さく詰めると隣のマスの見た目が欠けて見えることがあるため、gap=2で
-- 実際に描画/タップして確認しながら調整した値を使っている。

local ROWS, COLS = 9, 9
local MINES = 10
local CELL = 22
local GAP = 2

local x, y, w, h = pico.content_rect()
local margin = 6

-- ゲーム状態(1始まりの2次元配列)
local mine = {}
local revealed = {}
local flagged = {}
local adjacent = {}
local buttons = {}

local started = false -- 最初の一手でここから地雷を配置する(最初のマスは必ず安全)
local game_over = false
local win = false
local flag_mode = false
local elapsed_ms = 0

for r = 1, ROWS do
    mine[r] = {}
    revealed[r] = {}
    flagged[r] = {}
    adjacent[r] = {}
    for c = 1, COLS do
        mine[r][c] = false
        revealed[r][c] = false
        flagged[r][c] = false
        adjacent[r][c] = 0
    end
end

-- ヘッダー: 状態表示 + 操作ボタン
local status_label = pico.create("Label")
pico.set(status_label, "x", x + margin)
pico.set(status_label, "y", y + margin)
pico.set(status_label, "font_size", 0)
pico.set(status_label, "text", "残り地雷: " .. MINES)

local button_row_y = y + margin + 20

local back_button = pico.create("Button")
pico.set(back_button, "x", x + margin)
pico.set(back_button, "y", button_row_y)
pico.set(back_button, "w", 40)
pico.set(back_button, "h", 22)
pico.set(back_button, "font_size", 0)
pico.set(back_button, "text", "戻る")
pico.on(back_button, "press_start", function()
    pico.pop()
end)

local reset_button = pico.create("Button")
pico.set(reset_button, "x", x + margin + 44)
pico.set(reset_button, "y", button_row_y)
pico.set(reset_button, "w", 54)
pico.set(reset_button, "h", 22)
pico.set(reset_button, "font_size", 0)
pico.set(reset_button, "text", "リセット")

local flag_button = pico.create("Button")
pico.set(flag_button, "x", x + margin + 44 + 58)
pico.set(flag_button, "y", button_row_y)
pico.set(flag_button, "w", 66)
pico.set(flag_button, "h", 22)
pico.set(flag_button, "font_size", 0)
pico.set(flag_button, "text", "旗:OFF")
pico.on(flag_button, "press_start", function()
    flag_mode = not flag_mode
    pico.set(flag_button, "text", flag_mode and "旗:ON" or "旗:OFF")
end)

-- グリッド
local grid_w = COLS * CELL + (COLS - 1) * GAP
local grid_h = ROWS * CELL + (ROWS - 1) * GAP
local grid_x = x + math.floor((w - grid_w) / 2)
local grid_y = button_row_y + 22 + 8

local COLOR_UNREVEALED = 7  -- PICO_LIGHTGREY
local COLOR_REVEALED = 15   -- PICO_WHITE
local COLOR_FLAGGED = 14    -- PICO_YELLOW
local COLOR_MINE_HIT = 12   -- PICO_RED
local COLOR_TEXT_FLAG = 12  -- PICO_RED
local COLOR_TEXT_MINE = 0   -- PICO_BLACK

-- 隣接数ごとの文字色(定番配色)
local NUM_COLORS = {
    [1] = 9,  -- BLUE
    [2] = 10, -- GREEN
    [3] = 12, -- RED
    [4] = 5,  -- PURPLE
    [5] = 4,  -- MAROON
    [6] = 11, -- CYAN
    [7] = 0,  -- BLACK
    [8] = 8,  -- DARKGREY
}

local function neighbors(r, c)
    local list = {}
    for dr = -1, 1 do
        for dc = -1, 1 do
            if not (dr == 0 and dc == 0) then
                local nr, nc = r + dr, c + dc
                if nr >= 1 and nr <= ROWS and nc >= 1 and nc <= COLS then
                    list[#list + 1] = { nr, nc }
                end
            end
        end
    end
    return list
end

local function setStatusLabel()
    if game_over then
        pico.set(status_label, "text", win and "クリア!" or "GAME OVER")
        return
    end
    local flagged_count = 0
    for r = 1, ROWS do
        for c = 1, COLS do
            if flagged[r][c] then flagged_count = flagged_count + 1 end
        end
    end
    pico.set(status_label, "text", "残り地雷: " .. (MINES - flagged_count))
end

local function drawCell(r, c)
    local id = buttons[r][c]
    if flagged[r][c] and not revealed[r][c] then
        pico.set(id, "background_color", COLOR_FLAGGED)
        pico.set(id, "text_color", COLOR_TEXT_FLAG)
        pico.set(id, "text", "F")
        return
    end
    if not revealed[r][c] then
        pico.set(id, "background_color", COLOR_UNREVEALED)
        pico.set(id, "text", "")
        return
    end
    if mine[r][c] then
        pico.set(id, "background_color", COLOR_MINE_HIT)
        pico.set(id, "text_color", COLOR_TEXT_MINE)
        pico.set(id, "text", "*")
        return
    end
    pico.set(id, "background_color", COLOR_REVEALED)
    local n = adjacent[r][c]
    if n == 0 then
        pico.set(id, "text", "")
    else
        pico.set(id, "text_color", NUM_COLORS[n] or 0)
        pico.set(id, "text", tostring(n))
    end
end

-- 最初にタップしたマス(safe_r, safe_c)を避けて地雷を配置する
local function placeMines(safe_r, safe_c)
    local placed = 0
    while placed < MINES do
        local r = math.random(1, ROWS)
        local c = math.random(1, COLS)
        if not mine[r][c] and not (r == safe_r and c == safe_c) then
            mine[r][c] = true
            placed = placed + 1
        end
    end
    for r = 1, ROWS do
        for c = 1, COLS do
            if not mine[r][c] then
                local count = 0
                for _, n in ipairs(neighbors(r, c)) do
                    if mine[n[1]][n[2]] then count = count + 1 end
                end
                adjacent[r][c] = count
            end
        end
    end
end

local function revealAllMines()
    for r = 1, ROWS do
        for c = 1, COLS do
            if mine[r][c] then
                revealed[r][c] = true
                drawCell(r, c)
            end
        end
    end
end

local function checkWin()
    local revealed_count = 0
    for r = 1, ROWS do
        for c = 1, COLS do
            if revealed[r][c] then revealed_count = revealed_count + 1 end
        end
    end
    if revealed_count ~= ROWS * COLS - MINES then return end

    game_over = true
    win = true
    -- 地雷マスへ自動で旗を立てて見せる
    for r = 1, ROWS do
        for c = 1, COLS do
            if mine[r][c] and not flagged[r][c] then
                flagged[r][c] = true
                drawCell(r, c)
            end
        end
    end
    setStatusLabel()
end

-- 0マスは繋がっている限り自動で開く(盤面は9x9=81マスなので再帰深さの心配は無い)
local function floodReveal(r, c)
    if revealed[r][c] or flagged[r][c] then return end
    revealed[r][c] = true
    drawCell(r, c)
    if adjacent[r][c] == 0 then
        for _, n in ipairs(neighbors(r, c)) do
            floodReveal(n[1], n[2])
        end
    end
end

local function onReveal(r, c)
    if game_over or flagged[r][c] or revealed[r][c] then return end

    if not started then
        started = true
        -- NTP同期前でもpico.get_time()は値を返す(妥当性は保証されないが、
        -- loop()で積んだelapsed_msと組み合わせれば毎回違う種になる)
        local t = pico.get_time()
        math.randomseed(elapsed_ms + t.sec * 1000 + t.min * 60000 + t.hour * 3600000)
        placeMines(r, c)
    end

    if mine[r][c] then
        revealed[r][c] = true
        game_over = true
        win = false
        drawCell(r, c)
        revealAllMines()
        setStatusLabel()
        return
    end

    floodReveal(r, c)
    checkWin()
    if not game_over then setStatusLabel() end
end

local function onFlag(r, c)
    if game_over or revealed[r][c] then return end
    flagged[r][c] = not flagged[r][c]
    drawCell(r, c)
    setStatusLabel()
end

for r = 1, ROWS do
    buttons[r] = {}
    for c = 1, COLS do
        local btn = pico.create("Button")
        pico.set(btn, "w", CELL)
        pico.set(btn, "h", CELL)
        pico.set(btn, "font_size", 0)
        pico.set(btn, "background_color", COLOR_UNREVEALED)
        buttons[r][c] = btn
        pico.on(btn, "press_start", function()
            if flag_mode then
                onFlag(r, c)
            else
                onReveal(r, c)
            end
        end)
    end
end

-- GridContainerへ行優先(add_childした順)で流し込む。1行9列固定
local grid = pico.create("GridContainer")
pico.set(grid, "x", grid_x)
pico.set(grid, "y", grid_y)
pico.set(grid, "w", grid_w)
pico.set(grid, "h", grid_h)
pico.set(grid, "cols", COLS)
pico.set(grid, "gap", GAP)
for r = 1, ROWS do
    for c = 1, COLS do
        pico.add_child(grid, buttons[r][c])
    end
end

local function resetGame()
    started = false
    game_over = false
    win = false
    for r = 1, ROWS do
        for c = 1, COLS do
            mine[r][c] = false
            revealed[r][c] = false
            flagged[r][c] = false
            adjacent[r][c] = 0
            drawCell(r, c)
        end
    end
    setStatusLabel()
end

pico.on(reset_button, "press_start", function()
    resetGame()
end)

function loop(dt)
    elapsed_ms = elapsed_ms + dt
end
