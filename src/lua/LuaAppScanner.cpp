#include "lua/LuaAppScanner.hpp"

#include <SdFat.h>
#include <cstring>

#include "functions/App_Functions.hpp"
#include "functions/Config_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "gui/scenes/LuaScene.hpp"
#include "lua/LuaPermissions.hpp"
#include "storage/SD_IO.hpp"
#include "storage/SD_Path.hpp"
#include "util/FixedString.hpp"
#include "OS_Data.hpp"
#include "consts.hpp"

void (*LuaAppScanner::apps_changed)() = nullptr;

namespace {
    void NotifyChanged(){
        if(LuaAppScanner::apps_changed) LuaAppScanner::apps_changed();
    }

    // アプリディレクトリ配下に置く設定ファイルの相対名。main.lua同様、
    // 絶対パスは呼び出し側がapp_dir_pathと組み立てる
    constexpr const char* kAppConfigFileName = "app.cfg";

    void ReadBool(const char* config_path, const char* key, const char* value, bool& out){
        bool v = false;
        if (PICO_Config::ConfigValue::AsBool(value, v)) {
            out = v;
        } else {
            LOG_SYS_WARN("LuaAppScanner: %s の%sがtrue/falseではありません (%s)", config_path, key, value);
        }
    }

    // dir_name("/lua/apps/"の下の名前)のアプリを登録する、または登録済みなら読み直す。
    // main.luaが無ければfalse
    bool RegisterDir(const char* app_dir_path, const char* dir_name, bool log_info) {
        FixedString<PICO_PATH_LEN> script_path;
        FixedString<PICO_PATH_LEN> config_path;
        if (!PICO_IO::join(script_path, app_dir_path, "main.lua") ||
            !OSData::SD.exists(script_path.c_str())) {
            return false;
        }

        LuaAppScanner::AppInfo cfg;
        if (PICO_IO::join(config_path, app_dir_path, kAppConfigFileName)) {
            LuaAppScanner::ReadAppConfig(config_path.c_str(), cfg);
        }

        // 表示名はapp.cfgのnameを優先し、無ければディレクトリ名のまま
        const char* display_name = !cfg.name.empty() ? cfg.name.c_str() : dir_name;

        // アイコンはapp_dir基準の相対パスをここで絶対パスへ組み立てる。
        // icon_path変数自身がRegister()呼び出しの間だけ生きていれば十分
        // (Register()がアプリのディレクトリからの相対パスにしてAppEntry::icon_fileへコピーする)
        FixedString<PICO_PATH_LEN> icon_path;
        const char* icon_path_c = nullptr;
        if (!cfg.icon.empty() &&
            PICO_IO::join(icon_path, app_dir_path, cfg.icon.c_str())) {
            icon_path_c = icon_path.c_str();
        }

        if (log_info && (!cfg.description.empty() || !cfg.version.empty())) {
            LOG_SYS_MSG("LuaAppScanner: %s (%s) %s", display_name,
                        cfg.version.empty() ? "-" : cfg.version.c_str(),
                        cfg.description.c_str());
        }

        // 既に登録されていれば(アプリストアで更新した等)、その場で中身だけ差し替える。
        // 外して入れ直すとランチャの並びの位置が変わるため
        const int existing = AppFunctions::FindByArg(script_path.c_str());
        if (existing >= 0) {
            return AppFunctions::Refresh(existing, display_name, cfg.permissions, icon_path_c);
        }

        // 登録失敗(上限到達/名前や引数が異常に長い)はRegister()自身が警告ログを出す
        return AppFunctions::Register(display_name, IconID::AppBox, &MakeLuaAppScene,
                                      script_path.c_str(), cfg.permissions, icon_path_c);
    }
}

