// pico.* のうちSDカード・設定ファイル・アプリの保存(store_load/store_save)
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
#include "gui/icons/Image_Codec.hpp"
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

namespace {
    constexpr size_t kMaxStoreBytes = PICO_STR_16KiB;
    constexpr size_t kMaxStoreFileBytes = 24 * 1024;   // 暗号化した保存ファイルの読み込み上限
}

// ---- ファイルの情報・部分読み ----

int LuaEngine::l_sd_stat(lua_State* L) {
    // pico.sd_stat(path) -> {size=バイト数, is_dir=bool} | nil
    LuaEngine* self = Self(L);
    const char* path = luaL_checkstring(L, 1);
    if (!OSData::SD_usable) { lua_pushnil(L); return 1; }
    if (!self->SdPathAllowed(path)) {
        LOG_APP_WARN("pico.sd_stat: アプリディレクトリ外へのアクセスは許可されていません: %s", path);
        lua_pushnil(L);
        return 1;
    }
    if (!OSData::SD.exists(path)) { lua_pushnil(L); return 1; }
    FsFile f = OSData::SD.open(path, O_RDONLY);
    if (!f) { lua_pushnil(L); return 1; }
    const bool is_dir = f.isDir();
    const size_t size = is_dir ? 0 : f.fileSize();
    f.close();
    lua_createtable(L, 0, 2);
    lua_pushinteger(L, (lua_Integer)size);
    lua_setfield(L, -2, "size");
    lua_pushboolean(L, is_dir);
    lua_setfield(L, -2, "is_dir");
    return 1;
}

int LuaEngine::l_sd_read_part(lua_State* L) {
    // pico.sd_read_part(path, offset, length) -> string | nil
    // offsetバイト目からlengthバイトまで(sd_readと同じ上限)。ファイルの終わりを越えたら短く返る。
    // 大きなファイルを少しずつ読む/途中だけ読むためのもの
    LuaEngine* self = Self(L);
    const char* path = luaL_checkstring(L, 1);
    const lua_Integer offset = luaL_checkinteger(L, 2);
    const lua_Integer length = luaL_checkinteger(L, 3);
    if (offset < 0 || length < 0) return luaL_error(L, "pico.sd_read_part: offsetとlengthは0以上です");
    if (!OSData::SD_usable) { lua_pushnil(L); return 1; }
    if (!self->SdPathAllowed(path)) {
        LOG_APP_WARN("pico.sd_read_part: アプリディレクトリ外へのアクセスは許可されていません: %s", path);
        lua_pushnil(L);
        return 1;
    }
    FsFile f = OSData::SD.open(path, O_RDONLY);
    if (!f || f.isDir()) { if (f) f.close(); lua_pushnil(L); return 1; }

    const size_t file_size = f.fileSize();
    size_t want = (size_t)std::min<lua_Integer>(length, (lua_Integer)kMaxSdReadBytes);
    if ((size_t)offset >= file_size) want = 0;
    else want = std::min(want, file_size - (size_t)offset);
    if (want > 0 && !f.seek((uint32_t)offset)) { f.close(); lua_pushnil(L); return 1; }

    luaL_Buffer b;
    luaL_buffinit(L, &b);
    char chunk[256];
    size_t remaining = want;
    bool ok = true;
    while (remaining > 0) {
        const size_t n = std::min(remaining, sizeof(chunk));
        const int got = f.read((uint8_t*)chunk, n);
        if (got <= 0) { ok = false; break; }
        luaL_addlstring(&b, chunk, (size_t)got);
        remaining -= (size_t)got;
    }
    f.close();
    if (!ok) { lua_pushnil(L); return 1; }
    luaL_pushresult(&b);
    return 1;
}

// ---------------- SDカードアクセス ----------------
// ヘッダのクラスコメント参照。OSData::SD_usable==falseの間はどれも失敗(false/nil)を
// 返すだけでluaL_errorにはしない。app_dir_の外を指すパスも同じ扱い(SdPathAllowed()参照。
// プログラマの書き間違いだけでなく、悪意あるスクリプトが試す経路でもあるため
// luaL_errorで詳細を返さず、SD無し等と同じ「実行時の状態」枠にまとめてある)。

