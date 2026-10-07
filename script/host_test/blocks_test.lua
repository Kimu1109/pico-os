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
    sd_remove = function(p) files[p] = nil return true end,
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
local B, N, H = world.B, world.N, world.H
check(E.mode() == "title", "起動するとワールドを選ぶ画面")

local function run_afters()
    while #afters > 0 do table.remove(afters, 1)() end
end

-- ---- 読み書き ----
world.clear()
check(world.get(5, 5, 5) == B.AIR, "消した直後は空気")
world.set(3, 9, 7, B.TNT)
check(world.get(3, 9, 7) == B.TNT and world.get(3, 9, 6) == B.AIR and world.get(3, 8, 7) == B.AIR,
      "1マスだけ書き換わる(上の段の8bit境界をまたいでも)")
check(world.get(-1, 0, 0) == 0 and world.get(0, H, 0) == 0 and world.get(0, 0, N) == 0, "世界の外は空気")
world.set(-1, 0, 0, B.STONE)   -- 外への書き込みは無視
world.fill(-2, 0, 46, 1, 1, 60, B.SAND)
check(world.get(0, 0, 46) == B.SAND and world.get(1, 1, 47) == B.SAND and world.get(2, 0, 46) == B.AIR,
      "fill は世界の外を切り捨てて塗る")
world.fill(0, 2, 0, N - 1, 2, N - 1, B.GOLD)
local all = true
for x = 0, N - 1 do for z = 0, N - 1 do if world.get(x, 2, z) ~= B.GOLD then all = false end end end
check(all, "高さ1段まるごとの fill")

-- ---- 生成 ----
math.randomseed(1234)
local px, py, pz = world.gen_natural()
local ok_bedrock, ok_water, grass, sand, ore, stone = true, true, 0, 0, 0, 0
for x = 0, N - 1 do
    for z = 0, N - 1 do
        if world.get(x, 0, z) ~= B.BEDROCK then ok_bedrock = false end
        for y = 1, H - 1 do
            local b = world.get(x, y, z)
            if b == B.WATER and y > 5 then ok_water = false end
            if b == B.GRASS then grass = grass + 1 end
            if b == B.SAND then sand = sand + 1 end
            if b == B.STONE then stone = stone + 1 end
            if b == B.COAL_ORE or b == B.IRON_ORE then ore = ore + 1 end
        end
    end
end
check(ok_bedrock, "自然: 一番下は全部岩盤")
check(ok_water, "自然: 水は水面(5)より上に無い")
check(grass > 100 and sand > 0, "自然: 草と水辺の砂がある (" .. grass .. ", " .. sand .. ")")
check(ore > 0 and ore < (ore + stone) * 0.2, "自然: 石のおよそ1割が鉱石 (" .. ore .. "/" .. (ore + stone) .. ")")
check(px == N // 2 and pz == N // 2 and world.get(px, py, pz) == B.AIR and world.get(px, py - 1, pz) ~= B.AIR,
      "自然: 始まりは真ん中の柱の一番下の空気")

local fx, fy, fz = world.gen_flat()
check(world.get(10, 0, 10) == B.GRASS and world.get(10, 1, 10) == B.AIR and fy == 1, "平ら: 草の床だけ")

world.gen_demo()
check(world.get(N - 1, 8, N - 1) == B.GOLD and world.get(N - 9, 1, N - 9) == B.GOLD, "デモ: 金のピラミッド")
check(world.get(18, 2, 3) == B.TNT and world.get(19, 1, 3) == B.BOOKS, "デモ: 家の中の TNT と本棚")
check(world.get(40, 3, 5) == B.WATER, "デモ: 池")

