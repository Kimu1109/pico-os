// lib/lua(vendorしたLua 5.4.7本体)が実際にビルド・リンクでき、
// lua_newstate/luaL_openlibs/luaL_dostring/lua_closeが動くことを確認するテスト。
//
// CLAUDE.md「Lua着手前の受け皿の状態」の「ビルドの二重管理」に対応。
// LovyanGFXと違いgit/tarballをlib_deps経由で取得せず、lua.c/luac.c(main()を持つ
// ファイル)を除いてlib/lua/src/へvendorしてある(lib/lua/README-pico-os.md参照)。
// PlatformIO側は`lib/`配下を自動的に「プロジェクト専用ライブラリ」として拾うため、
// 実機ビルドもこのテストと同じソースを見る。
#include "lua.hpp"
#include <cstdio>

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

int main(){
    lua_State* L = luaL_newstate();
    check(L != nullptr, "luaL_newstate: state生成");
    if(!L){
        printf("\nFAILED (failures=%d)\n", ++failures);
        return 1;
    }

    luaL_openlibs(L);
    check(true, "luaL_openlibs: 標準ライブラリの読み込みが落ちない");

    check(luaL_dostring(L, "return 1 + 2") == LUA_OK, "luaL_dostring: 式の評価が成功する");
    check((int)lua_tointeger(L, -1) == 3, "luaL_dostring: 1+2の結果が3になる");
    lua_pop(L, 1);

    // 構文エラーがpcall相当(luaL_dostring)で例外にならず、エラー値として返ること
    // (将来Lua側のエラーをErrorFunctions::ShowFatal()等へ橋渡しする前提の確認)
    check(luaL_dostring(L, "this is not lua") != LUA_OK, "luaL_dostring: 構文エラーはLUA_OK以外を返す");
    lua_pop(L, 1);

    lua_close(L);
    check(true, "lua_close: 破棄が落ちない");

    printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
