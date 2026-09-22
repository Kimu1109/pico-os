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

namespace {
    // アプリディレクトリ配下に置く設定ファイルの相対名。main.lua同様、
    // 絶対パスは呼び出し側(Scan())がapp_dir_pathと組み立てる
    constexpr const char* kAppConfigFileName = "app.cfg";

    // app.cfgから読んだ内容。全キー省略可(未設定ならディレクトリ名/既定権限のまま)。
    // LuaAppScanner.hppの「設定ファイル(app.cfg)」参照
    struct AppConfig {
        FixedString<PICO_STR_M> name;         // 空ならディレクトリ名を使う
        FixedString<PICO_STR_L> description;  // 現状ログにのみ使う(表示UIは未実装)
        FixedString<PICO_STR_S> version;      // 同上
        FixedString<PICO_STR_M> icon;         // アプリディレクトリ内の相対パス(.pimg)
        LuaPermissions permissions;           // 既定は両方false(最小権限)
    };

    // config_pathが存在すれば読み、outへ書き込む。存在しなければ何もしない
    // (「無ければ既定値のまま」という他の設定ファイルと同じ扱い)
    void LoadAppConfig(const char* config_path, AppConfig& out) {
        if (!OSData::SD.exists(config_path)) return;

        PICO_Config::ParseFile(config_path,
            [&](const char* key, const char* value) {
                if (strcmp(key, "name") == 0) {
                    if (!out.name.assign(value)) {
                        LOG_SYS_WARN("LuaAppScanner: %s のnameが長すぎるため切り詰めました",
                                     config_path);
                    }
                } else if (strcmp(key, "description") == 0) {
                    out.description.assign(value); // ログ用途のみなので切り詰まっても実害無し
                } else if (strcmp(key, "version") == 0) {
                    out.version.assign(value);
                } else if (strcmp(key, "icon") == 0) {
                    if (!out.icon.assign(value)) {
                        LOG_SYS_WARN("LuaAppScanner: %s のiconが長すぎるため無視しました",
                                     config_path);
                        out.icon.clear();
                    }
                } else if (strcmp(key, "permission_network") == 0) {
                    bool v = false;
                    if (PICO_Config::ConfigValue::AsBool(value, v)) {
                        out.permissions.network = v;
                    } else {
                        LOG_SYS_WARN("LuaAppScanner: %s のpermission_networkがtrue/false"
                                     "ではありません (%s)", config_path, value);
                    }
                } else if (strcmp(key, "permission_sd_outside_app_dir") == 0) {
                    bool v = false;
                    if (PICO_Config::ConfigValue::AsBool(value, v)) {
                        out.permissions.sd_outside_app_dir = v;
                    } else {
                        LOG_SYS_WARN("LuaAppScanner: %s のpermission_sd_outside_app_dirが"
                                     "true/falseではありません (%s)", config_path, value);
                    }
                }
                // 未知のキーは前方互換のため無視する(discovery/manifestと同じ方針)
            }
        );
    }
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
        if (entry_file.isDir() && entry_file.getName(name, sizeof(name))) {
            FixedString<PICO_PATH_LEN> app_dir_path;
            FixedString<PICO_PATH_LEN> script_path;
            FixedString<PICO_PATH_LEN> config_path;

            if (PICO_IO::join(app_dir_path, PICO_Path::DIR::LUA_APPS, name) &&
                PICO_IO::join(script_path, app_dir_path, "main.lua") &&
                OSData::SD.exists(script_path.c_str())) {

                AppConfig cfg;
                if (PICO_IO::join(config_path, app_dir_path, kAppConfigFileName)) {
                    LoadAppConfig(config_path.c_str(), cfg);
                }

                // 表示名はapp.cfgのnameを優先し、無ければディレクトリ名のまま
                const char* display_name = !cfg.name.empty() ? cfg.name.c_str() : name;

                // アイコンはapp_dir基準の相対パスをここで絶対パスへ組み立てる。
                // icon_path変数自身がRegister()呼び出しの間だけ生きていれば十分
                // (Register()内でAppEntry::icon_pathへコピーされる)
                FixedString<PICO_PATH_LEN> icon_path;
                const char* icon_path_c = nullptr;
                if (!cfg.icon.empty() &&
                    PICO_IO::join(icon_path, app_dir_path, cfg.icon.c_str())) {
                    icon_path_c = icon_path.c_str();
                }

                if (!cfg.description.empty() || !cfg.version.empty()) {
                    LOG_SYS_MSG("LuaAppScanner: %s (%s) %s", display_name,
                                cfg.version.empty() ? "-" : cfg.version.c_str(),
                                cfg.description.c_str());
                }

                if (AppFunctions::Register(display_name, IconID::AppBox, &MakeLuaAppScene,
                                            script_path.c_str(), cfg.permissions,
                                            icon_path_c)) {
                    found++;
                }
                // 登録失敗(上限到達/名前や引数が異常に長い)はRegister()自身が
                // 警告ログを出すので、ここで重ねてログを出す必要はない
            }
        }
        entry_file.close();
    }
    dir.close();

    if (found > 0) {
        LOG_SYS_OK("LuaAppScanner: %s から%d件のLuaアプリを登録しました",
                   PICO_Path::DIR::LUA_APPS, found);
    }
    return found;
}
