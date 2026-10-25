/* picoos lua-test の下請け。vendorしたLua(lib/lua、LUA_32BITSで実機と同じ数の扱い)で
 * script/lua_test/pico_test.lua を読み、その main(arg) を呼んで終了コードにするだけ。
 *   pico_lua_test <リポジトリのルート> [pico_test.lua へ渡す引数...]
 * ビルドは cli-tool/picoos が cc で直接行う(CMake不要。script/lua_test/build/ に置く) */
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include <stdio.h>

static int traceback(lua_State* L){
    luaL_traceback(L, L, lua_tostring(L, 1), 1);
    return 1;
}

int main(int argc, char** argv){
    if(argc < 2){
        fprintf(stderr, "usage: pico_lua_test <repo root> [args...]\n");
        return 2;
    }
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    lua_pushstring(L, argv[1]);
    lua_setglobal(L, "ROOT_DIR");

    lua_pushcfunction(L, traceback);
    char path[4096];
    snprintf(path, sizeof(path), "%s/script/lua_test/pico_test.lua", argv[1]);
    int rc = 1;
    if(luaL_loadfile(L, path) != LUA_OK || lua_pcall(L, 0, 1, 1) != LUA_OK){
        fprintf(stderr, "[FATAL] %s\n", lua_tostring(L, -1));
        lua_close(L);
        return 2;
    }
    /* 戻り値の表の main(arg) を呼ぶ。arg は 1.. に残りの引数 */
    lua_getfield(L, -1, "main");
    lua_createtable(L, argc - 2, 0);
    for(int i = 2; i < argc; i++){
        lua_pushstring(L, argv[i]);
        lua_rawseti(L, -2, i - 1);
    }
    if(lua_pcall(L, 1, 1, 1) != LUA_OK){
        fprintf(stderr, "[FATAL] %s\n", lua_tostring(L, -1));
        rc = 2;
    } else {
        rc = (int)lua_tointeger(L, -1);
    }
    lua_close(L);
    return rc;
}