bool LuaEngine::SdPathAllowed(const char* path) const {
    if (permissions_.sd_outside_app_dir) return true;

    FixedString<PICO_PATH_LEN> normalized;
    if (!PICO_IO::normalize(normalized, path)) return false;

    const size_t dir_len = app_dir_.length();
    // app_dir_=="/"(既定値。LuaScene以外がapp_dirを指定せずLuaEngineを直接使う場合)は
    // 「制限なし」に相当する
    if (dir_len <= 1) return true;

    const char* p = normalized.c_str();
    const char* dir = app_dir_.c_str();
    if (strncmp(p, dir, dir_len) != 0) return false;
    // "/lua/foo"は"/lua"の配下だが、"/luaxxx"のような別ディレクトリを誤って配下と
    // 判定しないよう、続きがパス終端か'/'であることまで確認する
    return p[dir_len] == '\0' || p[dir_len] == '/';
}

namespace {
    // SdWriteAllowed()で比べるための形へ直す。normalized(PICO_IO::normalize()済み)の
    // 各セグメントについて、FATと同じく先頭の空白・末尾の空白と'.'を捨て、ASCIIを小文字にする
    // ("/Lua/Apps/X/APP.CFG." も "/lua/apps/x/app.cfg" と同じファイルを指すため)。
    // 捨てると空になるセグメントは元のまま残す(そういう名前はFATでは開けないので害は無い)
    bool CanonicalizeSdPath(FixedString<PICO_PATH_LEN>& out, const char* normalized) {
        char buf[PICO_PATH_LEN];
        size_t n = 0;
        const char* p = normalized;
        while (*p) {
            while (*p == '/') p++;
            if (!*p) break;
            const char* start = p;
            while (*p && *p != '/') p++;
            const char* b = start;
            const char* e = p;
            while (b < e && *b == ' ') b++;
            while (e > b && (e[-1] == ' ' || e[-1] == '.')) e--;
            if (b == e) { b = start; e = p; }
            if (n + 1 + (size_t)(e - b) >= sizeof(buf)) return false;
            buf[n++] = '/';
            for (const char* q = b; q < e; ++q) {
                char c = *q;
                if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
                buf[n++] = c;
            }
        }
        if (n == 0) buf[n++] = '/';
        buf[n] = '\0';
        return out.assign(buf);
    }

    // pathがdir自身か、その祖先か("/"は全ての祖先)
    bool IsAncestorOrSelf(const char* path, const char* dir) {
        if (strcmp(path, "/") == 0) return true;
        const size_t len = strlen(path);
        return strncmp(dir, path, len) == 0 && (dir[len] == '\0' || dir[len] == '/');
    }

    // "/lua/apps/<名前>"(LuaAppScannerが1アプリとして見るディレクトリ)か
    bool IsScannedAppDir(const char* path) {
        const char* apps = PICO_Path::DIR::LUA_APPS; // "/lua/apps/"
        const size_t n = strlen(apps);
        if (strncmp(path, apps, n) != 0) return false;
        const char* rest = path + n;
        return rest[0] != '\0' && strchr(rest, '/') == nullptr;
    }

    constexpr const char* kAppConfigName = "app.cfg"; // LuaAppScannerが読む名前(小文字で比べる)
}

bool LuaEngine::SdWriteAllowed(const char* path, const char* api, bool is_remove) const {
    if (!SdPathAllowed(path)) {
        LOG_APP_WARN("%s: アプリディレクトリ外へのアクセスは許可されていません: %s", api, path);
        return false;
    }

    FixedString<PICO_PATH_LEN> normalized;
    FixedString<PICO_PATH_LEN> target;
    FixedString<PICO_PATH_LEN> own_dir;
    if (!PICO_IO::normalize(normalized, path) || !CanonicalizeSdPath(target, normalized.c_str()) ||
        !CanonicalizeSdPath(own_dir, app_dir_.c_str())) {
        LOG_APP_WARN("%s: パスを解釈できません: %s", api, path);
        return false;
    }

    const char* t = target.c_str();
    const char* slash = strrchr(t, '/');
    FixedString<PICO_PATH_LEN> parent;
    if (slash == t) parent.assign("/");
    else parent.assign(t, (size_t)(slash - t));

    bool is_protected = false;
    if (strcmp(slash + 1, kAppConfigName) == 0) {
        // 自分のapp.cfg、またはスキャン対象のアプリのapp.cfg(sd_outside_app_dirを持つアプリが
        // 他のアプリ、あるいは新しく作ったアプリのapp.cfgで権限を足すのも防ぐ)
        is_protected = strcmp(parent.c_str(), own_dir.c_str()) == 0 || IsScannedAppDir(parent.c_str());
    }
    if (!is_protected && is_remove) {
        // app.cfgを含むディレクトリごと消すのも同じ扱い
        is_protected = IsAncestorOrSelf(t, own_dir.c_str()) || IsScannedAppDir(t) ||
                       IsAncestorOrSelf(t, "/lua/apps");
    }
    if (is_protected) {
        LOG_APP_WARN("%s: アプリの設定ファイル(app.cfg)は書き換えられません: %s", api, path);
        return false;
    }
    return true;
}

