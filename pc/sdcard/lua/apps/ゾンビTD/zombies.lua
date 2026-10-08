-- ゾンビTD: ゾンビ(出現・流れの場をたどって歩く・押し合い・ベースへの攻撃)。仕様は ZOMBIE_TD.md。
-- 道は C++ の流れの場(pico.iso.flow_*)。ゾンビは自分の柱の「次の柱」の真ん中へ向かって歩くだけ。
-- 押し合いも C++(pico.iso.crowd)で、その後に位置を読み戻す(pico.iso.entity_pos)。
local iso = pico.iso
local M = {}

local MAX_ALIVE = 40          -- 同時に出ているゾンビの上限(超えた分は順番待ち)
local SPAWN_GAP = 0.35        -- 同じ出現位置から次を出すまでの秒
local TAG = 1                 -- 人や物の tag(ゾンビ)

-- 種類(数値は初期値。ZOMBIE_TD.md「ゾンビ側」)
M.TYPES = {
    normal = { hp = 30, speed = 1.0, dmg = 5, rate = 1.0, reach = 0, sx = 0, w = 12, h = 22, r = 0.22, mass = 1, money = 5 },
    ranged = { hp = 20, speed = 0.9, dmg = 4, rate = 1.6, reach = 5, sx = 24, w = 12, h = 22, r = 0.22, mass = 1, money = 7 },
    heavy  = { hp = 90, speed = 0.55, dmg = 12, rate = 1.4, reach = 0, sx = 48, w = 16, h = 26, r = 0.32, mass = 3, money = 12 },
}

local img                     -- units.pimg
local list = {}               -- 出ているゾンビ
local queue = {}              -- 順番待ち(種類の名前)
local spawns = {}             -- 出現位置 {x=, z=, y=, wait=}
local base                    -- {id=, x=, z=, y=, hit=function(dmg)}(id = ベースの人や物。石の狙い先)
local stand_rules
M.list = list
M.stats = { spawned = 0, killed = 0 }

function M.init(units_image, spawn_points, base_info, rules)
    img = units_image
    spawns = {}
    for i, s in ipairs(spawn_points) do spawns[i] = { x = s.x, z = s.z, y = s.y, wait = 0 } end
    base = base_info
    stand_rules = rules
    M.clear()
end

function M.clear()
    for i = #list, 1, -1 do iso.entity_remove(list[i].id); list[i] = nil end
    for i = #queue, 1, -1 do queue[i] = nil end
end

