#include "functions/App_Functions.hpp"
#include "functions/Log_Functions.hpp"

#include "gui/scenes/MarkdownScene.hpp"
#include "gui/scenes/InputTestScene.hpp"
#include "gui/scenes/ClocksScene.hpp"
#include "gui/scenes/CalculatorScene.hpp"
#include "gui/scenes/FileExplorerScene.hpp"
#include "gui/scenes/SettingsScene.hpp"
#include "gui/scenes/DictScene.hpp"
#include "gui/scenes/LuaScene.hpp"
#include "lua/LuaPermissions.hpp"

namespace {
    // "Lua Hello"デモ用の生成関数。MakeSceneWithArg<LuaScene>を使わず専用の関数に
    // してあるのは、このアプリだけ"/lua/"の外(/img/hello.pimg)を読むための
    // sd_outside_app_dir権限が要るため(既定はLuaScene同様、両方false=最小権限)。
    // Luaアプリが増えて権限の組み合わせも増えてきたら、AppEntryへ権限を持たせる形へ
    // 一般化することを検討する(CLAUDE.md「Luaバインディング」「権限」参照)
    Scene* MakeLuaHelloScene(const AppEntry& entry) {
        LuaPermissions permissions;
        permissions.sd_outside_app_dir = true;
        return new LuaScene(entry.arg.c_str(), permissions);
    }
}

// このOSに載せるアプリの一覧。
//
// アプリを増やすときは、シーンのヘッダをincludeして下の並びへ1行足すだけでよい。
// 並べた順にランチャへ表示される。
//
// 登録簿の仕組み(Register/Launch等)は App_Functions.cpp にあり、
// このファイルとは分けてある。仕組み側だけならシーンの実装に依存せずに
// ビルドできるので、ホストテストが軽く保てる。
//
// 生成関数は2種類:
//   MakeScene<T>        … 引数なしでシーンを作る
//   MakeSceneWithArg<T> … Register()の第4引数(arg)をコンストラクタへ渡す。
//                         同じシーン型を別のargで何度でも登録できるので、
//                         「文書ごとに1タイル」「Luaスクリプトごとに1タイル」が作れる
void AppFunctions::Setup(){
    Clear();

    // ---- ここへ1行足すとランチャに並ぶ ----
    //引数を渡さないと network.cfg の browser-home を開く(無ければ同梱のサンプル)。
    //特定の文書を固定で開くタイルにしたい場合は MakeSceneWithArg + パス/URL を渡す
    Register("Markdown",   IconID::Browser,  &MakeScene<MarkdownScene>);
    Register("入力テスト", IconID::Keyboard, &MakeScene<InputTestScene>);
    Register("時計", IconID::Clock, &MakeScene<ClocksScene>);
    Register("電卓", IconID::Calculator, &MakeScene<CalculatorScene>);
    Register("ファイル", IconID::Folder, &MakeScene<FileExplorerScene>);
    Register("設定", IconID::Settings, &MakeScene<SettingsScene>);
    Register("辞書", IconID::Language, &MakeScene<DictScene>);
    // Luaバインディングの動作サンプル(pc/sdcard/lua/hello.lua参照)。
    // MakeLuaHelloScene()(上記)がsd_outside_app_dir権限付きでLuaSceneを生成する
    // (/img/hello.pimgを読むため)。権限が要らないLuaアプリなら
    // MakeSceneWithArg<LuaScene>にargだけ変えて登録すればよい
    Register("Lua Hello", IconID::AppBox, &MakeLuaHelloScene, "/lua/hello.lua");

    LOG_SYS_OK("App Setup has succeeded! (%d apps)", Count());
}