int LuaEngine::l_sd_exists(lua_State* L) {
    LuaEngine* self = Self(L);
    const char* path = luaL_checkstring(L, 1);
    lua_pushboolean(L, OSData::SD_usable && self->SdPathAllowed(path) && OSData::SD.exists(path));
    return 1;
}

int LuaEngine::l_sd_read(lua_State* L) {
    LuaEngine* self = Self(L);
    const char* path = luaL_checkstring(L, 1);
    if (!OSData::SD_usable) { lua_pushnil(L); return 1; }
    if (!self->SdPathAllowed(path)) {
        LOG_APP_WARN("pico.sd_read: アプリディレクトリ外へのアクセスは許可されていません: %s", path);
        lua_pushnil(L);
        return 1;
    }

    FsFile f = OSData::SD.open(path, O_RDONLY);
    if (!f) { lua_pushnil(L); return 1; }

    const size_t file_size = f.fileSize();
    if (file_size > kMaxSdReadBytes) {
        f.close();
        LOG_APP_WARN("pico.sd_read: %s が上限(%uB)を超えています(%uB)",
            path, (unsigned)kMaxSdReadBytes, (unsigned)file_size);
        lua_pushnil(L);
        return 1;
    }

    // MarkdownView::load()/LuaScene::loadAndRun()と同じく、ファイル全体ぶんの
    // 一時バッファをヒープへ一度に確保せず、スタック上の小さなチャンクで読み進める
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    char chunk[256];
    size_t remaining = file_size;
    bool ok = true;
    while (remaining > 0) {
        const size_t want = (remaining < sizeof(chunk)) ? remaining : sizeof(chunk);
        const int got = f.read((uint8_t*)chunk, want);
        if (got <= 0) { ok = false; break; } // 読み取り失敗。読めたところまでで打ち切る
        luaL_addlstring(&b, chunk, (size_t)got);
        remaining -= (size_t)got;
    }
    f.close();

    if (!ok) {
        // luaL_Bufferへ積んだ分は使わず捨てる(luaL_pushresultしないままリターンして良い。
        // Luaのスタック上のuserdataはGCが回収する)
        lua_pushnil(L);
        return 1;
    }

    luaL_pushresult(&b);
    return 1;
}

int LuaEngine::l_sd_write(lua_State* L) {
    LuaEngine* self = Self(L);
    const char* path = luaL_checkstring(L, 1);
    size_t len = 0;
    const char* data = luaL_checklstring(L, 2, &len);
    const bool append = lua_toboolean(L, 3);

    if (!OSData::SD_usable) { lua_pushboolean(L, false); return 1; }
    if (!self->SdWriteAllowed(path, "pico.sd_write")) {
        lua_pushboolean(L, false);
        return 1;
    }

    FsFile f = OSData::SD.open(path, O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC));
    if (!f) { lua_pushboolean(L, false); return 1; }

    const bool ok = (len == 0) || (f.write(data, len) == len);
    f.close();
    lua_pushboolean(L, ok);
    return 1;
}

int LuaEngine::l_sd_remove(lua_State* L) {
    LuaEngine* self = Self(L);
    const char* path = luaL_checkstring(L, 1);
    if (!OSData::SD_usable) { lua_pushboolean(L, false); return 1; }
    if (!self->SdWriteAllowed(path, "pico.sd_remove", true)) {
        lua_pushboolean(L, false);
        return 1;
    }

    // FileExplorer::on_press_delete()と同じ判断(ディレクトリなら再帰削除)
    FsFile f = OSData::SD.open(path);
    bool ok;
    if (!f) {
        ok = false;
    } else if (f.isDir()) {
        f.close();
        ok = PICO_IO::removeRecursive(path);
    } else {
        f.close();
        ok = OSData::SD.remove(path);
    }
    lua_pushboolean(L, ok);
    return 1;
}

