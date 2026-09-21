-- オセロ(リバーシ)。2人交互プレイのシンプル版。
-- src/lua/LuaAppScanner.cppがこのディレクトリを走査してランチャへ登録するので
-- App_List.cppには一切手を加えていない。
--
-- 盤面はButtonではなく、1枚の"Canvas"(pico.create("Canvas"))へpico.draw_*で
-- 自前描画している。タップ判定は"press_start"の中でpico.get_touch()が返す
-- 絶対スクリーン座標から、Canvas自身の左上位置(board_x/board_y)を引いて
-- マス目を逆算する(lua-api-doc「Canvasと直接描画」の「タップ位置の取得」参照)。
--
-- (旧版はマス目の数(8x8=64個)だけ小さなCanvasを敷き詰め、それぞれの
-- press_startで判定していた。pico.get_touch()が無かった頃の名残りで、
-- タップ座標を直接読めるようになった今は不要な遠回りだったため統合した)

local SIZE = 8
local CELL = 24
local BOARD_COLOR = 2   -- PICO_DARKGREEN
local LINE_COLOR = 0    -- PICO_BLACK
local BLACK_COLOR = 0
local WHITE_COLOR = 15
local HINT_COLOR = 7    -- PICO_LIGHTGREY

local DIRECTIONS = {
    { -1, -1 }, { -1, 0 }, { -1, 1 },
    { 0, -1 },             { 0, 1 },
    { 1, -1 },  { 1, 0 },  { 1, 1 },
}

local x, y, w, h = pico.content_rect()
local margin = 8

-- ゲーム状態(1始まりの2次元配列。0=空, 1=黒, 2=白)
local board = {}
local valid = {}
local current = 1
local game_over = false

for r = 1, SIZE do
    board[r] = {}
    valid[r] = {}
    for c = 1, SIZE do
        board[r][c] = 0
        valid[r][c] = false
    end
end

local function opponentOf(p)
    return p == 1 and 2 or 1
end

local function playerName(p)
    return p == 1 and "黒" or "白"
end

local function countStones()
    local b, wcount = 0, 0
    for r = 1, SIZE do
        for c = 1, SIZE do
            if board[r][c] == 1 then
                b = b + 1
            elseif board[r][c] == 2 then
                wcount = wcount + 1
            end
        end
    end
    return b, wcount
end

