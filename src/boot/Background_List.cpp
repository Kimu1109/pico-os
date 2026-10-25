#include "system/Background_Functions.hpp"
#include "gui/core/Scene_Functions.hpp"
#include "apps/music/Music_Service.hpp"
#include "apps/chat/Chat_Watch.hpp"
#include "apps/todo/Todo_Watch.hpp"
#include "lua/LuaBackground.hpp"
#include "lua/LuaAppScanner.hpp"
#include "gui/scenes/LuaScene.hpp"
#include "config/App_Config.hpp"

#include <cstring>

// 組み込みのバックグラウンドの仕事を登録する(アプリの登録簿の App_List.cpp と同じ役割)。
// 仕組み(登録簿・入り切りの保存)は Background_Functions.cpp。
//
// 仕事を増やすときは BackgroundService を継承したものを作り、ここで Add() するだけ。
// ビルド時に外せるアプリ(config/App_Config.hpp)の仕事は、そのアプリと一緒に外す。
void BackgroundFunctions::Setup(){
    //ミュージックの再生(アプリを閉じても鳴り続ける)
#if PICO_APP_MUSIC
    Add(MusicService::Service());
#endif

    //チャットの未読(チャットアプリが開いている間は画面に任せる)
#if PICO_APP_CHAT
    ChatWatch::foreground_query = [](){
        const Scene* cur = SceneFunctions::Current();
        return cur && strcmp(cur->getName(), "Chat") == 0;
    };
    Add(ChatWatch::Service(), true);
#endif

    //ホーム画面のTODOウィジェットの取り直し(ウィジェットを置いている間だけ。TODOアプリが開いている間は画面に任せる)
#if PICO_APP_TODO
    TodoWatch::foreground_query = [](){
        const Scene* cur = SceneFunctions::Current();
        return cur && strcmp(cur->getName(), "Todo") == 0;
    };
    Add(TodoWatch::Service(), true);
#endif

    //Luaアプリのバックグラウンドスクリプト(app.cfg の background)
    LuaBackground::foreground_script = []() -> const char* {
        Scene* cur = SceneFunctions::Current();
        if(!cur || strcmp(cur->getName(), "Lua") != 0) return nullptr;
        return static_cast<LuaScene*>(cur)->scriptPath();
    };
    LuaBackground::Refresh();
    //アプリストアで入れ替えた・USBドライブでPCから書き換えたら読み直す
    LuaAppScanner::apps_changed = [](){ LuaBackground::Refresh(); };
}
