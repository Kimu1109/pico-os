-- pico_mock.lua(Luaアプリのテスト用の偽物)自身のテスト。lua_script_test から run.sh が呼ぶ。
local mock = dofile(ROOT_DIR .. "/script/host_test/lua/pico_mock.lua")
local fails = 0
local function check(c, m) if c then print("[OK] " .. m) else print("[NG] " .. m) fails = fails + 1 end end

mock.install({ args = { level = 3 } })
check(pico.args().level == 3, "args")

-- ウィジェットとイベント
local b = pico.create("Button")
pico.set(b, "text", "go")
local hits = 0
pico.on(b, "press_end", function(id, x, y) hits = hits + x + y end)
mock.fire(b, "press_end", 3, 4)
check(hits == 7 and pico.get(b, "text") == "go", "set/get/on/fire")
check(pico.off(b, "press_end") and not pico.off(b, "press_end"), "off")

-- 名前・親子
local box = pico.create("LayoutContainer")
pico.add_child(box, b)
pico.set_name(box, "root")
check(pico.find("root") == box and pico.parent(b) == box and #pico.children(box) == 1, "名前と親子")
pico.destroy(b)
check(#pico.children(box) == 0, "destroyで親から外れる")

-- リスト
local sl = pico.create("ScrollList")
pico.list_add(sl, "a"); pico.list_add(sl, "c"); pico.list_insert(sl, 1, "b")
check(pico.list_get(sl, 1) == "b" and pico.get(sl, "item_count") == 3, "リスト")
check(pico.list_remove(sl, 0) and pico.list_get(sl, 0) == "b", "list_remove")

-- タイマー
local ticks = {}
pico.every(100, function() ticks[#ticks + 1] = mock.clock end)
pico.after(250, function() ticks[#ticks + 1] = "after" end)
mock.advance(350)
check(table.concat(ticks, ",") == "100,200,after,300", "advanceでafter/everyが時間順に鳴る: " .. table.concat(ticks, ","))

-- ダイアログ
local result
local d = pico.show_message("ok?", "no", "yes")
pico.on(d, "closed", function(_, ok) result = ok end)
mock.close_dialog(d, true)
check(result == true and mock.widgets[d] == nil, "ダイアログを閉じる")

-- HTTP
local got
pico.http_request("GET", "http://x/", nil, nil, function(ok, st, body) got = { ok, st, body } end)
mock.respond(1, true, 200, "hi")
check(got and got[1] and got[2] == 200 and got[3] == "hi", "HTTPの応答")

-- SD・保存・暗号
pico.sd_write("/a.txt", "x"); pico.sd_write("/a.txt", "y", true)
check(pico.sd_read("/a.txt") == "xy" and pico.sd_exists("/a.txt"), "sd")
check(#pico.sd_list("/") == 1, "sd_list")
check(pico.decrypt(pico.encrypt("s", "pw"), "pw") == "s" and pico.decrypt(pico.encrypt("s", "pw"), "no") == nil, "encrypt/decrypt")
check(pico.path_join("/a", "b", "c") == "/a/b/c", "path_join")

-- 画面遷移の記録
pico.push_scene("/x.lua", { a = 1 })
check(mock.scene.kind == "push" and mock.scene.args.a == 1, "push_scene")
pico.on_back(function() return false end)
pico.go_back()
check(mock.popped ~= nil, "on_backがfalseならpop")

if fails > 0 then os.exit(1) end
