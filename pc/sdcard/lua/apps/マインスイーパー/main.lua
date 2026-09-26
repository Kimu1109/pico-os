-- マインスイーパー(Minesweeper)。難易度3段階(初級/中級/上級)。
-- src/lua/LuaAppScanner.cppがこのディレクトリを走査してランチャへ登録するので
-- App_List.cppには一切手を加えていない。
--
-- 盤面はButtonではなく、1枚の"Canvas"へpico.draw_*で自前描画している
-- (リバーシと同じ方式)。タップ判定はpico.get_touch()の絶対座標から
-- grid_x/grid_yを引いてマス目を逆算する。
--
-- 地雷/旗/数字は自作の.pimg(14x14、透過あり。mine.pimg/flag.pimg/digits.pimg、
-- 本ディレクトリ直下)をpico.image_load()で読み、Canvasのrenderコールバックから
-- pico.draw_image()で描く。生成には script/generate_pimg.py を使った。
-- 権限が既定値(sd_outside_app_dir=false)なので、画像は自分のapp_dir
-- (このディレクトリ)配下に置く必要がある。
--
-- 数字はpico.draw_text()ではなく画像にした: Smallフォント(行高16px)は上級の
-- セル(14px)より背が高く、セルへクリップしても「はみ出た分が削れる」だけで
-- 見やすさは改善しなかった。セルに合わせて書き出した画像なら、はみ出しそのものが
-- 起きない。digits.pimgは1〜8(各14x14、隣接数の定番配色)を横に並べた
-- 112x14のシートで、pico.draw_image()に部分描画の引数が無いため、
-- pico.set_draw_area()で1コマぶんの窓を開けてシート全体をずらして描く
-- (下のdrawDigit()参照)。
--
-- 難易度が変わるとマス数もセルの大きさ(CELL)も変わるため、盤面のCanvasは
-- 切替のたびpico.destroy()して作り直す。画像は14x14固定であらゆる難易度の
-- 最小CELL(14)に合わせてあるので、それより小さい難易度は用意しない。

local ICON = 14 -- mine.pimg/flag.pimg/digits.pimg(1コマぶん)のネイティブサイズ

local DIFFICULTIES = {
    { name = "初級", rows = 9,  cols = 9,  mines = 10, cell = 20, gap = 2 },
    { name = "中級", rows = 12, cols = 12, mines = 22, cell = 16, gap = 1 },
    { name = "上級", rows = 16, cols = 15, mines = 45, cell = 14, gap = 1 },
}

local x, y, w, h = pico.content_rect()
local margin = 4

-- 難易度ごとに差し替わる盤面パラメータ(applyDifficulty()が埋める)
local diff_index = 1
local ROWS, COLS, MINES, CELL, GAP

-- ゲーム状態(1始まりの2次元配列。resetGame()が難易度に合わせて作り直す)
local mine, revealed, flagged, adjacent = {}, {}, {}, {}

local started = false -- 最初の一手でここから地雷を配置する(最初のマスは必ず安全)
local game_over = false
local win = false
local flag_mode = false
local elapsed_ms = 0

local grid_x, grid_y, grid_w, grid_h
local canvas = nil

-- 本ディレクトリ配下の.pimgのみ許可(既定権限)なので絶対パスで直に指定する
local mine_img = pico.image_load("/lua/apps/マインスイーパー/mine.pimg")
local flag_img = pico.image_load("/lua/apps/マインスイーパー/flag.pimg")
local digits_img = pico.image_load("/lua/apps/マインスイーパー/digits.pimg")

local COLOR_UNREVEALED = 7  -- PICO_LIGHTGREY
local COLOR_REVEALED = 15   -- PICO_WHITE
local COLOR_MINE_HIT = 12   -- PICO_RED
local COLOR_GRID_LINE = 8   -- PICO_DARKGREY(セル間の隙間に透けて格子線に見える)
local COLOR_TEXT_FLAG = 12  -- PICO_RED(画像が読めなかった場合のフォールバック文字用)
local COLOR_TEXT_MINE = 0   -- PICO_BLACK(同上)

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

-- 効果音(チャンネル2)。勝ったときのジングルは短いMMLを曲として鳴らす(チャンネル1なので効果音に食われない)
local function se(freq, ms, wave, env)
    pico.sound_play(2, freq, ms, { wave = wave or "pulse25", volume = 9, envelope = env or -3 })
end

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

-- ヘッダー: 状態表示 + 操作ボタン(1行に収める都合で短縮表記にしてある)
local status_label = pico.create("Label")
pico.set(status_label, "font_size", 0)

local back_button = pico.create("Button")
pico.set(back_button, "w", 34)
pico.set(back_button, "h", 22)
pico.set(back_button, "font_size", 0)
pico.set(back_button, "text", "戻る")
pico.on(back_button, "press_start", function()
    pico.pop()
end)

