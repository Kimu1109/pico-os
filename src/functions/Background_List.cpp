#include "functions/Background_Functions.hpp"
#include "functions/Scene_Functions.hpp"
#include "sound/Music_Service.hpp"
#include "chat/Chat_Watch.hpp"
#include "lua/LuaBackground.hpp"
#include "lua/LuaAppScanner.hpp"
#include "gui/scenes/LuaScene.hpp"

#include <cstring>

// 組み込みのバックグラウンドの仕事を登録する(アプリの登録簿の App_List.cpp と同じ役割)。
// 仕組み(登録簿・入り切りの保存)は Background_Functions.cpp。
//
// 仕事を増やすときは BackgroundService を継承したものを作り、ここで Add() するだけ。
void BackgroundFunctions::Setup(){
    //ミュージックの再生(アプリを閉じても鳴り続ける)
    Add(MusicService::Service());

    //チャットの未読(チャットアプリが開いている間は画面に任せる)
    ChatWatch::foreground_query = [](){
        const Scene* cur = SceneFunctions::Current();
        return cur && strcmp(cur->getName(), "Chat") == 0;
    };
    Add(ChatWatch::Service(), true);

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
