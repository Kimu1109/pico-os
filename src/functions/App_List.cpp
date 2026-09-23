#include "functions/App_Functions.hpp"
#include "functions/Log_Functions.hpp"

#include "gui/scenes/MarkdownScene.hpp"
#include "gui/scenes/InputTestScene.hpp"
#include "gui/scenes/ClocksScene.hpp"
#include "gui/scenes/CalculatorScene.hpp"
#include "gui/scenes/FileExplorerScene.hpp"
#include "gui/scenes/SettingsScene.hpp"
#include "gui/scenes/DictScene.hpp"
#include "gui/scenes/CalendarScene.hpp"
#include "gui/scenes/LuaScene.hpp"
#include "lua/LuaPermissions.hpp"
#include "lua/LuaAppScanner.hpp"

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
    Register("カレンダー", IconID::Calendar, &MakeScene<CalendarScene>);
    // Luaバインディングの動作サンプル(pc/sdcard/lua/hello.lua参照)。
    // MakeLuaAppScene(LuaScene.hpp)がentry.permissionsをそのままLuaSceneへ渡すので、
    // ここでsd_outside_app_dirを立てるだけで済む(/img/hello.pimgを読むため)。
    // 権限が要らないLuaアプリなら第5引数(permissions)を省略すればよい
    Register("Lua Hello", IconID::AppBox, &MakeLuaAppScene, "/lua/hello.lua",
             LuaPermissions{false, true});

    // "/lua/apps/<名前>/main.lua" を走査し、見つかった分をここまでの静的登録へ
    // 追加する(LuaAppScanner.hppのクラスコメント参照)。SD無し/ディレクトリが
    // 無い場合は何もしない。静的登録の後に置くことで、同名のLuaアプリが
    // SD側にもあった場合、後勝ちでSD側が優先される(登録簿は先勝ちではなく
    // 単純追記なので、実際には2タイル並ぶ点に注意。今のところ名前の重複チェックは
    // していない)
    LuaAppScanner::Scan();

    LOG_SYS_OK("App Setup has succeeded! (%d apps)", Count());
}
