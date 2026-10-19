// require / pico.require(アプリのフォルダと同梱モジュールの先読み)
#include "lua/LuaEngine.hpp"
#include "lua/LuaEngine_Internal.hpp"
#include "services/Power_Functions.hpp"
#include "system/Mem_Functions.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>

#include "gui/widgets/Widget.hpp"
#include "gui/widgets/WidgetRegistry.hpp"
#include "gui/widgets/WidgetFactory.hpp"
#include "gui/widgets/WidgetProperty.hpp"
#include "gui/widgets/LayoutContainer.hpp"
#include "gui/widgets/GridContainer.hpp"
#include "gui/widgets/ScrollContainer.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/Textbox.hpp"
#include "gui/widgets/LuaCanvas.hpp"
#include "gui/widgets/CanvasRaster.hpp"
#include "gui/widgets/Checkbox.hpp"
#include "gui/widgets/NumberSlider.hpp"
#include "gui/widgets/ScrollList.hpp"
#include "gui/widgets/TabBar.hpp"
#include "gui/widgets/DropdownMenu.hpp"
#include "gui/widgets/dialogs/MsgDialog.hpp"
#include "gui/widgets/dialogs/InputDialog.hpp"
#include "gui/widgets/dialogs/FileSaveDialog.hpp"
#include "gui/widgets/dialogs/FileSelectDialog.hpp"
#include "gui/widgets/dialogs/ColorDialog.hpp"
#include "gui/widgets/dialogs/PickerDialog.hpp"
#include "gui/icons/icon_render.h"
#include "gui/icons/Bmp_Codec.hpp"
#include "gui/icons/Jpeg_Codec.hpp"
#include "gui/Fill4bpp.hpp"
#include "gui/core/Widget_Functions.hpp"
#include "gui/core/Error_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "gui/core/Scene_Functions.hpp"
#include "services/App_Functions.hpp"
#include "gui/core/GFX_Functions.hpp"
#include "gui/core/Scene.hpp"
#include "gui/scenes/LuaScene.hpp"
#include "storage/SD_IO.hpp"
#include "storage/SD_Path.hpp"
#include "system/Config_Functions.hpp"
#include "net/Http_Request.hpp"
#include "util/Url.hpp"
#include "system/Time_Functions.hpp"
#include "hal/Sound_Functions.hpp"
#include "system/Pad_Functions.hpp"
#include "hal/Battery_Functions.hpp"
#include "system/Notification_Functions.hpp"
#include "sound/Note_Name.hpp"
#include "sound/Mml_Compiler.hpp"
#include "lua/LuaDebugger.hpp"
#include "lua/LuaJson.hpp"
#include "lua/LuaBuiltinModules.hpp"
#include "crypto/Secret_Aead.hpp"
#include "devtools/Profiler_Functions.hpp"
#include "OS_Data.hpp"
#include "consts.hpp"
#include <Arduino.h>


using namespace LuaInternal;

// ---------------- require ----------------

namespace {
    constexpr size_t kMaxModuleBytes = 32 * 1024;
    constexpr int kMaxPreloadModules = 16;
    char g_require_marker = 0; // 実行中のモジュールの目印(循環の検出)

    bool IsIdentChar(char c) {
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
    }

    // "a.b" の形。英数字・_・-・区切りの.だけ。..や先頭末尾の.は不可
    bool ModuleNameValid(const char* name) {
        if (!name || !*name) return false;
        const size_t n = strlen(name);
        if (n > 47) return false;
        if (name[0] == '.' || name[n - 1] == '.') return false;
        for (size_t i = 0; i < n; i++) {
            const char c = name[i];
            if (c == '.') {
                if (i + 1 < n && name[i + 1] == '.') return false;
                continue;
            }
            if (!IsIdentChar(c) && c != '-') return false;
        }
        return true;
    }