-- (r,c)へplayerが打った場合に裏返る石の一覧を返す(空なら着手不可)
local function flipsForMove(r, c, player)
    if board[r][c] ~= 0 then return {} end
    local opp = opponentOf(player)
    local flips = {}
    for _, d in ipairs(DIRECTIONS) do
        local dr, dc = d[1], d[2]
        local rr, cc = r + dr, c + dc
        local line = {}
        while rr >= 1 and rr <= SIZE and cc >= 1 and cc <= SIZE and board[rr][cc] == opp do
            line[#line + 1] = { rr, cc }
            rr = rr + dr
            cc = cc + dc
        end
        if #line > 0 and rr >= 1 and rr <= SIZE and cc >= 1 and cc <= SIZE and board[rr][cc] == player then
            for _, p in ipairs(line) do
                flips[#flips + 1] = p
            end
        end
    end
    return flips
end

local function computeValidMoves(player)
    local vm = {}
    local count = 0
    for r = 1, SIZE do
        vm[r] = {}
        for c = 1, SIZE do
            if board[r][c] == 0 and #flipsForMove(r, c, player) > 0 then
                vm[r][c] = true
                count = count + 1
            else
                vm[r][c] = false
            end
        end
    end
    return vm, count
end

local function applyMove(r, c, player)
    -- flipsForMove()はboard[r][c]==0(空マス)を前提に着手可否を判定するため、
    -- 石を置く前に裏返す一覧を確定させる(先に置いてしまうと常に空リストが
    -- 返り、新しい石を置くだけで一切裏返らなくなるバグを踏んだ経緯がある)。
    local flips = flipsForMove(r, c, player)
    board[r][c] = player
    for _, p in ipairs(flips) do
        board[p[1]][p[2]] = player
    end
end

-- 前方宣言(相互参照のため)
local updateStatus, redrawBoard, showResult, resolveTurn

local status_label
local board_canvas

function updateStatus()
    local b, wcount = countStones()
    if game_over then
        pico.set(status_label, "text", "終了  黒:" .. b .. " 白:" .. wcount)
    else
        pico.set(status_label, "text", playerName(current) .. "の番  黒:" .. b .. " 白:" .. wcount)
    end
end

function redrawBoard()
    pico.invalidate(board_canvas)
end

function showResult()
    local b, wcount = countStones()
    local msg
    if b > wcount then
        msg = "黒の勝ち! (黒" .. b .. " - 白" .. wcount .. ")"
    elseif wcount > b then
        msg = "白の勝ち! (黒" .. b .. " - 白" .. wcount .. ")"
    else
        msg = "引き分け (黒" .. b .. " - 白" .. wcount .. ")"
    end
    pico.show_message(msg, "", "OK")
end

-- 着手後の手番交代。両者とも置けない場合は終局、片方だけ置けない場合は
-- メッセージダイアログでパスを知らせてから戻す。
function resolveTurn()
    current = opponentOf(current)
    local next_valid, next_count = computeValidMoves(current)

    if next_count > 0 then
        valid = next_valid
        updateStatus()
        return
    end

    local other = opponentOf(current)
    local other_valid, other_count = computeValidMoves(other)

    if other_count == 0 then
        game_over = true
        valid = next_valid -- 全マスfalse
        updateStatus()
        redrawBoard()
        showResult()
        return
    end

    -- currentは打てないのでパス。ダイアログを閉じたらotherへ戻す。
    valid = next_valid -- 全マスfalse(パス中はヒントを出さない)
    updateStatus()
    redrawBoard()
    local dlg = pico.show_message(playerName(current) .. "は置ける場所が無いためパスします", "", "OK")
    pico.on(dlg, "closed", function()
        current = other
        valid = other_valid
        updateStatus()
        redrawBoard()
    end)
end

local function onCellTap(r, c)
    if game_over or not valid[r][c] then return end
    applyMove(r, c, current)
    resolveTurn()
    redrawBoard()
end

-- ヘッダー
local title_label = pico.create("Label")
pico.set(title_label, "x", x + margin)
pico.set(title_label, "y", y + margin)
pico.set(title_label, "font_size", 1)
pico.set(title_label, "text", "リバーシ")

status_label = pico.create("Label")
pico.set(status_label, "x", x + margin)
pico.set(status_label, "y", y + margin + 26)
pico.set(status_label, "font_size", 0)

local button_row_y = y + margin + 46

local back_button = pico.create("Button")
pico.set(back_button, "x", x + margin)
pico.set(back_button, "y", button_row_y)
pico.set(back_button, "w", 50)
pico.set(back_button, "h", 22)
pico.set(back_button, "font_size", 0)
pico.set(back_button, "text", "戻る")
pico.on(back_button, "press_start", function()
    pico.pop()
end)

local reset_button = pico.create("Button")
pico.set(reset_button, "x", x + margin + 54)
pico.set(reset_button, "y", button_row_y)
pico.set(reset_button, "w", 70)
pico.set(reset_button, "h", 22)
pico.set(reset_button, "font_size", 0)
pico.set(reset_button, "text", "リセット")

-- 盤面: 1枚のCanvasにSIZE*SIZEマスすべてを自前描画する。
local board_w = SIZE * CELL
local board_x = x + math.floor((w - board_w) / 2)
local board_y = button_row_y + 22 + 8

board_canvas = pico.create("Canvas")
pico.set(board_canvas, "x", board_x)
pico.set(board_canvas, "y", board_y)
pico.set(board_canvas, "w", board_w)
pico.set(board_canvas, "h", board_w)

pico.on(board_canvas, "render", function()
    for r = 1, SIZE do
        for c = 1, SIZE do
            local cx = board_x + (c - 1) * CELL
            local cy = board_y + (r - 1) * CELL
            pico.fill_rect(cx, cy, CELL, CELL, BOARD_COLOR)
            pico.draw_rect(cx, cy, CELL, CELL, LINE_COLOR)

            local center_x = cx + math.floor(CELL / 2)
            local center_y = cy + math.floor(CELL / 2)
            local v = board[r][c]
            if v == 1 then
                pico.fill_circle(center_x, center_y, 9, BLACK_COLOR)
            elseif v == 2 then
                pico.fill_circle(center_x, center_y, 9, WHITE_COLOR)
                pico.draw_circle(center_x, center_y, 9, LINE_COLOR)
            elseif valid[r][c] then
                pico.fill_circle(center_x, center_y, 2, HINT_COLOR)
            end
        end
    end
end)

pico.on(board_canvas, "press_start", function()
    local tx, ty = pico.get_touch()
    local c = math.floor((tx - board_x) / CELL) + 1
    local r = math.floor((ty - board_y) / CELL) + 1
    if r < 1 or r > SIZE or c < 1 or c > SIZE then return end
    onCellTap(r, c)
end)

local function resetGame()
    for r = 1, SIZE do
        for c = 1, SIZE do
            board[r][c] = 0
        end
    end
    board[4][4] = 2
    board[4][5] = 1
    board[5][4] = 1
    board[5][5] = 2
    current = 1
    game_over = false
    valid = select(1, computeValidMoves(current))
    updateStatus()
    redrawBoard()
end

pico.on(reset_button, "press_start", function()
    resetGame()
end)

resetGame()
