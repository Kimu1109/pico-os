-- ブロック崩し。ブロック/パドル/アイテムは図形ウィジェット(Rect/Ellipse/Triangle)で、
-- 位置だけを毎フレームpico.setする。ステージはstages.luaへ分離した
-- (main.luaを読み込み上限16KiBに収めるため。今もぎりぎりなので足すときは注意)。
-- 外部コントローラー: 左右=パドル、A/START/上=発射・ダイアログを閉じる、HOME=戻る。

local function clamp(v, lo, hi)
    if v < lo then return lo end
    if v > hi then return hi end
    return v
end

local STAGES
do
    local code = pico.sd_read("/lua/apps/ブロック崩し/stages.lua")
    local chunk = code and load(code, "stages")
    local ok, result = false, nil
    if chunk then ok, result = pcall(chunk) end
    if ok then STAGES = result end
end
if not STAGES then
    pico.show_error("ステージデータの読み込みに失敗しました")
    STAGES = { cols = 8, list = { { rows = 3, cell = function(r, c) return 1 end } },
        itemShapes = function() return {} end }
end

local COLS = STAGES.cols
local MAX_ROWS = 0
for i = 1, #STAGES.list do
    if STAGES.list[i].rows > MAX_ROWS then MAX_ROWS = STAGES.list[i].rows end
end

-- 色(=速度)のtier。1が最速(赤)、6が最遅(青)
local TIER_COLOR = { 12, 13, 14, 10, 11, 9 }
local TIER_MULT = { 2.0, 1.6, 1.35, 1.25, 1.0, 0.7 }
local TIER_POINTS = { 60, 50, 40, 30, 20, 10 }
local WALL_TIER = -1 -- 壊せないブロック(灰色)。stages.luaのaddWalls()が挿入する
local WALL_COLOR = 8 -- PICO_DARKGREY
local function baseSpeed(stage) return 110 + stage * 5 end -- px/秒

local BLOCK_GAP, ROW_GAP, BLOCK_H = 2, 2, 12
local BALL_R = 4
local PADDLE_H = 8
-- パドル幅はステージが進むほど狭くする(5ステージごとに4px、最小28px)
local PADDLE_W_MAX, PADDLE_W_MIN, PADDLE_W_STEP, PADDLE_W_STAGE_SPAN = 40, 28, 4, 5
local function paddleWidthForStage(stage)
    local w = PADDLE_W_MAX - math.floor((stage - 1) / PADDLE_W_STAGE_SPAN) * PADDLE_W_STEP
    return math.max(PADDLE_W_MIN, w)
end
local ITEM_SIZE = 10
local ITEM_TRIBALL, ITEM_DOUBLE, ITEM_SLOW = 1, 2, 3
local ITEM_FALL_SPEED = 70 -- px/秒
local DROP_CHANCE = 0.2
local MAX_BALLS = 16
local MAX_ITEMS = 4
local STEP_MAX = 6 -- サブステップ分割の閾値(px)。低フレームレートでの貫通防止
local PAD_SPEED = 220 -- コントローラーでのパドルの速さ(px/秒)
local MAX_ANGLE = 1.1 -- パドル反射の最大角(ラジアン)

local cx, cy, cw, ch = pico.content_rect()
local MARGIN = 4
local field_x = cx + MARGIN
local field_w = cw - MARGIN * 2
local TOP_WALL = cy + 22
local BLOCK_TOP = TOP_WALL + 2
local BLOCK_W = math.floor((field_w - (COLS - 1) * BLOCK_GAP) / COLS)
local totalBlockW = COLS * BLOCK_W + (COLS - 1) * BLOCK_GAP
local blockOffsetX = field_x + math.floor((field_w - totalBlockW) / 2)
local paddle_y = cy + ch - 24
local BOTTOM_LIMIT = cy + ch

local blockWidgets = {}
for slot = 1, MAX_ROWS * COLS do
    local id = pico.create("Rect")
    pico.set(id, "w", BLOCK_W)
    pico.set(id, "h", BLOCK_H)
    pico.set(id, "visible", false)
    blockWidgets[slot] = id
end

local ballWidgets = {}
for i = 1, MAX_BALLS do
    local id = pico.create("Ellipse")
    pico.set(id, "w", BALL_R * 2)
    pico.set(id, "h", BALL_R * 2)
    pico.set(id, "color", 0)
    pico.set(id, "visible", false)
    ballWidgets[i] = id
end

-- アイテムの図形(ids[種類])はstages.luaのitemShapes()が作る
local itemSlots = {}
for i = 1, MAX_ITEMS do
    itemSlots[i] = { ids = STAGES.itemShapes(ITEM_SIZE), active = false, type = 0, x = 0, y = 0 }
end