    // registry[key] のテーブルをスタックに積む(無ければ作る)
    void PushRegTable(lua_State* L, const char* key) {
        lua_getfield(L, LUA_REGISTRYINDEX, key);
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            lua_newtable(L);
            lua_pushvalue(L, -1);
            lua_setfield(L, LUA_REGISTRYINDEX, key);
        }
    }

    // スタックの一番上の関数(OS同梱モジュールをコンパイルしたもの)から、デバッグ情報
    // (行番号・ローカル変数名)を落として置き換える。pico.gameはこれで約3割小さくなる。
    // 自分でdumpしたものを読み直すだけなのでバイナリのチャンクでも安全。
    // モジュールの中で起きたエラーは行番号が「?」になるが、error(msg, 2)で投げる
    // 引数の誤りは呼んだ側(アプリ)の行を指すので困らない
    struct DumpWriter { bool init; luaL_Buffer b; };
    int DumpWrite(lua_State* L, const void* p, size_t sz, void* ud) {
        DumpWriter* w = static_cast<DumpWriter*>(ud);
        if (!w->init) { w->init = true; luaL_buffinit(L, &w->b); }
        luaL_addlstring(&w->b, static_cast<const char*>(p), sz);
        return 0;
    }
    void StripFunction(lua_State* L, const char* chunkname) {
        DumpWriter w;
        w.init = false;
        if (lua_dump(L, DumpWrite, &w, 1) != 0 || !w.init) return;   // 失敗したらそのまま使う
        luaL_pushresult(&w.b);                                        // fn, bin
        lua_remove(L, -2);                                            // bin(元の関数は捨てる)
        lua_gc(L, LUA_GCCOLLECT);                                     // 読み直す前に元の分を返す
        size_t n = 0;
        const char* bin = lua_tolstring(L, -1, &n);
        if (luaL_loadbufferx(L, bin, n, chunkname, "b") != LUA_OK) {  // bin, fn|msg
            lua_remove(L, -2);
            return;
        }
        lua_remove(L, -2);                                            // fn
    }

    // ソース中の require("名前") / require "名前" / require('名前') を拾って worklist (テーブルの添字idx)へ足す。
    // 文字列として拾うだけ(コメントの中でも拾うが、無いモジュールは無視されるだけで害は無い)
    void ScanRequires(lua_State* L, const char* src, size_t len, int wl_idx) {
        static const char kWord[] = "require";
        const size_t wn = sizeof(kWord) - 1;
        for (size_t i = 0; i + wn < len; i++) {
            if (src[i] != 'r' || memcmp(src + i, kWord, wn) != 0) continue;
            if (i > 0 && IsIdentChar(src[i - 1])) continue;
            size_t j = i + wn;
            if (j < len && IsIdentChar(src[j])) continue;
            while (j < len && (src[j] == ' ' || src[j] == '\t')) j++;
            if (j < len && src[j] == '(') { j++; while (j < len && (src[j] == ' ' || src[j] == '\t')) j++; }
            if (j >= len || (src[j] != '"' && src[j] != '\'')) continue;
            const char q = src[j++];
            const size_t start = j;
            while (j < len && src[j] != q && src[j] != '\n') j++;
            if (j >= len || src[j] != q) continue;
            const size_t nlen = j - start;
            if (nlen == 0 || nlen > 47) continue;
            char name[48];
            memcpy(name, src + start, nlen);
            name[nlen] = '\0';
            if (!ModuleNameValid(name)) continue;

            const size_t count = lua_rawlen(L, wl_idx);
            if ((int)count >= kMaxPreloadModules) return;
            bool dup = false;
            for (size_t k = 1; k <= count && !dup; k++) {
                lua_rawgeti(L, wl_idx, (lua_Integer)k);
                dup = (strcmp(lua_tostring(L, -1), name) == 0);
                lua_pop(L, 1);
            }
            if (dup) continue;
            lua_pushstring(L, name);
            lua_rawseti(L, wl_idx, (lua_Integer)count + 1);
        }
    }
}

// 1つの名前の置き場所を探す(<app_dir>/<a/b>.lua → <app_dir>/<a/b>/init.lua)
bool LuaEngine::ResolveModulePath(const char* name, FixedString<PICO_PATH_LEN>& out) const {
    if (!OSData::SD_usable) return false;
    FixedString<PICO_STR_M> rel;
    rel.assign(name);
    char* buf = const_cast<char*>(rel.c_str());
    for (char* p = buf; *p; p++) if (*p == '.') *p = '/';

    static const char* const kSuffix[] = {".lua", "/init.lua"};
    for (const char* suf : kSuffix) {
        FixedString<PICO_PATH_LEN> cand;
        if (!PICO_IO::join(cand, app_dir_, rel.c_str())) continue;
        if (!cand.append(suf)) continue;
        if (!SdPathAllowed(cand.c_str())) continue;
        if (OSData::SD.exists(cand.c_str())) { out.assign(cand); return true; }
    }
    return false;
}

