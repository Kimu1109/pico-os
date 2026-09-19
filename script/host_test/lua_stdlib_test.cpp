// lib/lua(vendorしたLua 5.4.7本体)の言語機能・標準ライブラリが一通り動くことを
// 確認するテスト。lua_smoke_test.cppは「ビルド・リンクできてlua_newstateが動く」
// ところまでだったので、こちらはLuaコードとして実際に書きそうなものを一通り
// 走らせて壊れていないかを見る(CLAUDE.md「Lua着手前の受け皿の状態」の
// 「ビルドの二重管理」で確認した「PCビルドでリンクできる」の先の検証)。
//
// pc/CMakeLists.txtと同じくLUA_USE_LINUX等は定義せずビルドする
// (RP2350実機はdlopen等のPOSIX機能を持たないため、実機と同じANSI構成で確認する)。
//
// check()はLuaスクリプト側からCへコールバックする形にしてある。これ自体が
// 「LuaからCの関数を呼べる」ことの確認も兼ねる(将来のWidgetProperty等の
// バインディングと同じ経路)。
#include "lua.hpp"
#include <cstdio>

static int failures = 0;

static int l_check(lua_State* L) {
    const bool cond = lua_toboolean(L, 1);
    const char* label = luaL_optstring(L, 2, "(no label)");
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if (!cond) failures++;
    return 0;
}

static const char* kScript = R"LUA(
-- 数値
check(1 + 2 == 3, "整数加算")
check(10 // 3 == 3, "整数除算(//)")
check(10 % 3 == 1, "剰余")
check(2 ^ 10 == 1024.0, "べき乗は浮動小数点数を返す")
check(math.type(1) == "integer", "リテラル1は integer 部分型")
check(math.type(1.0) == "float", "リテラル1.0は float 部分型")
check(math.floor(3.7) == 3, "math.floor")
check(math.max(1, 5, 3) == 5, "math.max")

-- 文字列
check(("hello"):upper() == "HELLO", "文字列メソッド構文(string.upper)")
check(string.format("%d-%s", 5, "x") == "5-x", "string.format")
check(string.find("pico-os", "os") == 6, "string.find")
check(string.gsub("a,b,c", ",", ";") == "a;b;c", "string.gsub")
check(#"あいう" == 9, "UTF-8はバイト数として#が返る(Luaは文字列をバイト列として扱う)")

-- テーブル
local t = {3, 1, 2}
table.sort(t)
check(t[1] == 1 and t[2] == 2 and t[3] == 3, "table.sort")
table.insert(t, 4)
check(#t == 4 and t[4] == 4, "table.insert")
check(table.concat({"a", "b", "c"}, "-") == "a-b-c", "table.concat")

-- クロージャ(upvalue)
local function counter()
    local n = 0
    return function() n = n + 1; return n end
end
local c = counter()
check(c() == 1 and c() == 2 and c() == 3,
      "クロージャの外側変数(upvalue)が呼び出しをまたいで保持される")

-- メタテーブル(将来のウィジェットラッパーのようなOOP的な使い方)
local Animal = {}
Animal.__index = Animal
function Animal.new(name) return setmetatable({ name = name }, Animal) end
function Animal:greet() return "I am " .. self.name end
local a = Animal.new("pico")
check(a:greet() == "I am pico", "メタテーブル__indexによるメソッド呼び出し")

-- pcall/エラー処理(将来Luaアプリのエラーをここで捕まえてErrorFunctions::ShowFatal()へ渡す想定)
local ok, err = pcall(function() error("boom") end)
check(ok == false, "pcallはエラーを捕捉してfalseを返す")
check(tostring(err):find("boom") ~= nil, "エラーメッセージが伝わる")

-- コルーチン(将来Task化してStepBudgetと組み合わせる際の土台になりうる機能)
local co = coroutine.create(function(x)
    local y = coroutine.yield(x + 1)
    return y + 1
end)
local ok1, v1 = coroutine.resume(co, 10)
check(ok1 and v1 == 11, "coroutine: 1回目のresumeでyieldの値を受け取る")
local ok2, v2 = coroutine.resume(co, 20)
check(ok2 and v2 == 21, "coroutine: 2回目のresumeでreturnの値を受け取る")
check(coroutine.status(co) == "dead", "coroutine: 完了後はdead状態")

-- ガベージコレクション(大量生成後も落ちないことの確認)
for i = 1, 1000 do
    local junk = { i, i * 2, tostring(i) }
end
collectgarbage("collect")
check(true, "collectgarbage: 大量のテーブル生成・回収後もクラッシュしない")

return true
)LUA";

int main(){
    lua_State* L = luaL_newstate();
    if (!L) {
        printf("[FAIL] luaL_newstate failed\n");
        return 1;
    }
    luaL_openlibs(L);

    lua_pushcfunction(L, l_check);
    lua_setglobal(L, "check");

    if (luaL_dostring(L, kScript) != LUA_OK) {
        printf("[FAIL] スクリプト実行中に予期しないエラー: %s\n", lua_tostring(L, -1));
        failures++;
    }

    lua_close(L);

    printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