-- ---- 保存と読み込み ----
local before = {}
for y = 1, H do before[y] = world.L[y] end
check(world.save("/s/a.dat", { x = 3, y = 4, z = 5 }, B.BRICKS), "保存できる")
check(#files["/s/a.dat"] == 10 + N * N * H, "保存の大きさ: 見出し10バイト + 全マス")
world.clear()
local p, cur = world.load("/s/a.dat")
local same = p ~= nil
for y = 1, H do if world.L[y] ~= before[y] then same = false end end
check(same and p.x == 3 and p.y == 4 and p.z == 5 and cur == B.BRICKS, "読み込むと同じワールド・位置・ブロック")
files["/s/b.dat"] = files["/s/a.dat"]:sub(1, 5000)
check(world.load("/s/b.dat") == nil, "途中で切れたファイルは読まない")
files["/s/c.dat"] = "XXXX" .. files["/s/a.dat"]:sub(5)
check(world.load("/s/c.dat") == nil, "形式の違うファイルは読まない")
files["/s/d.dat"] = files["/s/a.dat"]:sub(1, 100) .. "\200" .. files["/s/a.dat"]:sub(102)
check(world.load("/s/d.dat") == nil, "知らないブロック番号の入ったファイルは読まない")
check(world.load("/s/none.dat") == nil, "無いファイルは nil")

-- ---- 描画 ----
local function render_all()
    draws = {}
    V.show_cursor = false
    V.render(0, 20, 240, 204)
    V.show_cursor = true
end
world.clear()
V.OX, V.OY = 100, 150
world.set(0, 0, 0, B.STONE)
render_all()
check(#draws == 3, "1個だけのブロックは3面を描く (" .. #draws .. ")")
local sy = (B.STONE - 1) * 23
local got = {}
for _, d in ipairs(draws) do got[#got + 1] = table.concat(d, ",") end
table.sort(got)
local want = { "100,150,0," .. sy .. ",32,15", "100,158,64," .. sy .. ",16,23", "116,158,96," .. sy .. ",16,23" }
table.sort(want)
check(table.concat(got, " ") == table.concat(want, " "), "上面(日なた)・左面(日なた)・右面の位置と絵")

-- 囲まれて見えない面は描かない
world.clear()
world.fill(0, 0, 0, 2, 2, 2, B.DIRT)
render_all()
check(#draws == 27, "3x3x3 の塊は見える面(9+9+9)だけ描く (" .. #draws .. ")")

-- 影: 太陽(-1,+1,+1)の方にブロックがあると、上面は影の絵(32)
world.clear()
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
check(V.sunlit(5, 3, 5) and not V.sunlit(5, 1, 5), "sunlit: 太陽の方をたどって遮るものを見つける")

-- 水: 空気に面した面だけ、水の段(0)で描く
world.clear()
world.fill(0, 0, 0, 1, 0, 1, B.WATER)
render_all()
local water_ok = #draws == 8
for _, d in ipairs(draws) do if d[4] ~= 0 then water_ok = false end end
check(water_ok, "2x2の水はくっついた面を描かない(上4+左2+右2) (" .. #draws .. ")")

-- 描く範囲の外のブロックは描かない
world.clear()
world.set(0, 0, 0, B.STONE)
world.set(N - 1, 0, 0, B.STONE)
draws = {}
V.show_cursor = false
V.render(100, 150, 32, 31)
V.show_cursor = true
check(#draws == 3, "矩形の外にあるブロックは描かない (" .. #draws .. ")")

-- ---- タップ位置の引き当て ----
world.clear()
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
world.clear()
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
    if d[3] > 32 or d[4] > 31 then big = true end
end
check(#dirties > 1 and not big, "置いたときは小さな矩形だけ描き直す (" .. #dirties .. "個)")

-- カーソル: 移動は世界の中に収まる
V.cx, V.cy, V.cz = 0, 0, 0
E.move(-1, -1, -1)
check(V.cx == 0 and V.cy == 0 and V.cz == 0, "カーソルは世界の外へ出ない")
E.move(1, 1, 1)
check(V.cx == 1 and V.cy == 1 and V.cz == 1, "カーソルが動く")

-- ---- 画面の流れ: 作る → 遊ぶ → 保存して戻る → 読む ----
E.slot(2)
E.start(1)
run_afters()
check(E.mode() == "play" and world.get(5, 0, 5) == B.GRASS, "平らなワールドを作って遊ぶ画面へ")
world.set(7, 3, 9, B.GOLD)
E.save_and_quit()
run_afters()
check(E.mode() == "title" and files["/app/worlds/world_B.dat"] ~= nil, "保存してワールドを選ぶ画面へ")
check(world.get(7, 3, 9) == B.AIR, "戻ると世界は空になる")
E.start(nil)
run_afters()
check(E.mode() == "play" and world.get(7, 3, 9) == B.GOLD, "保存したワールドを読んで続きから")
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