int LuaEngine::l_sd_mkdir(lua_State* L) {
    LuaEngine* self = Self(L);
    const char* path = luaL_checkstring(L, 1);
    if (!OSData::SD_usable) { lua_pushboolean(L, false); return 1; }
    if (!self->SdWriteAllowed(path, "pico.sd_mkdir")) {
        lua_pushboolean(L, false);
        return 1;
    }

    lua_pushboolean(L, OSData::SD.mkdir(path));
    return 1;
}

int LuaEngine::l_sd_list(lua_State* L) {
    LuaEngine* self = Self(L);
    const char* path = luaL_checkstring(L, 1);
    if (!OSData::SD_usable) { lua_pushnil(L); return 1; }
    if (!self->SdPathAllowed(path)) {
        LOG_APP_WARN("pico.sd_list: アプリディレクトリ外へのアクセスは許可されていません: %s", path);
        lua_pushnil(L);
        return 1;
    }

    FsFile dir = OSData::SD.open(path, O_RDONLY);
    if (!dir || !dir.isDir()) {
        if (dir) dir.close();
        lua_pushnil(L);
        return 1;
    }

    // FileExplorer::update_list()と同じ走査方法。{name=..., is_dir=...}の配列を返す
    lua_newtable(L);
    int idx = 1;
    FsFile file;
    char name[128];
    while (file.openNext(&dir, O_RDONLY)) {
        if (file.getName(name, sizeof(name))) {
            lua_newtable(L);
            lua_pushstring(L, name);
            lua_setfield(L, -2, "name");
            lua_pushboolean(L, file.isDir());
            lua_setfield(L, -2, "is_dir");
            lua_pushinteger(L, file.isDir() ? 0 : (lua_Integer)file.fileSize());
            lua_setfield(L, -2, "size");
            lua_rawseti(L, -2, idx++);
        }
        file.close();
    }
    dir.close();
    return 1;
}

// ---------------- 設定ファイル ----------------
// ヘッダのl_config_readの説明参照。書式はPICO_Config(key=value、'#'でコメント、後勝ち)。

namespace {
    // config_read/config_getの共通の入口。読めるならtrue(ファイルがあり、ディレクトリでなく、
    // 大きさがpico.sd_readと同じ上限以内)。読めない理由はここでログへ出す
    bool OpenableConfig(LuaEngine* self, const char* path, const char* api, size_t max_bytes,
                        bool path_allowed) {
        (void)self;
        if (!OSData::SD_usable) return false;
        if (!path_allowed) {
            LOG_APP_WARN("%s: アプリディレクトリ外へのアクセスは許可されていません: %s", api, path);
            return false;
        }
        if (!OSData::SD.exists(path)) return false;
        FsFile f = OSData::SD.open(path, O_RDONLY);
        if (!f) return false;
        const bool is_dir = f.isDir();
        const size_t size = f.fileSize();
        f.close();
        if (is_dir) return false;
        if (size > max_bytes) {
            LOG_APP_WARN("%s: %s が上限(%uB)を超えています(%uB)", api, path,
                (unsigned)max_bytes, (unsigned)size);
            return false;
        }
        return true;
    }

    // キーとして書けるか。読み戻したときに同じキーになる形だけを許す
    // ('='を含まない・前後に空白が無い・'#'で始まらない・改行を含まない)
    bool ValidConfigKey(const char* key) {
        const size_t len = strlen(key);
        if (len == 0 || len >= PICO_Config::kConfigMaxKeyLen) return false;
        if (key[0] == '#' || key[0] == ' ' || key[0] == '\t') return false;
        if (key[len - 1] == ' ' || key[len - 1] == '\t') return false;
        for (const char* p = key; *p; ++p) {
            if (*p == '=' || *p == '\r' || *p == '\n') return false;
        }
        return true;
    }
}

int LuaEngine::l_config_read(lua_State* L) {
    LuaEngine* self = Self(L);
    const char* path = luaL_checkstring(L, 1);
    if (!OpenableConfig(self, path, "pico.config_read", kMaxSdReadBytes, self->SdPathAllowed(path))) {
        lua_pushnil(L);
        return 1;
    }

    lua_newtable(L);
    const bool ok = PICO_Config::ParseFile(path, [&](const char* key, const char* value) {
        // 後勝ち: 同じキーが後で出てくればそのまま上書きされる
        lua_pushstring(L, value);
        lua_setfield(L, -2, key);
    });
    if (!ok) {
        lua_pop(L, 1);
        lua_pushnil(L);
    }
    return 1;
}