function M.queue(kind, n)
    for _ = 1, n or 1 do queue[#queue + 1] = kind end
end

function M.alive() return #list end
function M.waiting() return #queue end

-- 立つ高さ(柱の一番上。バリケードの中も立てる)
local function stand(x, z, y)
    return iso.stand(x, z, nil, stand_rules) or y
end

local function spawn_one(kind)
    local t = M.TYPES[kind]
    -- 空いている出現位置から(全部が待ち中なら出さない)
    local best
    for _, s in ipairs(spawns) do
        if s.wait <= 0 and (not best or s.wait < best.wait) then best = s end
    end
    if not best then return false end
    best.wait = SPAWN_GAP
    local x = best.x + 0.5 + (math.random() - 0.5) * 1.6
    local z = best.z + 0.5 + (math.random() - 0.5) * 1.6
    local y = stand(x, z, best.y)
    local id = iso.entity_add(img, x, y, z, {
        sx = t.sx, sy = 40 - t.h, w = t.w, h = t.h, r = t.r, height = t.h / 16,
        crowd = "move", mass = t.mass, tag = TAG, shadow_color = 3,
    })
    if not id then return false end
    list[#list + 1] = {
        id = id, kind = kind, t = t, x = x, y = y, z = z, gy = y, hp = t.hp,
        col = -1, tx = x, tz = z, frame = 0, ft = 0, flip = false, atk = 0, state = "walk",
    }
    M.stats.spawned = M.stats.spawned + 1
    return true
end

-- 1匹を dt 秒進める
local function step(zb, dt)
    local t = zb.t
    local cx, cz = math.floor(zb.x), math.floor(zb.z)
    local col = cx * 1024 + cz
    if col ~= zb.col then
        -- 柱が変わったら、次に向かう柱と立つ高さを引き直す
        zb.col = col
        zb.gy = stand(zb.x, zb.z, zb.gy)
        local d, nx, nz = iso.flow_get(cx, cz)
        zb.dist = d
        -- 遠距離ゾンビ: 届く所まで来て、地形に遮られずベースが見えたら止まって投げる
        zb.throw = t.reach > 0 and d ~= nil and d <= t.reach
            and iso.sight(zb.x, zb.gy + 1.2, zb.z, base.x + 0.5, base.y + 1.5, base.z + 0.5)
        if nx then zb.tx, zb.tz = nx + 0.5, nz + 0.5
        elseif d then zb.tx, zb.tz = nil, nil          -- 目的地(ベースの隣)
        else zb.tx, zb.tz = base.x + 0.5, base.z + 0.5 end   -- 届かない所に押し出された: ベースの方へ
    end
    local attacking = (zb.tx == nil) or zb.throw
    local moving = false
    if attacking then
        zb.atk = zb.atk - dt
        if zb.atk <= 0 then
            zb.atk = t.rate
            if t.reach > 0 then
                iso.shot_add(zb.x, zb.y + 1.2, zb.z, {
                    target = base.id, speed = 6, arc = 0.4, color = 8, size = 3, tag = t.dmg,
                })
            else
                base.hit(t.dmg)
            end
        end
        -- ベースの方を向く
        local face = (base.x - zb.x) - (base.z - zb.z) < 0
        if face ~= zb.flip then zb.flip = face; zb.dirty = true end
    else
        local dx, dz = zb.tx - zb.x, zb.tz - zb.z
        local len = math.sqrt(dx * dx + dz * dz)
        local v = t.speed * dt
        if len > 1e-4 then
            if v > len then v = len end
            zb.x = zb.x + dx / len * v
            zb.z = zb.z + dz / len * v
            moving = true
            -- 画面の左右(x - z が減る = 左)で向きを変える
            local sdx = dx - dz
            if sdx < -0.05 and not zb.flip then zb.flip = true; zb.dirty = true
            elseif sdx > 0.05 and zb.flip then zb.flip = false; zb.dirty = true end
        end
    end
    -- 段差は少しずつ上り下り(見た目だけ。位置の判定は柱で決まる)
    if zb.y ~= zb.gy then
        local s = 6 * dt
        if math.abs(zb.gy - zb.y) <= s then zb.y = zb.gy
        elseif zb.gy > zb.y then zb.y = zb.y + s else zb.y = zb.y - s end
    end
    -- 歩く2コマ
    if moving then
        zb.ft = zb.ft + dt
        if zb.ft >= 0.25 then zb.ft = 0; zb.frame = 1 - zb.frame; zb.dirty = true end
    elseif zb.frame ~= 0 then zb.frame = 0; zb.dirty = true end
    if zb.dirty then
        zb.dirty = false
        iso.entity_set(zb.id, { sx = t.sx + zb.frame * t.w, flip = zb.flip })
    end
end

function M.update(dt)
    for _, s in ipairs(spawns) do s.wait = s.wait - dt end
    while #queue > 0 and #list < MAX_ALIVE do
        if not spawn_one(queue[1]) then break end
        table.remove(queue, 1)
    end
    for i = 1, #list do
        local zb = list[i]
        step(zb, dt)
        iso.entity_move(zb.id, zb.x, zb.y, zb.z)
    end
    -- 押し合い(C++)。動いたものの位置を読み戻す
    if iso.crowd(2, stand_rules) > 0 then
        for i = 1, #list do
            local zb = list[i]
            local x, _, z = iso.entity_pos(zb.id)
            if x then zb.x, zb.z = x, z end
        end
    end
end

-- 倒す(戦闘は次の段。今はテスト用)
function M.kill(i)
    local zb = list[i]
    if not zb then return end
    iso.entity_remove(zb.id)
    table.remove(list, i)
    M.stats.killed = M.stats.killed + 1
end

M.MAX_ALIVE = MAX_ALIVE
return M
