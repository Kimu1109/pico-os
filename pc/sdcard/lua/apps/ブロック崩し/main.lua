-- ブロック崩し。ブロック/パドル/落下アイテムは全てpico.create()の図形ウィジェット
-- (Rect/Ellipse/Triangle)で、Canvasの直接描画は使わない。座標計算だけをLua側で行い、
-- 毎フレームpico.set(id,"x"/"y",...)で位置を反映する。
--
-- ステージ(20面ぶん)の生成ロジックはstages.luaへ分離してpico.sd_read+load()で
-- 読み込む(main.lua単体をスクリプト読み込み上限16KiB以内に収めるため)。

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
    STAGES = { cols = 8, list = { { rows = 3, cell = function(r, c) return 1 end } } }
end

local COLS = STAGES.cols
local MAX_ROWS = 0
for i = 1, #STAGES.list do
    if STAGES.list[i].rows > MAX_ROWS then MAX_ROWS = STAGES.list[i].rows end
end

-- 色(=速度)のtier。1が最速(赤)、6が最遅(青)。元祖ブロック崩しの「上段ほど速い」に寄せた配色
local TIER_COLOR = { 12, 13, 14, 10, 11, 9 }
local TIER_MULT = { 1.5, 1.3, 1.15, 1.0, 0.85, 0.7 }
local TIER_POINTS = { 60, 50, 40, 30, 20, 10 }
local function baseSpeed(stage) return 84 + stage * 4 end -- px/秒

local BLOCK_GAP, ROW_GAP, BLOCK_H = 2, 2, 12
local BALL_R = 4
local PADDLE_W, PADDLE_H = 40, 8
local ITEM_SIZE = 10
local ITEM_TRIBALL, ITEM_DOUBLE, ITEM_SLOW = 1, 2, 3
local ITEM_FALL_SPEED = 70 -- px/秒
local DROP_CHANCE = 0.2
local MAX_BALLS = 16
local MAX_ITEMS = 4
local STEP_MAX = 6 -- サブステップ分割の閾値(px)。低フレームレートでの貫通防止
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

local itemSlots = {}
for i = 1, MAX_ITEMS do
    local r = pico.create("Rect")
    pico.set(r, "w", ITEM_SIZE); pico.set(r, "h", ITEM_SIZE)
    pico.set(r, "color", 14); pico.set(r, "visible", false)
    local e = pico.create("Ellipse")
    pico.set(e, "w", ITEM_SIZE); pico.set(e, "h", ITEM_SIZE)
    pico.set(e, "color", 11); pico.set(e, "visible", false)
    local t = pico.create("Triangle")
    pico.set(t, "x1", 0); pico.set(t, "y1", ITEM_SIZE)
    pico.set(t, "x2", ITEM_SIZE / 2); pico.set(t, "y2", 0)
    pico.set(t, "x3", ITEM_SIZE); pico.set(t, "y3", ITEM_SIZE)
    pico.set(t, "color", 9); pico.set(t, "visible", false)
    itemSlots[i] = { rect = r, ell = e, tri = t, active = false, type = 0, x = 0, y = 0 }
end

local paddle_id = pico.create("Rect")
pico.set(paddle_id, "w", PADDLE_W); pico.set(paddle_id, "h", PADDLE_H)
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
pico.set(status_label, "x", cx + 42); pico.set(status_label, "y", cy + 4)

local score, lives, stage_idx = 0, 3, 1
local blocks, blocks_remaining = {}, 0
local balls = {}
local paddle_cx = field_x + field_w / 2
local game_state = "ready" -- "ready" | "playing" | "dialog"
local prev_touched = false

local function syncItemSlot(i)
    local s = itemSlots[i]
    pico.set(s.rect, "visible", s.active and s.type == ITEM_TRIBALL)
    pico.set(s.ell, "visible", s.active and s.type == ITEM_DOUBLE)
    pico.set(s.tri, "visible", s.active and s.type == ITEM_SLOW)
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
            if tier > 0 then
                local bx = blockOffsetX + (c - 1) * (BLOCK_W + BLOCK_GAP)
                local by = BLOCK_TOP + (r - 1) * (BLOCK_H + ROW_GAP)
                blocks[slot] = { x = bx, y = by, tier = tier }
                blocks_remaining = blocks_remaining + 1
                pico.set(wid, "x", bx); pico.set(wid, "y", by)
                pico.set(wid, "color", TIER_COLOR[tier])
                pico.set(wid, "visible", true)
            else
                pico.set(wid, "visible", false)
            end
        end
    end
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
    pico.set(paddle_id, "x", math.floor(paddle_cx - PADDLE_W / 2))
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
                applyTierSpeed(b, blk.tier)
                destroyBlock(slot)
                return
            end
        end
    end