int LuaEngine::l_config_get(lua_State* L) {
    LuaEngine* self = Self(L);
    const char* path = luaL_checkstring(L, 1);
    const char* key = luaL_checkstring(L, 2);
    if (!OpenableConfig(self, path, "pico.config_get", kMaxSdReadBytes, self->SdPathAllowed(path))) {
        lua_pushnil(L);
        return 1;
    }

    FixedString<PICO_Config::kConfigMaxValueLen> found;
    bool has = false;
    PICO_Config::ParseFile(path, [&](const char* k, const char* v) {
        if (strcmp(k, key) == 0) { found.assign(v); has = true; } // 後勝ち
    });
    if (has) lua_pushstring(L, found.c_str());
    else lua_pushnil(L);
    return 1;
}

int LuaEngine::l_config_write(lua_State* L) {
    LuaEngine* self = Self(L);
    const char* path = luaL_checkstring(L, 1);
    const char* key = luaL_checkstring(L, 2);
    if (!ValidConfigKey(key)) {
        return luaL_error(L, "pico.config_write: キーが不正です('='・改行・前後の空白を含まず、"
                             "'#'で始まらない%d字未満の文字列): %s",
                          (int)PICO_Config::kConfigMaxKeyLen, key);
    }

    // 値は文字列/数値/真偽値。読み戻し(PICO_Config::ConfigValue::AsXxx)で同じ値になる表記にする
    char num_buf[48];
    const char* value = nullptr;
    switch (lua_type(L, 3)) {
    case LUA_TBOOLEAN:
        value = PICO_Config::ConfigValue::FromBool(lua_toboolean(L, 3));
        break;
    case LUA_TNUMBER:
        if (lua_isinteger(L, 3)) {
            snprintf(num_buf, sizeof(num_buf), "%lld", (long long)lua_tointeger(L, 3));
        } else {
            // AsFloat()は指数表記を受け付けないので%gは使えない。小数点以下の余分な0は削る
            snprintf(num_buf, sizeof(num_buf), "%.6f", (double)lua_tonumber(L, 3));
            char* end = num_buf + strlen(num_buf);
            while (end > num_buf + 1 && end[-1] == '0' && end[-2] != '.') *--end = '\0';
        }
        value = num_buf;
        break;
    case LUA_TSTRING:
        value = lua_tostring(L, 3);
        break;
    default:
        return luaL_error(L, "pico.config_write: 値は文字列・数値・真偽値のどれかです");
    }

    if (!OSData::SD_usable) { lua_pushboolean(L, false); return 1; }
    // 改行を含む値を通すと、次の行として別のキー(権限等)を差し込めてしまう
    if (strchr(value, '\n') || strchr(value, '\r')) {
        LOG_APP_WARN("pico.config_write: 値に改行は使えません (%s)", key);
        lua_pushboolean(L, false);
        return 1;
    }
    if (!self->SdWriteAllowed(path, "pico.config_write")) {
        lua_pushboolean(L, false);
        return 1;
    }

    lua_pushboolean(L, PICO_Config::SetValue(path, key, value));
    return 1;
}

