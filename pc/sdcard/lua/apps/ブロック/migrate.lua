-- ブロック: 前の版の保存(48x16x48 を1ファイル、"BLK1")を、チャンクに分けた今の形(K=6)へ移す。
-- 前の版のワールドを開くときだけ読み込む(game.lua)。migrate(古いファイル, 新しいディレクトリ) → 成否, 理由
local world = require("world")
local byte, sub = string.byte, string.sub

return function(old, d)
    local H = world.H
    local head = pico.sd_read_part(old, 0, 10)
    if not head or #head < 10 or sub(head, 1, 4) ~= "BLK1" then return false, "形式が違います" end
    local n, h, px, py, pz, cur = byte(head, 5, 10)
    if n ~= 48 or h ~= H then return false, "大きさが違います" end
    local layers = {}
    for y = 1, H do
        local s = pico.sd_read_part(old, 10 + (y - 1) * 2304, 2304)
        if not s or #s ~= 2304 or s:find("[\25-\255]") then return false, "壊れています" end
        layers[y] = s
    end
    -- 前の版は高さごとに (x, z) が x*48+z+1 バイト目。チャンクは y*64 + lx*8 + lz + 1 バイト目
    local chunks = {}
    local air = string.rep("\0", 64)
    for cx = 0, 5 do
        for cz = 0, 5 do
            local lay = {}
            for y = 1, H do
                local parts = {}
                for lx = 0, 7 do
                    local i = (cx * 8 + lx) * 48 + cz * 8 + 1
                    parts[lx + 1] = sub(layers[y], i, i + 7)
                end
                lay[y] = table.concat(parts)
            end
            local t = H
            while t > 0 and lay[t] == air do t = t - 1 end
            chunks[cx * 6 + cz] = table.concat(lay, "", 1, t)
        end
    end
    layers = nil
    pico.sd_mkdir(d)
    world.adopt(d, 6, chunks)
    local ok = world.save({ x = px, y = py, z = pz }, cur)
    world.close()
    if not ok then return false, "書き出せません" end
    pico.sd_remove(old)
    return true
end