local paddle_w = paddleWidthForStage(1)
local paddle_id = pico.create("Rect")
pico.set(paddle_id, "w", paddle_w); pico.set(paddle_id, "h", PADDLE_H)
pico.set(paddle_id, "color", 1); pico.set(paddle_id, "y", paddle_y)

local border_id = pico.create("Rect")
pico.set(border_id, "filled", false); pico.set(border_id, "color", 7)
pico.set(border_id, "x", field_x); pico.set(border_id, "y", TOP_WALL)
pico.set(border_id, "w", field_w); pico.set(border_id, "h", BOTTOM_LIMIT - TOP_WALL)

local back_button = pico.create("Button")
pico.set(back_button, "x", cx + 2); pico.set(back_button, "y", cy + 2)
pico.set(back_button, "w", 34); pico.set(back_button, "h", 16)
pico.set(back_button, "font_size", 0); pico.set(back_button, "text", "戻る")
pico.on(back_button, "press_start", function() pico.pop() end)

local status_label = pico.create("Label")
pico.set(status_label, "font_size", 0)
pico.set(status_label, "x", cx + 48); pico.set(status_label, "y", cy + 4)

local score, lives, stage_idx = 0, 3, 1
local blocks, blocks_remaining = {}, 0
local balls = {}
local paddle_cx = field_x + field_w / 2
local game_state = "ready" -- "ready" | "playing" | "dialog"
local prev_touched = false
local dlg, dlg_fn -- 表示中のダイアログ(コントローラーで閉じるため)

-- 効果音はch2で1フレーム1回、優先度(pri)の高いもの(ボールが多いと命令の列が溢れるため)
local snd
local function se(pri, f, ms, wave)
    if not snd or pri > snd[1] then snd = { pri, f, ms, wave or "pulse25" } end
end
-- ジングルは短いMMLを曲として鳴らす(ch1)
local function jingle(mml)
    pico.music_play_text("#tempo 200\nA @pulse25 v11 q7 o5 l16 " .. mml)
end

local function syncItemSlot(i)
    local s = itemSlots[i]
    for t, id in pairs(s.ids) do pico.set(id, "visible", s.active and s.type == t) end
end

