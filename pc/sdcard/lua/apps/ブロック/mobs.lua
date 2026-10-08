-- 村人と羊: pico.iso の人や物(エンティティ)の使い方の例。
-- 箱庭の上を歩き回り(1段なら登る、崖は落ちる、水と壁の前では向きを変える)、タップすると跳ねる。
-- ブロックとの前後(壁の裏に回ると隠れる)と足元の影はエンジンが描く。ここは動かし方だけ。
-- 絵は people.pimg(script/generate_blocks_people.py が作る)。
local iso = pico.iso
local M = {}

local KINDS = {
    villager = { sx = 0, sy = 0, w = 12, h = 22, frames = { 0, 12 }, speed = 1.5, r = 0.2, height = 1.4, n = 3 },
    sheep = { sx = 24, sy = 10, w = 16, h = 12, speed = 0.9, r = 0.3, height = 0.75, n = 2, faces_right = true },
}
local GRAVITY = 30     -- ブロック/秒^2
local JUMP = 9         -- 跳ねたときの上向きの速さ(約1.3段)
local FAR = 40         -- カーソルからこれより離れたら近くへ連れてくる
local D = 0.7071
local DIRS = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { D, D }, { -D, -D }, { D, -D }, { -D, D } }

local img = nil
local mobs = {}

function M.init(image) img = image end

local function solid(b) return b ~= 0 and b ~= 25 end   -- 空気と松明は通れる

-- (x, z) に半径 r の足元で立ったときの地面の高さ(y+1 より上は登れない)。立てなければ nil
local function stand_height(x, y, z, r, height)
    local g = -1
    for _, c in ipairs({ { -r, -r }, { r, -r }, { -r, r }, { r, r } }) do
        local px, pz = x + c[1], z + c[2]
        local gy = iso.ground(px, pz, y + 1.01)
        if not gy or gy > y + 1 then return nil end
        local ix, iz = math.floor(px), math.floor(pz)
        if iso.get(ix, gy - 1, iz) == 1 then return nil end          -- 水には入らない
        for yy = gy, gy + math.ceil(height) - 1 do                    -- 頭がつかえる
            if solid(iso.get(ix, yy, iz)) then return nil end
        end
        if gy > g then g = gy end
    end
    return g
end

local function new_dir(m)
    local d = DIRS[math.random(#DIRS)]
    m.dx, m.dz = d[1], d[2]
    m.walk = 1 + math.random() * 3          -- 秒
    m.wait = math.random() < 0.3 and (0.5 + math.random() * 1.5) or 0
    if m.k.faces_right then
        -- 画面で左へ動くなら反転(絵は右向き)。画面の横は 16*(x - z)
        local sdx = m.dx - m.dz
        if sdx ~= 0 then iso.entity_set(m.id, { flip = sdx < 0 }) end
    end
end

-- (cx, cz) のまわりの、立てる所へ置く(置けなければ false)
local function place(m, cx, cz)
    for _ = 1, 20 do
        local x = cx + math.random(-6, 6) + 0.5
        local z = cz + math.random(-6, 6) + 0.5
        local g = iso.ground(x, z)
        if g and g < 16 and stand_height(x, g, z, m.k.r, m.k.height) == g then
            m.x, m.y, m.z, m.vy = x, g, z, 0
            iso.entity_move(m.id, x, g, z)
            return true
        end
    end
    return false
end

-- カーソルのまわりに村人と羊を出す(チャンクを読み込んでから呼ぶ)
function M.spawn(cx, cz)
    M.clear()
    if not img then return end
    for name, k in pairs(KINDS) do
        for _ = 1, k.n do
            local id = iso.entity_add(img, cx + 0.5, 0, cz + 0.5,
                { sx = k.sx, sy = k.sy, w = k.w, h = k.h, r = k.r, height = k.height })
            if id then
                local m = { id = id, k = k, name = name, frame = 1, anim = 0 }
                if place(m, cx, cz) then
                    new_dir(m)
                    mobs[#mobs + 1] = m
                else
                    iso.entity_remove(id)
                end
            end
        end
    end
end

-- ワールドを閉じると、エンジンの人や物は片付く(こちらの表も空にする)
function M.clear()
    for _, m in ipairs(mobs) do iso.entity_remove(m.id) end
    mobs = {}
end

function M.update(dt_ms, cx, cz)
    local dt = math.min(dt_ms, 100) / 1000
    for _, m in ipairs(mobs) do
        local k = m.k
        if math.abs(m.x - cx) + math.abs(m.z - cz) > FAR then place(m, cx, cz) end
        local moving = false
        if m.wait > 0 then
            m.wait = m.wait - dt
        else
            m.walk = m.walk - dt
            if m.walk <= 0 then new_dir(m) end
            local nx, nz = m.x + m.dx * k.speed * dt, m.z + m.dz * k.speed * dt
            local g = stand_height(nx, m.y, nz, k.r, k.height)
            if g and m.vy == 0 then
                m.x, m.z = nx, nz
                if g > m.y then m.y = g end   -- 1段なら登る
                moving = true
            elseif not g then
                new_dir(m)                    -- 壁・水・崖の外: 向きを変える
            end
        end
        -- 落ちる・跳ねる
        local gnd = iso.ground(m.x, m.z, m.y + 0.01) or -1
        if m.y > gnd or m.vy > 0 then
            m.vy = m.vy - GRAVITY * dt
            m.y = m.y + m.vy * dt
            if m.y <= gnd then m.y, m.vy = gnd, 0 end
        end
        iso.entity_move(m.id, m.x, m.y, m.z)
        -- 歩くコマ
        if k.frames then
            m.anim = m.anim + dt
            local f = moving and (math.floor(m.anim / 0.18) % #k.frames + 1) or 1
            if f ~= m.frame then
                m.frame = f
                iso.entity_set(m.id, { sx = k.frames[f] })
            end
        end
    end
end

-- 画面の点に村人か羊がいれば跳ねさせる(いれば true)
function M.tap(px, py)
    local id = iso.entity_at(px, py)
    if not id then return false end
    for _, m in ipairs(mobs) do
        if m.id == id then
            if m.vy == 0 then m.vy = JUMP end
            pico.sound_play(2, m.name == "sheep" and 520 or 700, 60, { wave = "square", volume = 6 })
            return true
        end
    end
    return false
end

function M.count() return #mobs end

return M