end

local function collidePaddle(b)
    local px = paddle_cx - PADDLE_W / 2
    if b.x + BALL_R >= px and b.x - BALL_R <= px + PADDLE_W
        and b.y + BALL_R >= paddle_y and b.y - BALL_R <= paddle_y + PADDLE_H then
        local offset = clamp((b.x - paddle_cx) / (PADDLE_W / 2), -1, 1)
        local speed = math.sqrt(b.vx * b.vx + b.vy * b.vy)
        local angle = offset * MAX_ANGLE
        b.vx = speed * math.sin(angle)
        b.vy = -speed * math.cos(angle)
        b.y = paddle_y - BALL_R
    end
end

local function stepBall(b, dtSec)
    local dist = math.sqrt(b.vx * b.vx + b.vy * b.vy) * dtSec
    local steps = math.max(1, math.ceil(dist / STEP_MAX))
    local subDt = dtSec / steps
    for i = 1, steps do
        b.x = b.x + b.vx * subDt
        b.y = b.y + b.vy * subDt
        if b.x - BALL_R < field_x then
            b.x = field_x + BALL_R; b.vx = math.abs(b.vx)
        elseif b.x + BALL_R > field_x + field_w then
            b.x = field_x + field_w - BALL_R; b.vx = -math.abs(b.vx)
        end
        if b.y - BALL_R < TOP_WALL then
            b.y = TOP_WALL + BALL_R; b.vy = math.abs(b.vy)
        end
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
    local speed = baseSpeed(stage_idx) * TIER_MULT[#TIER_MULT]
    for i = 1, #balls do
        local b = balls[i]
        local mag = math.sqrt(b.vx * b.vx + b.vy * b.vy)
        if mag > 0 then
            local k = speed / mag
            b.vx, b.vy = b.vx * k, b.vy * k
        end
    end
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
            local px = paddle_cx - PADDLE_W / 2
            if s.x + ITEM_SIZE >= px and s.x <= px + PADDLE_W
                and s.y + ITEM_SIZE >= paddle_y and s.y <= paddle_y + PADDLE_H then
                s.active = false
                syncItemSlot(i)
                applyItem(s.type)
            elseif s.y > BOTTOM_LIMIT then
                s.active = false
                syncItemSlot(i)
            else
                local id = (s.type == ITEM_TRIBALL) and s.rect
                    or ((s.type == ITEM_DOUBLE) and s.ell or s.tri)
                pico.set(id, "x", math.floor(s.x))
                pico.set(id, "y", math.floor(s.y))
            end
        end
    end
end

local function gameOver()
    game_state = "dialog"
    local id = pico.show_message("ゲームオーバー  スコア:" .. score, "", "もう一度")
    pico.on(id, "closed", function() startGame() end)
end

local function gameClear()
    game_state = "dialog"
    local id = pico.show_message("全ステージクリア!  スコア:" .. score, "", "最初から")
    pico.on(id, "closed", function() startGame() end)
end

local function stageClear()
    if stage_idx >= #STAGES.list then
        gameClear()
        return
    end
    game_state = "dialog"
    local id = pico.show_message("ステージ" .. stage_idx .. "クリア!", "", "次へ")
    pico.on(id, "closed", function()
        stage_idx = stage_idx + 1
        loadStage(stage_idx)
        readyBall()
    end)
end

local function loseLife()
    lives = lives - 1
    updateHud()
    if lives <= 0 then
        gameOver()
    else
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
end

local function updatePaddle(tx, touched)
    if touched then
        paddle_cx = clamp(tx, field_x + PADDLE_W / 2, field_x + field_w - PADDLE_W / 2)
        pico.set(paddle_id, "x", math.floor(paddle_cx - PADDLE_W / 2))
    end
end

function loop(dt)
    local tx, ty, touched = pico.get_touch()
    updatePaddle(tx, touched)

    if game_state == "ready" then
        pico.set(ballWidgets[1], "visible", true)
        pico.set(ballWidgets[1], "x", math.floor(paddle_cx - BALL_R))
        pico.set(ballWidgets[1], "y", math.floor(paddle_y - BALL_R * 2 - 1))
        if touched and not prev_touched then launch() end
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
end

math.randomseed(pico.get_time().sec * 1000 + pico.get_time().min)
startGame()