// path のファイルを文字列としてスタックに積む(積んだら1、できなければ0で何も積まない)
int LuaEngine::pushFileString(lua_State* L, const char* path, size_t max, const char** why) {
    *why = "";
    FsFile f = OSData::SD.open(path, O_RDONLY);
    if (!f) { *why = "開けません"; return 0; }
    const size_t file_size = f.fileSize();
    if (file_size > max) {
        f.close();
        *why = "大きすぎます";
        return 0;
    }
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    char chunk[256];
    size_t remaining = file_size;
    bool ok = true;
    while (remaining > 0) {
        const size_t want = (remaining < sizeof(chunk)) ? remaining : sizeof(chunk);
        const int got = f.read((uint8_t*)chunk, want);
        if (got <= 0) { ok = false; break; }
        luaL_addlstring(&b, chunk, (size_t)got);
        remaining -= (size_t)got;
    }
    f.close();
    if (!ok) {
        luaL_pushresult(&b); // バッファの後始末(箱をスタックから外す)
        lua_pop(L, 1);
        *why = "読み取りに失敗しました";
        return 0;
    }
    luaL_pushresult(&b);
    return 1;
}

int LuaEngine::PreloadTrampoline(lua_State* L) {
    LuaEngine* self = static_cast<LuaEngine*>(lua_touserdata(L, 1));
    const char* src = static_cast<const char*>(lua_touserdata(L, 2));
    const size_t len = (size_t)lua_tointeger(L, 3);
    self->preloadModules(src, len);
    return 0;
}

// スクリプトが require する名前を拾い、モジュールを(実行の外で)読み込んでおく。
// 読み込んだ関数は registry.pico_preload[名前] に、構文エラーなどはその文字列で置く
void LuaEngine::preloadModules(const char* src, size_t len) {
    lua_newtable(L);                       // worklist
    const int wl = lua_gettop(L);
    ScanRequires(L, src, len, wl);
    if (lua_rawlen(L, wl) == 0) { lua_pop(L, 1); return; }

    PushRegTable(L, "pico_preload");       // preload
    const int pre = lua_gettop(L);
    PushRegTable(L, "pico_loaded");        // 読み込み済み(もう一度コンパイルすると使われないまま残る)
    const int loaded = lua_gettop(L);

    for (size_t i = 1; i <= lua_rawlen(L, wl); i++) {   // 読んだモジュールが増やした分も辿る
        lua_rawgeti(L, wl, (lua_Integer)i);
        const char* name = lua_tostring(L, -1);          // worklistが保持しているので有効
        lua_getfield(L, pre, name);
        lua_getfield(L, loaded, name);
        const bool already = !lua_isnil(L, -1) || !lua_isnil(L, -2);
        lua_pop(L, 2);
        if (already) { lua_pop(L, 1); continue; }

        // OS同梱のモジュール(pico.ui / pico.async)は、アプリのフォルダより優先してそのソースを使う
        if (const char* bsrc = LuaBuiltin::Source(name)) {
            FixedString<PICO_STR_M> bname;
            bname.assign("=");
            bname.append(name);
            if (luaL_loadbufferx(L, bsrc, strlen(bsrc), bname.c_str(), "t") == LUA_OK) {
                StripFunction(L, bname.c_str());
            }
            lua_setfield(L, pre, name);  // 成功なら関数、失敗ならメッセージ
            lua_pop(L, 1);
            continue;
        }

        FixedString<PICO_PATH_LEN> path;
        if (!ResolveModulePath(name, path)) { lua_pop(L, 1); continue; } // 無ければ実行時にエラーになる

        const char* why = "";
        if (pushFileString(L, path.c_str(), kMaxModuleBytes, &why) == 0) {
            lua_pushfstring(L, "module '%s' を読めません(%s): %s", name, path.c_str(), why);
            lua_setfield(L, pre, name);
            lua_pop(L, 1);
            continue;
        }
        size_t slen = 0;
        const char* s = lua_tolstring(L, -1, &slen);
        ScanRequires(L, s, slen, wl);

        FixedString<PICO_PATH_LEN> chunkname;
        chunkname.assign("@");
        chunkname.append(path);
        if (luaL_loadbufferx(L, s, slen, chunkname.c_str(), "t") == LUA_OK) {
            // app.cfg の strip_debug: 行番号とローカル変数名を落とす(大きなアプリでLuaのメモリを減らす)
            if (permissions_.strip_debug) StripFunction(L, chunkname.c_str());
            lua_setfield(L, pre, name);        // 関数
        } else {
            lua_setfield(L, pre, name);        // エラーメッセージ(文字列)
        }
        lua_pop(L, 2);                          // ソースの文字列と名前
    }
    lua_settop(L, wl - 1);
}

