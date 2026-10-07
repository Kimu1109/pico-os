-- Luaアプリ「ブロック」(pc/sdcard/lua/apps/ブロック/。Blocks-TI-84 の移植)の中身を確かめる:
-- ワールドの生成(自然/平ら/デモ)、ブロックの読み書き、保存と読み込みの往復と壊れたファイルの拒否、
-- 描画(見えない面を描かない・影・水・描く範囲の絞り込み)、タップ位置の引き当て、置く/壊すの規則。
-- pico.* は描画を記録するだけの偽物と、メモリ上のSDに差し替える。lua_script_test から run.sh が呼ぶ。
-- 見た目はPCビルドの --tap / --shot で確かめること。
local APP = ROOT_DIR .. "/pc/sdcard/lua/apps/ブロック/"
local fails = 0
local function check(c, msg)
    print((c and "[ OK ] " or "[FAIL] ") .. msg)
    if not c then fails = fails + 1 end
end

-- ---- 偽物の pico ----
local files = {}
local draws = {}
local dirties = {}
local handlers = {}
local afters = {}
local nid = 0
local clip = { 0, 20, 240, 204 }
pico = setmetatable({
    content_rect = function() return 0, 20, 240, 300 end,
    create = function() nid = nid + 1 return nid end,
    on = function(id, ev, fn) handlers[id .. ev] = fn end,
    after = function(_, fn) afters[#afters + 1] = fn return #afters end,
    millis = function() return 0 end,
    pad_down = function() return false end,
    pad_pressed = function() return false end,
    get_touch = function() return 0, 0, false end,
    image_load = function() return 1 end,
    app_dir = function() return "/app" end,
    path_join = function(...) return table.concat({ ... }, "/") end,
    memory_info = function() return { lua_used = collectgarbage("count") * 1024, lua_budget = 204800 } end,
    get_draw_area = function() return table.unpack(clip) end,
    draw_image_part = function(_, x, y, sx, sy, w, h) draws[#draws + 1] = { x, y, sx, sy, w, h } end,
    mark_dirty = function(x, y, w, h) dirties[#dirties + 1] = { x, y, w, h } end,
    sd_exists = function(p) return files[p] ~= nil end,
    sd_write = function(p, s, append)
        files[p] = (append and files[p] or "") .. s
        return true
    end,
    sd_read_part = function(p, off, len)
        local s = files[p]
        if not s then return nil end
        return s:sub(off + 1, off + len)
    end,
    sd_remove = function(p)
        files[p] = nil
        for k in pairs(files) do if k:sub(1, #p + 1) == p .. "/" then files[k] = nil end end
        return true
    end,
}, { __index = function() return function() end end })

function require(n) return dofile(APP .. n .. ".lua") end
local loaded = {}
local real_require = require
function require(n)
    if not loaded[n] then loaded[n] = real_require(n) end
    return loaded[n]
end

TEST = {}
dofile(APP .. "main.lua")
local E = TEST.env
local world, V = E.world, E.V
local B, H = world.B, world.H
check(E.mode() == "title", "起動するとワールドを選ぶ画面")

local function run_afters()
    while #afters > 0 do table.remove(afters, 1)() end
end

-- 小さな空のワールド(K=6、48x48)を作る。描画・引き当て・置く/壊すの確かめに使う
local function empty_world()
    world.create("/w/t", 3, 0, 6)
    for cx = 0, 5 do for cz = 0, 5 do world.load_chunk(cx, cz) end end
end
local function fill(x0, y0, z0, x1, y1, z1, b)
    for x = x0, x1 do for y = y0, y1 do for z = z0, z1 do world.set(x, y, z, b) end end end
end
local function ld(x, z) world.load_chunk(x >> 3, z >> 3) end

-- ---- 読み書き ----
empty_world()
check(world.W == 48 and world.get(5, 5, 5) == B.AIR, "空のワールドは空気")
world.set(3, 9, 7, B.TNT)
check(world.get(3, 9, 7) == B.TNT and world.get(3, 9, 6) == B.AIR and world.get(3, 8, 7) == B.AIR,
      "1マスだけ書き換わる")
check(#world.chunk(0, 0) == 64 * 10, "チャンクの文字列は書いた高さまでの長さ (" .. #world.chunk(0, 0) .. ")")
world.set(3, 9, 7, B.AIR)
check(world.chunk(0, 0) == "", "空気に戻すと上の空気の段を落とす")
world.set(7, 2, 8, B.GOLD); world.set(8, 2, 7, B.SAND)
check(world.get(7, 2, 8) == B.GOLD and world.get(8, 2, 7) == B.SAND and world.get(8, 2, 8) == B.AIR,
      "チャンクの境目をまたいでも正しく読み書きできる")
check(world.get(-1, 0, 0) == 0 and world.get(0, H, 0) == 0 and world.get(0, 0, world.W) == 0
      and world.get(0, -1, 0) == 0, "世界の外は空気")
world.set(-1, 0, 0, B.STONE)   -- 外への書き込みは無視
check(world.chunk(-1, 0) == nil, "世界の外にはチャンクが無い")

-- ---- 生成 ----
world.create("/w/n", 0, 1234)
check(world.W == 1024 and world.K == 128, "新しいワールドは 1024x1024 (128x128 チャンク)")
local g1 = world.generate(40, 51)
world.create("/w/n", 0, 1234)
check(world.generate(40, 51) == g1, "同じ種・同じ位置のチャンクはいつ作っても同じ")
world.create("/w/n", 0, 99)
check(world.generate(40, 51) ~= g1, "種が違えば違う地形")
local px, py, pz = world.create("/w/n", 0, 1234)
for cx = 56, 71 do for cz = 56, 71 do world.load_chunk(cx, cz) end end
local ok_bedrock, ok_water, grass, sand, ore, stone, wood = true, true, 0, 0, 0, 0, 0
for x = 448, 575 do
    for z = 448, 575 do
        if world.get(x, 0, z) ~= B.BEDROCK then ok_bedrock = false end
        for y = 1, H - 1 do
            local b = world.get(x, y, z)
            if b == B.WATER and y > 5 then ok_water = false end
            if b == B.GRASS then grass = grass + 1 end
            if b == B.SAND then sand = sand + 1 end
            if b == B.STONE then stone = stone + 1 end
            if b == B.WOOD then wood = wood + 1 end
            if b == B.COAL_ORE or b == B.IRON_ORE then ore = ore + 1 end
        end
    end
end
check(ok_bedrock, "自然: 一番下は全部岩盤")
check(ok_water, "自然: 水は水面(5)より上に無い")
check(grass > 1000 and sand > 0, "自然: 草と水辺の砂がある (" .. grass .. ", " .. sand .. ")")
check(ore > 0 and ore < (ore + stone) * 0.2, "自然: 石のおよそ1割が鉱石 (" .. ore .. "/" .. (ore + stone) .. ")")
check(wood > 0, "自然: 木がある (" .. wood .. ")")
check(px == 512 and pz == 512 and world.get(px, py, pz) == B.AIR and world.get(px, py - 1, pz) ~= B.AIR,
      "自然: 始まりは真ん中の柱の一番下の空気")
-- 高さはチャンクの境目でもつながる(格子の補間)
local smooth = true
for z = 448, 575 do
    if math.abs(world.height(463, z) - world.height(464, z)) > 2 then smooth = false end
end
check(smooth, "自然: チャンクの境目で高さが飛ばない")

local fx, fy, fz = world.create("/w/f", 1, 5)
ld(10, 10); ld(900, 900)
check(world.get(10, 0, 10) == B.GRASS and world.get(10, 1, 10) == B.AIR and fy == 1
      and world.get(900, 0, 900) == B.GRASS, "平ら: どこまでも草の床")

world.create("/w/d", 2, 5)
world.demo = require("demo")
for cx = 0, 5 do for cz = 0, 5 do world.load_chunk(cx, cz) end end
ld(200, 200)
check(world.get(47, 8, 47) == B.GOLD and world.get(39, 1, 39) == B.GOLD, "デモ: 金のピラミッド")
check(world.get(18, 2, 3) == B.TNT and world.get(19, 1, 3) == B.BOOKS, "デモ: 家の中の TNT と本棚")
check(world.get(40, 3, 5) == B.WATER, "デモ: 池")
check(world.get(8, 6, 7) == B.WOOD and world.get(8, 7, 7) == B.LEAVES, "デモ: 木(チャンクの境目にかかる葉も)")
check(world.get(6, 4, 5) == B.LEAVES and world.get(10, 4, 9) == B.LEAVES, "デモ: 隣のチャンクへはみ出した葉")
check(world.get(200, 0, 200) == B.GRASS and world.get(200, 1, 200) == B.AIR, "デモ: 遠くは平らな草")

-- ---- 読み込みの範囲(チャンクの読み込みと手放し) ----
world.create("/w/v", 0, 7)
local function pump_all() while world.pending() > 0 do world.pump(6) end end
world.window(-20, 20, 1000, 1080)      -- (512, 512) のあたり
pump_all()
local k1 = world.loaded_count()
check(k1 > 10 and k1 < 80, "見える範囲のチャンクだけ読み込む (" .. k1 .. "個)")
check(world.chunk(64, 64) ~= nil and world.chunk(0, 0) == nil, "遠くのチャンクは読み込まない")
local ny = world.first_air(515, 517)
world.set(515, ny, 517, B.GOLD)
world.window(-20, 20, 200, 280)        -- 遠くへ動かす
check(world.chunk(64, 64) == nil, "範囲から外れたチャンクは手放す")
check(files["/w/v/c_64_64.dat"] ~= nil, "書き換えたチャンクは手放すときに書き出す")
check(files["/w/v/c_63_63.dat"] == nil, "書き換えていないチャンクは書き出さない")
pump_all()
check(world.get(515, ny, 517) == B.AIR, "読み込んでいないチャンクは空気")
world.window(-20, 20, 1000, 1080)
pump_all()
check(world.get(515, ny, 517) == B.GOLD, "戻ると書き換えた内容をファイルから読む")
local _, bytes = world.loaded_count()
check(bytes < 60000, "読み込んでいるチャンクは60KB未満 (" .. bytes .. ")")

-- ---- 保存と読み込み ----
world.set(520, 9, 520, B.BRICKS)
check(world.save({ x = 515, y = 4, z = 517 }, B.BRICKS), "保存できる")
check(files["/w/v/world.dat"] and #files["/w/v/world.dat"] == 17 + 128 * 128 // 8,
      "見出し17バイト + チャンクの印")
world.close()
check(world.chunk(64, 64) == nil and world.get(515, ny, 517) == 0, "閉じると何も持たない")
local p, cur = world.open("/w/v")
check(p and p.x == 515 and p.y == 4 and p.z == 517 and cur == B.BRICKS and world.W == 1024,
      "開くと同じ位置・ブロック・大きさ")
ld(515, 517); ld(520, 520)
check(world.get(515, ny, 517) == B.GOLD and world.get(520, 9, 520) == B.BRICKS, "書き換えた内容が残っている")
local fresh = world.generate(10, 10)
ld(80, 80)
check(world.chunk(10, 10) == fresh, "書き換えていないチャンクは種から作り直す")
local kind, w = world.info("/w/v/world.dat")
check(kind == 0 and w == 1024, "見出しから種類と大きさを読める")
-- 壊れたチャンクのファイルは読まずに作り直す
files["/w/v/c_65_65.dat"] = "\200" .. string.rep("\0", 63)
world.close(); world.open("/w/v")
world.set(523, 1, 523, B.TNT); world.save({ x = 0, y = 0, z = 0 }, 2)  -- c_65_65 の印を立てる
files["/w/v/c_65_65.dat"] = "\200" .. string.rep("\0", 63)
world.close(); world.open("/w/v")
ld(523, 523)
check(world.chunk(65, 65) == world.generate(65, 65), "壊れたチャンクのファイルは読まずに作り直す")
files["/w/x/world.dat"] = "XXXX" .. files["/w/v/world.dat"]:sub(5)
check(world.open("/w/x") == nil, "形式の違うファイルは読まない")
files["/w/y/world.dat"] = files["/w/v/world.dat"]:sub(1, 100)
check(world.open("/w/y") == nil, "途中で切れたファイルは読まない")
check(world.open("/w/none") == nil, "無いワールドは nil")

-- ---- 前の版(48x48 を1ファイル)からの移し替え ----
do
    local layers = {}
    for y = 0, H - 1 do
        local t = {}
        for x = 0, 47 do for z = 0, 47 do
            t[#t + 1] = string.char((y == 0) and B.BEDROCK or ((x * 7 + z * 3 + y) % 9 == 0 and y < 6) and B.STONE or 0)
        end end
        layers[#layers + 1] = table.concat(t)
    end
    files["/old.dat"] = "BLK1" .. string.char(48, 16, 3, 4, 5, B.GOLD) .. table.concat(layers)
    local okm = require("migrate")("/old.dat", "/w/m")
    check(okm and files["/old.dat"] == nil, "前の版の保存を移して元のファイルを消す")
    local pm, cm = world.open("/w/m")
    check(pm and pm.x == 3 and pm.y == 4 and pm.z == 5 and cm == B.GOLD and world.W == 48, "移したワールドを開ける")
    for cx = 0, 5 do for cz = 0, 5 do world.load_chunk(cx, cz) end end
    local same = true
    for x = 0, 47 do for z = 0, 47 do for y = 0, H - 1 do
        local want = string.byte(layers[y + 1], x * 48 + z + 1)
        if world.get(x, y, z) ~= want then same = false end
    end end end
    check(same, "移したワールドの中身が元と同じ")
end

-- ---- 描画 ----
local function render_all()
    draws = {}
    V.show_cursor = false
    V.render(0, 20, 240, 204)
    V.show_cursor = true
end
empty_world()
V.OX, V.OY = 100, 150
world.set(0, 0, 0, B.STONE)
render_all()
check(#draws == 3, "1個だけのブロックは3面を描く (" .. #draws .. ")")
local sy = (B.STONE - 1) * 23
local got = {}
for _, d in ipairs(draws) do got[#got + 1] = table.concat(d, ",") end
table.sort(got)
local want = { "100,150,0," .. sy .. ",32,15", "100,158,128," .. sy .. ",16,23", "116,158,192," .. sy .. ",16,23" }
table.sort(want)
check(table.concat(got, " ") == table.concat(want, " "), "上面(日なた)・左面(日なた)・右面の位置と絵")

-- 囲まれて見えない面は描かない
empty_world()
fill(0, 0, 0, 2, 2, 2, B.DIRT)
render_all()
check(#draws == 27, "3x3x3 の塊は見える面(9+9+9)だけ描く (" .. #draws .. ")")

-- 影: 太陽(-1,+1,+1)の方にブロックがあると、上面は影の絵(32)
empty_world()
world.set(5, 0, 5, B.STONE)
world.set(4, 2, 6, B.STONE)     -- (5,1,5) から太陽の方へ1歩
draws = {}
V.render(0, 20, 240, 204)
local top_shadow = false
local bx, by = V.block_pos(5, 0, 5)
for _, d in ipairs(draws) do
    if d[1] == bx and d[2] == by and d[3] == 32 then top_shadow = true end
end
check(top_shadow, "日の当たらない上面は影の絵で描く")

-- 元と同じ「面を2つの三角形に分けた影」: 遮るマスの位置で奥/手前・上/下の片方だけが影になる
local function shadow_case(occ, fn, x, y, z)
    empty_world()
    world.set(5, 3, 5, B.STONE)
    world.set(occ[1], occ[2], occ[3], B.DIRT)
    V.top = H - 1
    return fn(5, 3, 5)
end
local f, n = shadow_case({5, 4, 6}, V.top_shadow)
check(f and not n, "上面: 奥 (x, y+1, z+1) のブロックは奥半分だけを影にする")
f, n = shadow_case({4, 4, 5}, V.top_shadow)
check(n and not f, "上面: 左 (x-1, y+1, z) のブロックは手前半分だけを影にする")
f, n = shadow_case({4, 4, 6}, V.top_shadow)
check(f and n, "上面: (x-1, y+1, z+1) のブロックは全部を影にする")
f, n = shadow_case({2, 7, 8}, V.top_shadow)
check(f and n, "上面: 太陽の方へ遠く (3歩先) のブロックも影を落とす")
f, n = shadow_case({5, 2, 5}, V.top_shadow)
check(not f and not n, "上面: 下のブロックは影を落とさない")
local u, w = shadow_case({4, 4, 5}, V.left_shadow)
check(u and not w, "左面: (x-1, y+1, z) のブロックは上半分だけを影にする")
u, w = shadow_case({4, 3, 6}, V.left_shadow)
check(w and not u, "左面: (x-1, y, z+1) のブロックは下半分だけを影にする")
u, w = shadow_case({3, 4, 6}, V.left_shadow)
check(u and w, "左面: (x-2, y+1, z+1) のブロックは全部を影にする")
empty_world(); world.set(5, 3, 5, B.STONE); world.set(4, 4, 6, B.WATER); V.top = H - 1
f, n = V.top_shadow(5, 3, 5)
check(not f and not n, "水は影を落とさない")

-- 半分の影の絵: 奥半分の影は x=64、手前半分は x=96、左面の上半分は x=160
empty_world()
world.set(5, 3, 5, B.STONE); world.set(5, 4, 6, B.DIRT)
draws = {}
V.show_cursor = false
V.render(0, 20, 240, 204)
V.show_cursor = true
bx, by = V.block_pos(5, 3, 5)
local half_top = false
for _, d in ipairs(draws) do
    if d[1] == bx and d[2] == by and d[3] == 64 and d[4] == sy then half_top = true end
end
check(half_top, "奥半分だけ影の上面は、奥半分が影の絵 (x=64) で描く")

-- 水: 空気に面した面だけ、水の段(0)で描く
empty_world()
fill(0, 0, 0, 1, 0, 1, B.WATER)
render_all()
local water_ok = #draws == 8
for _, d in ipairs(draws) do if d[4] ~= 0 then water_ok = false end end
check(water_ok, "2x2の水はくっついた面を描かない(上4+左2+右2) (" .. #draws .. ")")

-- 水面(真上が水でない水)は元の WATER_HALF と同じく2px低い: 上面は2px下、横の面は上2行を抜いた絵
local function water_draws(x, y, z)
    local bx2, by2 = V.block_pos(x, y, z)
    local got2 = {}
    for _, d in ipairs(draws) do
        if d[4] == 0 and d[1] >= bx2 and d[1] < bx2 + 32 and d[2] >= by2 and d[2] < by2 + 31 then
            got2[#got2 + 1] = (d[1] - bx2) .. "," .. (d[2] - by2) .. "," .. d[3]
        end
    end
    table.sort(got2)
    return table.concat(got2, " ")
end
empty_world()
world.set(0, 0, 0, B.WATER)
render_all()
check(water_draws(0, 0, 0) == "0,2,0 0,8,144 16,8,160", "水面: 上面は2px下げ、横の面は水面用の絵 (" .. water_draws(0, 0, 0) .. ")")
world.set(0, 1, 0, B.WATER)
render_all()
check(water_draws(0, 0, 0) == "0,8,128 16,8,192", "真上も水なら下の水は普通の横の面 (" .. water_draws(0, 0, 0) .. ")")

-- 描く範囲の外のブロックは描かない
empty_world()
world.set(0, 0, 0, B.STONE)
world.set(world.W - 1, 0, 0, B.STONE)
draws = {}
V.show_cursor = false
V.render(100, 150, 32, 31)
V.show_cursor = true
check(#draws == 3, "矩形の外にあるブロックは描かない (" .. #draws .. ")")

-- ---- タップ位置の引き当て ----
empty_world()
world.set(4, 2, 6, B.STONE)
bx, by = V.block_pos(4, 2, 6)
local x, y, z, f = V.pick(bx + 16, by + 7)
check(x == 4 and y == 2 and z == 6 and f == "top", "上面のタップ")
x, y, z, f = V.pick(bx + 4, by + 18)
check(f == "left", "左面のタップ")
x, y, z, f = V.pick(bx + 28, by + 18)
check(f == "right", "右面のタップ")
check(V.pick(bx - 30, by - 40) == nil, "何も無い所は nil")
world.set(4, 3, 6, B.DIRT)        -- 上に積むと、上のブロックが手前
x, y, z, f = V.pick(bx + 16, by - 9)
check(y == 3, "重なっていれば手前(上)のブロック")

-- ---- 置く/壊す ----
empty_world()
V.cx, V.cy, V.cz = 10, 1, 10
E.set_cur(B.PLANKS)
E.act()
check(world.get(10, 1, 10) == B.PLANKS, "空気の所に置く")
E.act()
check(world.get(10, 1, 10) == B.AIR, "ブロックの所で押すと壊す")
world.set(10, 1, 10, B.WATER)
E.act()
check(world.get(10, 1, 10) == B.PLANKS, "水の所に置くと置き換える")
E.set_cur(B.WATER)
E.act()
check(world.get(10, 1, 10) == B.AIR, "水を持っているときはブロックを消す")
E.act()
check(world.get(10, 1, 10) == B.WATER, "水を持って空気の所で押すと水を置く")

-- 置いたときの描き直し: そのブロック + 影が変わりうる面だけ(全画面ではない)
dirties = {}
V.OX, V.OY = 120, 200
V.dirty_edit(10, 4, 10)
local area, big = 0, false
for _, d in ipairs(dirties) do
    area = area + d[3] * d[4]
    if d[3] > 64 or d[4] > 63 then big = true end
end
check(#dirties > 1 and not big, "置いたときは小さな矩形だけ描き直す (" .. #dirties .. "個)")

-- カーソル: 移動は世界の中に収まる
V.cx, V.cy, V.cz = 0, 0, 0
E.move(-1, -1, -1)
check(V.cx == 0 and V.cy == 0 and V.cz == 0, "カーソルは世界の外へ出ない")
E.move(1, 1, 1)
check(V.cx == 1 and V.cy == 1 and V.cz == 1, "カーソルが動く")

-- ---- 画面の流れ: 作る → 遊ぶ → 保存して戻る → 読む ----
local function frames(n) for _ = 1, n do loop(16) end end
E.slot(2)
E.start(1)
run_afters()
check(E.mode() == "load", "作ったらチャンクを読み込む画面")
frames(20)
check(E.mode() == "play" and world.get(512, 0, 512) == B.GRASS, "読み込み終わると遊ぶ画面へ")
local nk = world.loaded_count()
check(nk > 10 and nk < 120, "遊ぶ画面で読み込んでいるチャンク (" .. nk .. "個)")
V.cx, V.cy, V.cz = 513, 3, 511
E.act()
check(world.get(513, 3, 511) ~= B.AIR, "置ける")
E.save_and_quit()
run_afters()
check(E.mode() == "title" and files["/app/worlds/B/world.dat"] ~= nil, "保存してワールドを選ぶ画面へ")
check(world.get(513, 3, 511) == B.AIR, "戻ると世界は空になる")
E.start(nil)
run_afters()
frames(20)
check(E.mode() == "play" and world.get(513, 3, 511) ~= B.AIR, "保存したワールドを読んで続きから")
-- 視点を遠くへ動かすと、新しい所のチャンクを少しずつ読み込む
for _ = 1, 20 do E.move(1, 0, 1) end
frames(1)
check(world.pending() > 0 or world.chunk(66, 66) ~= nil, "視点を動かすと新しい所を読み込み始める")
frames(30)
check(world.pending() == 0 and world.chunk(66, 66) ~= nil, "少しずつ読み込み終わる")
E.open_select()
check(E.mode() == "select", "ブロックを選ぶ画面")
E.choose(3)
check(E.mode() == "play", "選ぶと遊ぶ画面へ戻る")

-- main.lua は16KiBまで
local f = assert(io.open(APP .. "main.lua", "rb"))
local size = #f:read("a")
f:close()
check(size <= 16384, "main.lua は16KiB以内 (" .. size .. ")")

print(fails == 0 and "blocks_test: 全部通りました" or ("blocks_test: " .. fails .. " 件失敗"))
if fails > 0 then os.exit(1) end
