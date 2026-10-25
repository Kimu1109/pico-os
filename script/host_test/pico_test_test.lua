-- pico_test.lua(picoos lua-test のテストランナー)自身のテスト。lua_script_test から run.sh が呼ぶ。
-- script/host_test/lua/test_app(小さなアプリ)のテストを動かし、成功/失敗の数と出力を確かめる。
-- 併せて pc/sdcard/lua/apps/*/tests/ の見本のテストが通ることも見る(壊れた見本を残さないため)。
local T = dofile(ROOT_DIR .. "/script/lua_test/pico_test.lua")
local fails = 0
local function check(c, m) if c then print("[ OK ] " .. m) else print("[FAIL] " .. m) fails = fails + 1 end end

local lines = {}
local pass, failed = T.run_files({ ROOT_DIR .. "/script/host_test/lua/test_app/tests/sample_test.lua" },
                                 { out = function(s) lines[#lines + 1] = s end })
local text = table.concat(lines, "\n")
print(text)
check(pass == 5 and failed == 2, "成功5件・失敗2件: " .. pass .. " / " .. failed)
check(text:find("eq: 期待 {a=1, b={3}} / 実際 {a=1, b={2}} (b.1 が違う)", 1, true) ~= nil, "eq の失敗の文")
check(text:find("sample_test.lua:%d+:") ~= nil, "失敗の場所はテストファイルの行")
check(text:find("ウィジェットが見つかりません", 1, true) ~= nil, "見つからないウィジェットの文")
check(T.find_app_dir(ROOT_DIR .. "/script/host_test/lua/test_app/tests/sample_test.lua")
      == ROOT_DIR .. "/script/host_test/lua/test_app", "テストファイルからアプリのフォルダを探す")

-- -k(名前の一部で絞る)
lines = {}
pass, failed = T.run_files({ ROOT_DIR .. "/script/host_test/lua/test_app/tests/sample_test.lua" },
                           { filter = "サンドボックス", out = function(s) lines[#lines + 1] = s end })
check(pass == 1 and failed == 0, "filter で1件だけ")

-- 読み込みで落ちるテストファイル
local bad = ROOT_DIR .. "/script/host_test/build/pico_test_bad_test.lua"
local f = io.open(bad, "w")
if f then
    f:write("test('x', function() end)\nerror('こわれた')\n")
    f:close()
    lines = {}
    pass, failed = T.run_files({ bad }, { out = function(s) lines[#lines + 1] = s end })
    check(failed == 1 and table.concat(lines, "\n"):find("(読み込み)", 1, true), "読み込みの失敗を数える")
    os.remove(bad)
end

-- 見本のテスト
lines = {}
pass, failed = T.run_files({ ROOT_DIR .. "/pc/sdcard/lua/apps/動作確認/tests/menu_test.lua" },
                           { out = function(s) lines[#lines + 1] = s end })
print(table.concat(lines, "\n"))
check(pass == 4 and failed == 0, "見本(動作確認)のテストが通る")

if fails > 0 then os.exit(1) end
print("pico_test_test: OK")