int LuaEngine::l_require(lua_State* L) {
    LuaEngine* self = Self(L);
    const char* name = luaL_checkstring(L, 1);
    if (!ModuleNameValid(name)) {
        return luaL_error(L, "require: モジュール名 '%s' は使えません(英数字・_・-と、区切りの.だけ)", name);
    }

    PushRegTable(L, "pico_loaded");
    const int loaded = lua_gettop(L);
    lua_getfield(L, loaded, name);
    if (!lua_isnil(L, -1)) {
        if (lua_islightuserdata(L, -1) && lua_touserdata(L, -1) == &g_require_marker) {
            return luaL_error(L, "require: '%s' が循環しています", name);
        }
        return 1; // 読み込み済み
    }
    lua_pop(L, 1);

    // 先読みしてあれば、その関数(文字列ならその時のエラー)を使う
    PushRegTable(L, "pico_preload");
    const int pre = lua_gettop(L);
    lua_getfield(L, pre, name);
    FixedString<PICO_PATH_LEN> path;
    if (lua_isstring(L, -1) && !lua_isnumber(L, -1)) {
        return luaL_error(L, "%s", lua_tostring(L, -1));
    }
    if (lua_isfunction(L, -1)) {
        lua_pushnil(L);
        lua_setfield(L, pre, name);           // 先読みの置き場から外す(関数は下の呼び出しで使い切る)
        self->ResolveModulePath(name, path);  // 表示用(デバッガ・エラー)。見つからなければ空のまま
    } else {
        lua_pop(L, 1);
        // 先読みで拾えなかった名前(組み立てた名前など)。実行中のコンパイルはスタックを使うので、
        // Luaの呼び出しが浅い(トップレベル近く)ときだけ許す
        lua_Debug ar;
        if (lua_getstack(L, 6, &ar)) {
            return luaL_error(L, "require: '%s' を実行中に読み込めません(関数の奥から呼ばれています)。"
                                 "スクリプトの先頭で require(\"%s\") と書くか、トップレベルで呼んでください", name, name);
        }
        if (!self->ResolveModulePath(name, path)) {
            return luaL_error(L, "require: モジュール '%s' が見つかりません(アプリのフォルダの %s.lua または %s/init.lua)",
                              name, name, name);
        }
        const char* why = "";
        if (pushFileString(L, path.c_str(), kMaxModuleBytes, &why) == 0) {
            return luaL_error(L, "require: '%s' を読めません(%s): %s", name, path.c_str(), why);
        }
        size_t slen = 0;
        const char* s = lua_tolstring(L, -1, &slen);
        FixedString<PICO_PATH_LEN> chunkname;
        chunkname.assign("@");
        chunkname.append(path);
        if (luaL_loadbufferx(L, s, slen, chunkname.c_str(), "t") != LUA_OK) {
            return lua_error(L);              // 構文エラーのメッセージ
        }
        if (self->permissions_.strip_debug) StripFunction(L, chunkname.c_str());
        lua_remove(L, -2);                    // ソースの文字列
    }

    // スタック: [..., loaded, pre, fn]
    lua_pushlightuserdata(L, &g_require_marker);
    lua_setfield(L, loaded, name);            // 実行中の目印(循環の検出)
    lua_pushvalue(L, -1);
    lua_pushstring(L, name);
    lua_pushstring(L, path.c_str());
    if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
        // 失敗した目印を残さない。エラーはそのまま呼び出し元へ(打ち切りの握り潰しにならない)
        lua_pushnil(L);
        lua_setfield(L, loaded, name);
        return lua_error(L);
    }
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_pushboolean(L, 1);
    }
    lua_pushvalue(L, -1);
    lua_setfield(L, loaded, name);
    return 1;
}