local function syncBalls()
    for i = 1, MAX_BALLS do
        pico.set(ballWidgets[i], "visible", i <= #balls)
    end
end

local function updateHud()
    pico.set(status_label, "text",
        "St:" .. stage_idx .. "/" .. #STAGES.list .. " Sc:" .. score .. " L:" .. lives)
end

local function loadStage(idx)
    local data = STAGES.list[idx]
    blocks, blocks_remaining = {}, 0
    for r = 1, MAX_ROWS do
        for c = 1, COLS do
            local slot = (r - 1) * COLS + c
            local wid = blockWidgets[slot]
            local tier = (r <= data.rows) and data.cell(r, c) or 0
            if tier ~= 0 then
                local bx = blockOffsetX + (c - 1) * (BLOCK_W + BLOCK_GAP)
                local by = BLOCK_TOP + (r - 1) * (BLOCK_H + ROW_GAP)
                blocks[slot] = { x = bx, y = by, tier = tier }
                if tier ~= WALL_TIER then blocks_remaining = blocks_remaining + 1 end
                pico.set(wid, "x", bx); pico.set(wid, "y", by)
                pico.set(wid, "color", (tier == WALL_TIER) and WALL_COLOR or TIER_COLOR[tier])
                pico.set(wid, "visible", true)
            else
                pico.set(wid, "visible", false)
            end
        end
    end
    paddle_w = paddleWidthForStage(idx)
    pico.set(paddle_id, "w", paddle_w)
    updateHud()
end

local function readyBall()
    balls = {}
    syncBalls()
    for i = 1, MAX_ITEMS do
        itemSlots[i].active = false
        syncItemSlot(i)
    end
    paddle_cx = field_x + field_w / 2
    pico.set(paddle_id, "x", math.floor(paddle_cx - paddle_w / 2))
    game_state = "ready"
end

local function startGame()
    score, lives, stage_idx = 0, 3, 1
    loadStage(stage_idx)
    readyBall()
end

local function applyTierSpeed(b, tier)
    local speed = baseSpeed(stage_idx) * TIER_MULT[tier]
    local mag = math.sqrt(b.vx * b.vx + b.vy * b.vy)
    if mag > 0 then
        local k = speed / mag
        b.vx, b.vy = b.vx * k, b.vy * k
    end
end

local function spawnItem(x, y)
    for i = 1, MAX_ITEMS do
        local s = itemSlots[i]
        if not s.active then
            s.active, s.type = true, math.random(1, 3)
            s.x, s.y = x - ITEM_SIZE / 2, y - ITEM_SIZE / 2
            syncItemSlot(i)
            return
        end
    end
end

local function destroyBlock(slot)
    local blk = blocks[slot]
    if not blk then return end
    pico.set(blockWidgets[slot], "visible", false)
    blocks[slot] = nil
    blocks_remaining = blocks_remaining - 1
    se(3, 1500 - blk.tier * 150, 50, "pulse50")
    score = score + TIER_POINTS[blk.tier]
    updateHud()
    if math.random() < DROP_CHANCE then
        spawnItem(blk.x + BLOCK_W / 2, blk.y + BLOCK_H / 2)
    end
end

local function collideBlocks(b)
    for slot = 1, MAX_ROWS * COLS do
        local blk = blocks[slot]
        if blk then
            local nx = clamp(b.x, blk.x, blk.x + BLOCK_W)
            local ny = clamp(b.y, blk.y, blk.y + BLOCK_H)
            local dx, dy = b.x - nx, b.y - ny
            if dx * dx + dy * dy < BALL_R * BALL_R then
                local left = (b.x + BALL_R) - blk.x
                local right = (blk.x + BLOCK_W) - (b.x - BALL_R)
                local top = (b.y + BALL_R) - blk.y
                local bottom = (blk.y + BLOCK_H) - (b.y - BALL_R)
                local m = math.min(left, right, top, bottom)
                if m == left then
                    b.x = blk.x - BALL_R; b.vx = -math.abs(b.vx)
                elseif m == right then
                    b.x = blk.x + BLOCK_W + BALL_R; b.vx = math.abs(b.vx)
                elseif m == top then
                    b.y = blk.y - BALL_R; b.vy = -math.abs(b.vy)
                else
                    b.y = blk.y + BLOCK_H + BALL_R; b.vy = math.abs(b.vy)
                end
                if blk.tier == WALL_TIER then
                    se(2, 180, 40, "triangle") -- 壊せないブロック: 反射だけ
                else
                    applyTierSpeed(b, blk.tier)
                    destroyBlock(slot)
                end
                return
            end
        end
    end
end

local function collidePaddle(b)
    local px = paddle_cx - paddle_w / 2
    if b.x + BALL_R >= px and b.x - BALL_R <= px + paddle_w
        and b.y + BALL_R >= paddle_y and b.y - BALL_R <= paddle_y + PADDLE_H then
        local offset = clamp((b.x - paddle_cx) / (paddle_w / 2), -1, 1)
        local speed = math.sqrt(b.vx * b.vx + b.vy * b.vy)
        local angle = offset * MAX_ANGLE
        b.vx = speed * math.sin(angle)
        b.vy = -speed * math.cos(angle)
        b.y = paddle_y - BALL_R
        se(2, 520, 40)
    end
end

local function stepBall(b, dtSec)
    local dist = math.sqrt(b.vx * b.vx + b.vy * b.vy) * dtSec
    local steps = math.max(1, math.ceil(dist / STEP_MAX))
    local subDt = dtSec / steps
    for i = 1, steps do
        b.x = b.x + b.vx * subDt
        b.y = b.y + b.vy * subDt
        local vx, vy = b.vx, b.vy
        if b.x - BALL_R < field_x then
            b.x = field_x + BALL_R; b.vx = math.abs(b.vx)
        elseif b.x + BALL_R > field_x + field_w then
            b.x = field_x + field_w - BALL_R; b.vx = -math.abs(b.vx)
        end
        if b.y - BALL_R < TOP_WALL then
            b.y = TOP_WALL + BALL_R; b.vy = math.abs(b.vy)
        end
        if vx ~= b.vx or vy ~= b.vy then se(1, 330, 20, "pulse12") end
        collideBlocks(b)
        if b.vy > 0 then collidePaddle(b) end
        if b.y - BALL_R > BOTTOM_LIMIT then
            b.dead = true
            return
        end
    end
end

local function spawnExtraBalls(n)
    local speed = baseSpeed(stage_idx)
    for i = 1, n do
        if #balls < MAX_BALLS then
            local ang = (i - (n + 1) / 2) * 0.35
            table.insert(balls, {
                x = paddle_cx, y = paddle_y - BALL_R * 2 - 1,
                vx = speed * math.sin(ang), vy = -speed * math.cos(ang),
            })
        end
    end
    syncBalls()
end

local function doubleBalls()
    local n = #balls
    for i = 1, n do
        if #balls < MAX_BALLS then
            local b = balls[i]
            table.insert(balls, { x = b.x, y = b.y, vx = -b.vx, vy = b.vy })
        end
    end
    syncBalls()
end

local function slowAllBalls()
    for i = 1, #balls do applyTierSpeed(balls[i], #TIER_MULT) end
end

local function applyItem(t)
    if t == ITEM_TRIBALL then
        spawnExtraBalls(3)
    elseif t == ITEM_DOUBLE then
        doubleBalls()
    else
        slowAllBalls()
    end
end

local function updateItems(dtSec)
    for i = 1, MAX_ITEMS do
        local s = itemSlots[i]
        if s.active then
            s.y = s.y + ITEM_FALL_SPEED * dtSec
            local px = paddle_cx - paddle_w / 2
            if s.x + ITEM_SIZE >= px and s.x <= px + paddle_w
                and s.y + ITEM_SIZE >= paddle_y and s.y <= paddle_y + PADDLE_H then
                s.active = false
                syncItemSlot(i)
                se(4, 1760, 120, "pulse50")
                applyItem(s.type)
            elseif s.y > BOTTOM_LIMIT then
                s.active = false
                syncItemSlot(i)
            else
                local id = s.ids[s.type]
                pico.set(id, "x", math.floor(s.x))
                pico.set(id, "y", math.floor(s.y))
            end
        end
    end
end

-- タッチで閉じても、コントローラーで閉じても(loop()参照)fnが1回だけ呼ばれる
local function dialog(text, btn, fn)
    game_state = "dialog"
    dlg_fn = fn
    dlg = pico.show_message(text, "", btn)
    pico.on(dlg, "closed", function() dlg = nil; fn() end)
end

local function stageClear()
    if stage_idx >= #STAGES.list then
        jingle("c e g > c e g > c4")
        dialog("全ステージクリア!  スコア:" .. score, "最初から", startGame)
        return
    end
    jingle("c e g > c4")
    dialog("ステージ" .. stage_idx .. "クリア!", "次へ", function()
        stage_idx = stage_idx + 1
        loadStage(stage_idx)
        readyBall()
    end)
end

local function loseLife()
    lives = lives - 1
    updateHud()
    if lives <= 0 then
        jingle("l8 e d c < g2")
        dialog("ゲームオーバー  スコア:" .. score, "もう一度", startGame)
    else
        jingle("l8 g e c4")
        readyBall()
    end
end

local function launch()
    local speed = baseSpeed(stage_idx)
    balls = { { x = paddle_cx, y = paddle_y - BALL_R * 2 - 1, vx = 0, vy = -speed } }
    syncBalls()
    pico.set(ballWidgets[1], "x", math.floor(balls[1].x - BALL_R))
    pico.set(ballWidgets[1], "y", math.floor(balls[1].y - BALL_R))
    game_state = "playing"
    se(2, 880, 60)
end

local function setPaddle(x)
    paddle_cx = clamp(x, field_x + paddle_w / 2, field_x + field_w - paddle_w / 2)
    pico.set(paddle_id, "x", math.floor(paddle_cx - paddle_w / 2))
end

function loop(dt)
    if pico.pad_pressed("home") then pico.pop() return end
    local tx, ty, touched = pico.get_touch()
    if touched then setPaddle(tx) end
    local dir = (pico.pad_down("right") and 1 or 0) - (pico.pad_down("left") and 1 or 0)
    if dir ~= 0 and game_state ~= "dialog" then setPaddle(paddle_cx + dir * PAD_SPEED * dt / 1000) end
    local go = pico.pad_pressed("a") or pico.pad_pressed("start") or pico.pad_pressed("up")

    if game_state == "dialog" then
        if go and dlg then
            local id = dlg
            dlg = nil
            pico.destroy(id) -- closedは呼ばれないので自分でfnを呼ぶ
            dlg_fn()
        end
    elseif game_state == "ready" then
        pico.set(ballWidgets[1], "visible", true)
        pico.set(ballWidgets[1], "x", math.floor(paddle_cx - BALL_R))
        pico.set(ballWidgets[1], "y", math.floor(paddle_y - BALL_R * 2 - 1))
        if (touched and not prev_touched) or go then launch() end
    elseif game_state == "playing" then
        local dtSec = dt / 1000
        for i = #balls, 1, -1 do
            stepBall(balls[i], dtSec)
            if balls[i].dead then table.remove(balls, i) end
        end
        syncBalls()
        for i = 1, #balls do
            pico.set(ballWidgets[i], "x", math.floor(balls[i].x - BALL_R))
            pico.set(ballWidgets[i], "y", math.floor(balls[i].y - BALL_R))
        end
        updateItems(dtSec)
        if #balls == 0 then
            loseLife()
        elseif blocks_remaining == 0 then
            stageClear()
        end
    end
    prev_touched = touched
    if snd then
        pico.sound_play(2, snd[2], snd[3], { wave = snd[4], volume = 9, envelope = -3 })
        snd = nil
    end
end

math.randomseed(pico.get_time().sec * 1000 + pico.get_time().min)
startGame()