local reset_button = pico.create("Button")
pico.set(reset_button, "w", 58)
pico.set(reset_button, "h", 22)
pico.set(reset_button, "font_size", 0)
pico.set(reset_button, "text", "リセット")

local flag_button = pico.create("Button")
pico.set(flag_button, "w", 52)
pico.set(flag_button, "h", 22)
pico.set(flag_button, "font_size", 0)
pico.set(flag_button, "text", "旗:OFF")
pico.on(flag_button, "press_start", function()
    flag_mode = not flag_mode
    se(flag_mode and 990 or 660, 30, "pulse12")
    pico.set(flag_button, "text", flag_mode and "旗:ON" or "旗:OFF")
end)

-- 難易度切替タブ。3段階なのでkMaxTabs(4)に収まる
local diff_tabs = pico.create("TabBar")
pico.tab_add(diff_tabs, DIFFICULTIES[1].name)
pico.tab_add(diff_tabs, DIFFICULTIES[2].name)
pico.tab_add(diff_tabs, DIFFICULTIES[3].name)

-- 行の高さはボタン実測値ではなく固定値で決め打ち(ClocksSceneのような可変フォント
-- 環境ではないので、22px固定で足りることを--shotで確認しながら詰めた)
local row1_y = y + margin           -- ボタン + 状態表示
local row2_y = row1_y + 22 + 4      -- 難易度タブ
local grid_y_base = row2_y + 22 + 6 -- 盤面の開始y(難易度が変わっても固定)

pico.set(back_button, "x", x + margin)
pico.set(back_button, "y", row1_y)
pico.set(reset_button, "x", x + margin + 34 + 4)
pico.set(reset_button, "y", row1_y)
pico.set(flag_button, "x", x + margin + 34 + 4 + 58 + 4)
pico.set(flag_button, "y", row1_y)
pico.set(status_label, "x", x + margin + 34 + 4 + 58 + 4 + 52 + 4 + 5)
pico.set(status_label, "y", row1_y + 3)

pico.set(diff_tabs, "x", x + margin)
pico.set(diff_tabs, "y", row2_y)
pico.set(diff_tabs, "w", w - margin * 2)
pico.set(diff_tabs, "h", 22)

local function setStatusLabel()
    if game_over then
        -- 右カラムの残り幅(約80px)に収まらず"GAME OVER"は画面端からはみ出た。
        -- 漢字2文字(32px相当)なら確実に収まる
        pico.set(status_label, "text", win and "クリア!" or "失敗")
        return
    end
    local flagged_count = 0
    for r = 1, ROWS do
        for c = 1, COLS do
            if flagged[r][c] then flagged_count = flagged_count + 1 end
        end
    end
    pico.set(status_label, "text", "残り:" .. (MINES - flagged_count))
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
            if mine[r][c] then revealed[r][c] = true end
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
    pico.music_play_text("#tempo 180\nA @pulse25 v11 q7 o5 l16 c e g > c e g > c4")
    -- 地雷マスへ自動で旗を立てて見せる
    for r = 1, ROWS do
        for c = 1, COLS do
            if mine[r][c] and not flagged[r][c] then flagged[r][c] = true end
        end
    end
    setStatusLabel()
end

-- 0マスは繋がっている限り自動で開く(上級でも16x15=240マスなので再帰深さの心配は無い)
local function floodReveal(r, c)
    if revealed[r][c] or flagged[r][c] then return end
    revealed[r][c] = true
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
        se(70, 900, "noise", -2) -- 爆発
        revealed[r][c] = true
        game_over = true
        win = false
        revealAllMines()
        setStatusLabel()
        return
    end

    -- 1マスだけならクリック音、0マスで広く開いたら低めの長い音
    if adjacent[r][c] == 0 then se(440, 120, "triangle", -2) else se(880, 30) end
    floodReveal(r, c)
    checkWin()
    if not game_over then setStatusLabel() end
end

local function onFlag(r, c)
    if game_over or revealed[r][c] then return end
    flagged[r][c] = not flagged[r][c]
    se(flagged[r][c] and 1320 or 660, 40, "pulse12")
    setStatusLabel()
end

-- digits_imgからn番目(1始まり)のコマだけをtarget_x,target_yへ描く。
-- 窓をICON(=1コマの大きさ)ちょうどにすること — CELLの方が大きい初級/中級で
-- CELL幅の窓にすると隣のコマの端が覗いてしまう
local function drawDigit(n, target_x, target_y)
    pico.set_draw_area(target_x, target_y, ICON, ICON)
    pico.draw_image(digits_img, target_x - (n - 1) * ICON, target_y)
    pico.clear_draw_area()
end