int LuaEngine::l_store_load(lua_State* L) {
    LuaEngine* self = Self(L);
    // pico.store_load([{password = "..."}]) 暗号化して保存したstoreを読む(パスワードを付けて保存したときだけ必要)
    const char* password = nullptr;
    if (lua_istable(L, 1)) {
        lua_getfield(L, 1, "password");   // スタックに残したまま使う(文字列を生かしておくため)
        if (lua_type(L, -1) == LUA_TSTRING && lua_tostring(L, -1)[0]) password = lua_tostring(L, -1);
    }
    if (!OSData::SD_usable) { lua_pushnil(L); return 1; }
    FixedString<PICO_PATH_LEN> path;
    if (!PICO_IO::join(path, self->app_dir_, "store.json")) { lua_pushnil(L); return 1; }
    if (!OSData::SD.exists(path.c_str())) { lua_pushnil(L); return 1; }

    const char* why = "";
    // 暗号化すると約4/3倍になるので、読む上限は平文の上限より少し大きくしてある
    if (pushFileString(L, path.c_str(), kMaxStoreFileBytes, &why) == 0) {
        LOG_APP_WARN("pico.store_load: %s", why);
        lua_pushnil(L);
        return 1;
    }
    size_t len = 0;
    const char* s = lua_tolstring(L, -1, &len);
    const char* err = nullptr;
    size_t pos = 0;

    if (SecretAead::IsEncrypted(s, len)) {
        SecretAead::Result r;
        bool decoded = false;
        {
            std::string plain;
            r = SecretAead::Decrypt(s, len, self->app_dir_.c_str(), password, plain);
            if (r == SecretAead::Result::Ok) {
                decoded = LuaJson::Decode(L, plain.data(), plain.size(), false, err, pos);
            }
        }
        if (r != SecretAead::Result::Ok) {
            LOG_APP_WARN("pico.store_load: %s", SecretAead::ResultToStr(r));
            lua_pushnil(L);
            lua_pushstring(L, SecretAead::ResultToStr(r));
            return 2;
        }
        if (!decoded) {
            LOG_APP_WARN("pico.store_load: store.jsonが壊れています: %s", err ? err : "?");
            lua_pushnil(L);
        }
        return 1;
    }

    if (!LuaJson::Decode(L, s, len, false, err, pos)) {
        LOG_APP_WARN("pico.store_load: store.jsonが壊れています: %s", err ? err : "?");
        lua_pushnil(L);
    }
    return 1;
}

int LuaEngine::l_store_save(lua_State* L) {
    LuaEngine* self = Self(L);
    luaL_checkany(L, 1);
    // pico.store_save(tbl [, {encrypt = true, password = "..."}])
    //   encrypt=trueで暗号化して保存する(passwordを渡すとそのパスワードで。渡さなければアプリごとの固定鍵)。
    //   passwordだけ渡してもencrypt=true扱い。暗号化すると平文の上限は12KiB
    bool encrypt = false;
    const char* password = nullptr;
    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "encrypt");
        encrypt = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        lua_getfield(L, 2, "password");
        if (lua_type(L, -1) == LUA_TSTRING && lua_tostring(L, -1)[0]) { password = lua_tostring(L, -1); encrypt = true; }
        // passwordの文字列はスタックに残したまま使う
    }
    lua_settop(L, lua_istable(L, 2) ? 3 : 1);

    const char* err = nullptr;
    bool ok = false;
    bool too_big = false;
    const char* enc_err = nullptr;
    FixedString<PICO_PATH_LEN> path, part;
    {
        std::string json;
        ok = LuaJson::EncodeValue(L, 1, json, 0, err);
        if (ok && json.size() > kMaxStoreBytes) { too_big = true; ok = false; }
        if (ok && encrypt) {
            if (json.size() > SecretAead::kMaxPlainBytes) {
                too_big = true;
                ok = false;
            } else {
                std::string enc;
                const SecretAead::Result r = SecretAead::Encrypt((const uint8_t*)json.data(), json.size(),
                                                                 self->app_dir_.c_str(), password, enc);
                if (r != SecretAead::Result::Ok) { ok = false; enc_err = SecretAead::ResultToStr(r); }
                else json.swap(enc);
            }
        }
        if (ok) {
            ok = OSData::SD_usable
                && PICO_IO::join(path, self->app_dir_, "store.json")
                && self->SdWriteAllowed(path.c_str(), "pico.store_save");
            if (ok) {
                part.assign(path);
                part.append(".tmp");
                // 一時ファイルへ書いてから差し替える(書き込み中の電源断で既存の内容を壊さない)
                FsFile f = OSData::SD.open(part.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
                ok = f && (json.empty() || f.write(json.data(), json.size()) == json.size());
                if (f) f.close();
                if (ok) {
                    OSData::SD.remove(path.c_str());
                    ok = OSData::SD.rename(part.c_str(), path.c_str());
                } else if (OSData::SD_usable) {
                    OSData::SD.remove(part.c_str());
                }
            }
        }
    }
    if (err) return luaL_error(L, "pico.store_save: 値をJSONにできません(%s)", err);
    if (too_big) {
        LOG_APP_WARN("pico.store_save: 大きすぎます(%uBまで)", (unsigned)(encrypt ? SecretAead::kMaxPlainBytes : kMaxStoreBytes));
    }
    if (enc_err) LOG_APP_WARN("pico.store_save: 暗号化できません: %s", enc_err);
    lua_pushboolean(L, ok);
    return 1;
}
