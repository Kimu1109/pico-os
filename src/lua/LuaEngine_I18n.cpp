// pico.lang() / pico.tr(): Luaアプリの表示言語(docs/design/i18n.md「Luaアプリ」)
//
// 訳はアプリのフォルダの lang/<言語>.lua に「原文 → 訳」の表として置く(C++の TR() と同じく原文がキー):
//
//   -- lang/en.lua
//   return {
//     ["スコア"] = "Score",
//     ["%d点"]   = "%d pts",
//   }
//
// 表は最初の pico.tr() で1回だけ読み、registry.pico_lang に言語コードと一緒に覚える
// (常駐するバックグラウンドのエンジンは、言語が変わったら読み直す)。
// 表のファイルは空の環境で実行するので、表を返す以外のこと(pico.*・グローバルの書き換え)はできない。
// ファイルが無い・壊れている・原文が表に無いときは原文のまま返す(訳し漏れでアプリが止まらないように)
#include "lua/LuaEngine.hpp"
#include "lua/LuaEngine_Internal.hpp"
#include "system/I18n_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "storage/SD_IO.hpp"
#include "OS_Data.hpp"

#include <cstring>

using namespace LuaInternal;

namespace {
    // 訳の表のファイルの上限(require するモジュールと同じ)
    constexpr size_t kMaxLangBytes = 32 * 1024;
    constexpr const char* kRegKey = "pico_lang";
}

void LuaEngine::pushLangTable(lua_State* L) {
    const char* code = I18nFunctions::Code(I18nFunctions::Current());

    // 覚えてある表が今の言語のものならそれを使う
    lua_getfield(L, LUA_REGISTRYINDEX, kRegKey);
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "code");
        const bool same = lua_type(L, -1) == LUA_TSTRING && strcmp(lua_tostring(L, -1), code) == 0;
        lua_pop(L, 1);
        if (same) {
            lua_getfield(L, -1, "t");
            lua_remove(L, -2);
            if (!lua_istable(L, -1)) { lua_pop(L, 1); lua_pushnil(L); }
            return;
        }
    }
    lua_pop(L, 1);

    // <app_dir>/lang/<code>.lua を読む(無ければ false を覚えて、次からは探さない)
    lua_pushboolean(L, 0);                                    // 結果(false = 表なし)
    FixedString<PICO_PATH_LEN> path;
    FixedString<PICO_STR_S> rel;
    rel.assign("lang/");
    rel.append(code);
    rel.append(".lua");
    if (OSData::SD_usable && PICO_IO::join(path, this->app_dir_, rel.c_str()) &&
        OSData::SD.exists(path.c_str())) {
        const char* why = "";
        if (pushFileString(L, path.c_str(), kMaxLangBytes, &why) == 0) {
            LOG_APP_WARN("pico.tr: %s を読めません(%s)", path.c_str(), why);
        } else {
            size_t len = 0;
            const char* src = lua_tolstring(L, -1, &len);
            FixedString<PICO_PATH_LEN> chunkname;
            chunkname.assign("@");
            chunkname.append(path);
            if (luaL_loadbufferx(L, src, len, chunkname.c_str(), "t") != LUA_OK) {
                LOG_APP_WARN("pico.tr: %s", lua_tostring(L, -1));
                lua_pop(L, 1);
            } else {
                lua_newtable(L);                              // 空の環境(表を返すだけのファイル)
                lua_setupvalue(L, -2, 1);
                if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
                    LOG_APP_WARN("pico.tr: %s", lua_tostring(L, -1));
                    lua_pop(L, 1);
                } else if (!lua_istable(L, -1)) {
                    LOG_APP_WARN("pico.tr: %s が表を返していません", path.c_str());
                    lua_pop(L, 1);
                } else {
                    lua_replace(L, -3);                       // 結果を表に差し替える
                }
            }
            lua_pop(L, 1);                                    // ソースの文字列
        }
    }

    // registry.pico_lang = { code = code, t = 結果 }
    lua_createtable(L, 0, 2);
    lua_pushstring(L, code);
    lua_setfield(L, -2, "code");
    lua_pushvalue(L, -2);
    lua_setfield(L, -2, "t");
    lua_setfield(L, LUA_REGISTRYINDEX, kRegKey);

    if (!lua_istable(L, -1)) { lua_pop(L, 1); lua_pushnil(L); }
}

// pico.lang() -> "ja" / "en"
int LuaEngine::l_lang(lua_State* L) {
    lua_pushstring(L, I18nFunctions::Code(I18nFunctions::Current()));
    return 1;
}

// pico.tr(text [, ...]) -> 今の言語の訳(無ければ text)。引数を足すと string.format(訳, ...) する
int LuaEngine::l_tr(lua_State* L) {
    LuaEngine* self = Self(L);
    luaL_checkstring(L, 1);
    const int nargs = lua_gettop(L);

    self->pushLangTable(L);                                   // 表 | nil
    if (lua_istable(L, -1)) {
        lua_pushvalue(L, 1);
        lua_rawget(L, -2);
        if (lua_type(L, -1) == LUA_TSTRING) lua_replace(L, 1); // 訳が見つかれば text を差し替える
        else lua_pop(L, 1);
    }
    lua_pop(L, 1);

    if (nargs == 1) {
        lua_settop(L, 1);
        return 1;
    }
    lua_getglobal(L, "string");
    lua_getfield(L, -1, "format");
    lua_remove(L, -2);
    lua_insert(L, 1);                                         // format, text, ...
    lua_call(L, nargs, 1);
    return 1;
}