-- 盤面全体を毎回描き直す(リバーシのboard_canvasと同じ方式)。差分だけ塗る
-- 最適化はせず、pico.invalidate()を呼んだ側が「状態が変わった」ことだけ
-- 保証すればよい単純な作りにしてある
local function renderBoard()
    local icon_off = math.floor((CELL - ICON) / 2)
    -- 画像が読めなかった場合だけのフォールバック。セルへクリップして描く
    local text_off_x = math.floor((CELL - 8) / 2)
    local text_off_y = math.max(0, math.floor((CELL - 16) / 2))

    for r = 1, ROWS do
        for c = 1, COLS do
            local cx = grid_x + (c - 1) * (CELL + GAP)
            local cy = grid_y + (r - 1) * (CELL + GAP)

            if flagged[r][c] and not revealed[r][c] then
                pico.fill_rect(cx, cy, CELL, CELL, COLOR_UNREVEALED)
                if flag_img then
                    pico.draw_image(flag_img, cx + icon_off, cy + icon_off)
                else
                    pico.set_draw_area(cx, cy, CELL, CELL)
                    pico.draw_text(cx + text_off_x, cy + text_off_y, "F", COLOR_TEXT_FLAG)
                    pico.clear_draw_area()
                end
            elseif not revealed[r][c] then
                pico.fill_rect(cx, cy, CELL, CELL, COLOR_UNREVEALED)
            elseif mine[r][c] then
                pico.fill_rect(cx, cy, CELL, CELL, COLOR_MINE_HIT)
                if mine_img then
                    pico.draw_image(mine_img, cx + icon_off, cy + icon_off)
                else
                    pico.set_draw_area(cx, cy, CELL, CELL)
                    pico.draw_text(cx + text_off_x, cy + text_off_y, "*", COLOR_TEXT_MINE)
                    pico.clear_draw_area()
                end
            else
                pico.fill_rect(cx, cy, CELL, CELL, COLOR_REVEALED)
                local n = adjacent[r][c]
                if n > 0 then
                    if digits_img then
                        drawDigit(n, cx + icon_off, cy + icon_off)
                    else
                        pico.set_draw_area(cx, cy, CELL, CELL)
                        pico.draw_text(cx + text_off_x, cy + text_off_y, tostring(n), NUM_COLORS[n] or COLOR_TEXT_MINE)
                        pico.clear_draw_area()
                    end
                end
            end
        end
    end
end

local function onCanvasPress()
    local tx, ty, touched = pico.get_touch()
    if not touched then return end

    local c = math.floor((tx - grid_x) / (CELL + GAP)) + 1
    local r = math.floor((ty - grid_y) / (CELL + GAP)) + 1
    if r < 1 or r > ROWS or c < 1 or c > COLS then return end

    if flag_mode then
        onFlag(r, c)
    else
        onReveal(r, c)
    end
    pico.invalidate(canvas)
end

-- 難易度に合わせて盤面配列を作り直し、最初の状態へ戻す(同じ難易度のままの
-- 「リセット」からも、難易度切替からも呼ばれる)
local function resetGame()
    started = false
    game_over = false
    win = false

    mine, revealed, flagged, adjacent = {}, {}, {}, {}
    for r = 1, ROWS do
        mine[r], revealed[r], flagged[r], adjacent[r] = {}, {}, {}, {}
        for c = 1, COLS do
            mine[r][c] = false
            revealed[r][c] = false
            flagged[r][c] = false
            adjacent[r][c] = 0
        end
    end

    setStatusLabel()
    if canvas then pico.invalidate(canvas) end
end

-- 難易度を切り替える。マス数・セルの大きさが変わるので盤面のCanvasを
-- 作り直す(setW/setHで済ませず作り直すのは、中心寄せのx位置も
-- 一緒に計算し直したいため)
local function applyDifficulty(index)
    diff_index = index
    local d = DIFFICULTIES[index]
    ROWS, COLS, MINES, CELL, GAP = d.rows, d.cols, d.mines, d.cell, d.gap

    grid_w = COLS * CELL + (COLS - 1) * GAP
    grid_h = ROWS * CELL + (ROWS - 1) * GAP
    grid_x = x + math.floor((w - grid_w) / 2)
    grid_y = grid_y_base

    if canvas then
        pico.destroy(canvas)
        canvas = nil
    end

    canvas = pico.create("Canvas")
    pico.set(canvas, "x", grid_x)
    pico.set(canvas, "y", grid_y)
    pico.set(canvas, "w", grid_w)
    pico.set(canvas, "h", grid_h)
    pico.set(canvas, "background_color", COLOR_GRID_LINE)
    pico.on(canvas, "render", renderBoard)
    pico.on(canvas, "press_start", onCanvasPress)

    resetGame()
end

pico.on(diff_tabs, "tab_changed", function(id)
    -- tab_selectedは0始まり(TabBar::getSelected()そのまま)なので+1する
    applyDifficulty(pico.get(id, "tab_selected") + 1)
end)

pico.on(reset_button, "press_start", function()
    resetGame()
end)

applyDifficulty(diff_index)

function loop(dt)
    elapsed_ms = elapsed_ms + dt
end