bool LuaAppScanner::ReadAppConfig(const char* config_path, AppInfo& out) {
    if (!OSData::SD.exists(config_path)) return false;

    return PICO_Config::ParseFile(config_path,
        [&](const char* key, const char* value) {
            if (strcmp(key, "name") == 0) {
                if (!out.name.assign(value)) {
                    LOG_SYS_WARN("LuaAppScanner: %s のnameが長すぎるため切り詰めました",
                                 config_path);
                }
            } else if (strcmp(key, "description") == 0) {
                out.description.assign(value); // 表示用なので切り詰まっても実害無し
            } else if (strcmp(key, "version") == 0) {
                out.version.assign(value);
            } else if (strcmp(key, "id") == 0) {
                if (!out.id.assign(value)) out.id.clear(); // 切り詰めたidは別物を指すので持たない
            } else if (strcmp(key, "icon") == 0) {
                if (!out.icon.assign(value)) {
                    LOG_SYS_WARN("LuaAppScanner: %s のiconが長すぎるため無視しました",
                                 config_path);
                    out.icon.clear();
                }
            } else if (strcmp(key, "background") == 0) {
                if (!out.background.assign(value)) {
                    LOG_SYS_WARN("LuaAppScanner: %s のbackgroundが長すぎるため無視しました", config_path);
                    out.background.clear();
                }
            } else if (strcmp(key, "background_interval") == 0) {
                int v = 0;
                if (PICO_Config::ConfigValue::AsInt(value, v) && v > 0) out.background_interval_sec = (uint32_t)v;
                else LOG_SYS_WARN("LuaAppScanner: %s のbackground_intervalが秒数ではありません (%s)", config_path, value);
            } else if (strcmp(key, "permission_background") == 0) {
                ReadBool(config_path, key, value, out.permissions.background);
            } else if (strcmp(key, "permission_network") == 0) {
                ReadBool(config_path, key, value, out.permissions.network);
            } else if (strcmp(key, "permission_notify") == 0) {
                ReadBool(config_path, key, value, out.permissions.notify);
            } else if (strcmp(key, "strip_debug") == 0) {
                ReadBool(config_path, key, value, out.permissions.strip_debug);
            } else if (strcmp(key, "permission_sd_outside_app_dir") == 0) {
                ReadBool(config_path, key, value, out.permissions.sd_outside_app_dir);
            }
            // 未知のキーは前方互換のため無視する(discovery/manifestと同じ方針)
        }
    );
}

bool LuaAppScanner::RegisterOrRefresh(const char* app_dir_path) {
    if (!app_dir_path) return false;
    const bool ok = RegisterDir(app_dir_path, PICO_IO::filename(app_dir_path), false);
    NotifyChanged();
    return ok;
}

bool LuaAppScanner::Unregister(const char* app_dir_path) {
    FixedString<PICO_PATH_LEN> script_path;
    if (!app_dir_path || !PICO_IO::join(script_path, app_dir_path, "main.lua")) return false;
    const int index = AppFunctions::FindByArg(script_path.c_str());
    if (index < 0) return false;
    const bool ok = AppFunctions::Unregister(index);
    NotifyChanged();
    return ok;
}

int LuaAppScanner::Scan() {
    if (!OSData::SD_usable) return 0;
    if (!OSData::SD.exists(PICO_Path::DIR::LUA_APPS)) return 0;

    FsFile dir = OSData::SD.open(PICO_Path::DIR::LUA_APPS, O_RDONLY);
    if (!dir || !dir.isDir()) {
        if (dir) dir.close();
        return 0;
    }

    // FileExplorer::update_list()やLuaEngine::l_sd_list()と同じopenNext()の走査。
    // 名前バッファも同じ128B
    int found = 0;
    FsFile entry_file;
    char name[128];
    while (entry_file.openNext(&dir, O_RDONLY)) {
        if (entry_file.isDir() && entry_file.getName(name, sizeof(name)) && name[0] != '.') {
            FixedString<PICO_PATH_LEN> app_dir_path;
            if (PICO_IO::join(app_dir_path, PICO_Path::DIR::LUA_APPS, name) &&
                RegisterDir(app_dir_path.c_str(), name, true)) {
                found++;
            }
        }
        entry_file.close();
    }
    dir.close();

    if (found > 0) {
        LOG_SYS_OK("LuaAppScanner: %s から%d件のLuaアプリを登録しました",
                   PICO_Path::DIR::LUA_APPS, found);
    }
    NotifyChanged();
    return found;
}

int LuaAppScanner::PruneMissing() {
    if (!OSData::SD_usable) return 0;
    const size_t prefix_len = strlen(PICO_Path::DIR::LUA_APPS);
    int removed = 0;
    for (int i = AppFunctions::Count() - 1; i >= 0; i--) {
        const AppEntry* e = AppFunctions::Get(i);
        if (!e || e->create != &MakeLuaAppScene) continue;
        const char* arg = e->arg.c_str();
        if (strncmp(arg, PICO_Path::DIR::LUA_APPS, prefix_len) != 0) continue;
        if (OSData::SD.exists(arg)) continue;
        LOG_SYS_MSG("LuaAppScanner: %s が無くなったので登録簿から外します", arg);
        if (AppFunctions::Unregister(i)) removed++;
    }
    if (removed > 0) NotifyChanged();
    return removed;
}
