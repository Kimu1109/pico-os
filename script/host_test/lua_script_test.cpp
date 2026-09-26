// Luaで書いたテスト(script/host_test/*.lua)を、vendorしたLua(lib/lua)で動かすだけの下請け。
//   lua_script_test <テスト.lua> <リポジトリのルート>
// ルートはLua側へ ROOT_DIR として渡す。テストはos.exit(0/1)で結果を返す
// (実行時エラーもここで1にする)
#include "lua.hpp"

#include <cstdio>

int main(int argc, char** argv){
    if(argc < 3){
        fprintf(stderr, "usage: lua_script_test <test.lua> <repo root>\n");
        return 2;
    }
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    lua_pushstring(L, argv[2]);
    lua_setglobal(L, "ROOT_DIR");
    int rc = 0;
    if(luaL_dofile(L, argv[1]) != LUA_OK){
        fprintf(stderr, "[FAIL] %s\n", lua_tostring(L, -1));
        rc = 1;
    }
    lua_close(L);
    return rc;
}
