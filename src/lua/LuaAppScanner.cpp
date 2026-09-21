#include "lua/LuaAppScanner.hpp"

#include <SdFat.h>

#include "functions/App_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "gui/scenes/LuaScene.hpp"
#include "storage/SD_IO.hpp"
#include "storage/SD_Path.hpp"
#include "util/FixedString.hpp"
#include "OS_Data.hpp"
#include "consts.hpp"

namespace {
    // 見つかった各アプリの生成関数。権限は既定(LuaPermissions{}、network/
    // sd_outside_app_dirとも false)固定(LuaAppScanner.hppのクラスコメント参照)。
    // app_dirはLuaScene::onEnter()がscript_path(=".../<名前>/main.lua")の
    // 親ディレクトリから毎回計算し直すので、ここで明示的に渡す必要はない
    Scene* MakeScannedLuaApp(const AppEntry& entry) {
        return new LuaScene(entry.arg.c_str());
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

            if (PICO_IO::join(app_dir_path, PICO_Path::DIR::LUA_APPS, name) &&
                PICO_IO::join(script_path, app_dir_path, "main.lua") &&
                OSData::SD.exists(script_path.c_str())) {
                if (AppFunctions::Register(name, IconID::AppBox, &MakeScannedLuaApp,
                                            script_path.c_str())) {
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
