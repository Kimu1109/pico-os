-- テトリス(pc/sdcard/lua/apps/テトリス/)のゲームの規則を、pico.* を差し替えて確かめる。
-- lua_script_test(vendorしたLuaでこのファイルを動かすだけの下請け)から run.sh が呼ぶ。
-- 見た目はPCビルドの --tap / --shot で確かめること(ここでは描画は数えるだけ)。
-- main.lua の末尾へ TEST(ローカル変数を覗く口)を足してから読み込む
local ROOT = ROOT_DIR .. "/pc/sdcard"
local cbs, nid, files_written = {}, 0, {}
local pad = {}
local touch = {0,0}
local draws = 0
pico = {}
function pico.content_rect() return 0, 20, 240, 300 end
function pico.sd_read(p)
  if files_written[p] then return files_written[p] end
  local f = io.open(ROOT..p, "rb"); if not f then return nil end
  local s = f:read("a"); f:close(); return s
end
function pico.sd_write(p, s) files_written[p] = s return true end
function pico.image_load(p) return pico.sd_read(p) and 1 or nil end
function pico.log(s) print("LOG", s) end
function pico.create(t) nid = nid + 1; cbs[nid] = {type=t}; return nid end
function pico.set(id,k,v) cbs[id][k]=v end
function pico.on(id,ev,fn) cbs[id][ev]=fn end
for _,n in ipairs{"fill_rect","draw_rect","draw_text","draw_line","draw_circle","draw_image_part","mark_dirty","invalidate"} do
  pico[n] = function() draws = draws + 1 end
end
function pico.get_draw_area() return 0,0,0,0 end
function pico.get_touch() return touch[1], touch[2], true end
function pico.pad_connected() return true end
function pico.pad_down(n) return pad[n] or false end
local sounds = 0
function pico.sound_play() sounds = sounds + 1 return true end
local music_on = false
function pico.music_play(p) music_on = true return pico.sd_read(p) ~= nil end
function pico.music_stop() music_on = false end
local popped = false
function pico.pop() popped = true end

local src = assert(io.open(ROOT.."/lua/apps/テトリス/main.lua")):read("a")
assert(#src < 16384, "main.lua must be under 16KiB: "..#src)
src = src .. [[
TEST = { g = g, spawn = spawn, S = function() return state, score, lines, level, p, rot, px, py, hold, hiscore end }
]]
assert(load(src, "main"))()
local fails = 0
local function check(c, m) if c then print("[OK] "..m) else print("[NG] "..m) fails = fails + 1 end end

-- 描画コールバックが全部エラー無く走る
for id, w in pairs(cbs) do if w.render then w.render() end end
check(draws > 0, "render callbacks run")
local board = 1
local function S() return TEST.S() end
local function step(n, ms) for i=1,n do loop(ms or 16) end end
local function pressPad(name) pad[name]=true; step(1); pad[name]=nil; step(1) end

check(S() == "title", "starts at title")
cbs[board].press_start()
check(S() == "play", "board tap starts the game")
check(music_on, "bgm starts")
step(1)
-- 左に押し続ける → 壁まで
pad.left = true; step(40); pad.left = nil; step(1)
local _,_,_,_,p,rot,px = S()
local minx = 9
local ROTS = nil
check(px <= 1, "DAS moves to the wall (px="..px..")")

-- HOLD
local _,_,_,_,p0 = S()
pressPad("l")
local st,_,_,_,p1,_,_,_,hold = S()
check(hold == p0 and p1 ~= nil, "hold stores current piece")
pressPad("l")
local _,_,_,_,p2,_,_,_,hold2 = S()
check(hold2 == p0 and p2 == p1, "hold only once per piece")

-- 1ライン消し: 最下段を4マス(3..6)残して埋め、Iを落とす
local g, W = TEST.g, 10
for i = 1, #g do g[i] = 0 end
for x = 0, 9 do if x < 3 or x > 6 then g[21*W + x + 1] = 5 end end
TEST.spawn(1)
pressPad("up")
local st, score, lines = S()
check(st == "clear" and lines == 1, "single line clear (state="..st..", lines="..lines..")")
step(20)
st = S()
check(st == "play", "back to play after clear animation")
local empty = true
for x = 0, 9 do if g[21*W + x + 1] ~= 0 then empty = false end end
check(empty, "bottom row empty after clear")

-- テトリス: 下4段を列5以外埋めて、縦にしたIを落とす
for i = 1, #g do g[i] = 0 end
for y = 18, 21 do for x = 0, 9 do if x ~= 5 then g[y*W + x + 1] = 3 end end end
local _, sc0 = S()
TEST.spawn(1)
pressPad("a")  -- 右回転: 縦で列px+2=5
local _,_,_,_,_,r,ppx = S()
check(r == 1 and ppx == 3, "I rotated to vertical at column 5")
pressPad("up")
local st2, sc1, lines2 = S()
check(lines2 == 5 and sc1 - sc0 >= 800, "tetris: 4 lines, +800 (score +"..(sc1-sc0)..")")
step(20)
local all_empty = true
for i = 1, #g do if g[i] ~= 0 then all_empty = false end end
check(all_empty, "board empty after tetris")

-- 壁蹴り: 右壁にくっつけたTを回転できる
TEST.spawn(3)
pad.right = true; step(40); pad.right = nil; step(1)
pressPad("b"); pressPad("b")
local _,_,_,_,_,rr = S()
check(rr == 2, "T rotates twice near wall (rot="..rr..")")

-- ゲームオーバー → ハイスコア保存
for y = 1, 3 do for x = 3, 6 do g[y*W + x + 1] = 2 end end
TEST.spawn(4)
local st3,sc3,_,_,_,_,_,_,_,hi = S()
check(st3 == "over", "spawn overlap -> game over")
check(files_written["/lua/apps/テトリス/hiscore.txt"] == tostring(sc3), "hiscore saved")
check(not music_on, "bgm stopped on game over")
pressPad("start")
check(S() == "play", "start restarts the game")

-- 一時停止
pressPad("start")
check(S() == "pause", "start pauses")
cbs[board].press_start()
check(S() == "play", "tap resumes")

-- タッチの操作ボタン: 左端のボタン(左移動)
local pad_id = 3
local _,_,_,_,_,_,pxa = S()
touch = {10, 300}; cbs[pad_id].press_start(); step(1); cbs[pad_id].press_end(); step(1)
local _,_,_,_,_,_,pxb = S()
check(pxb == pxa - 1, "touch left button moves left")
-- 重力: 落下中のミノが時間で下がる
local _,_,_,_,_,_,_,pya = S()
step(70)
local _,_,_,_,_,_,_,pyb = S()
check(pyb > pya, "gravity drops the piece")
for id, w in pairs(cbs) do if w.render then w.render() end end
pressPad("home")
check(popped, "home leaves the app")
print(fails == 0 and "ALL OK" or (fails.." FAILED"))
os.exit(fails == 0 and 0 or 1)
