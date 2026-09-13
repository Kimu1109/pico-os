#include "functions/App_Functions.hpp"
#include "functions/Log_Functions.hpp"

#include "gui/scenes/MarkdownScene.hpp"
#include "gui/scenes/InputTestScene.hpp"

// このOSに載せるアプリの一覧。
//
// アプリを増やすときは、シーンのヘッダをincludeして下の並びへ1行足すだけでよい。
// 並べた順にランチャへ表示される。
//
// 登録簿の仕組み(Register/Launch等)は App_Functions.cpp にあり、
// このファイルとは分けてある。仕組み側だけならシーンの実装に依存せずに
// ビルドできるので、ホストテストが軽く保てる。
void AppFunctions::Setup(){
    Clear();

    // ---- ここへ1行足すとランチャに並ぶ ----
    Register("Markdown",   IconID::File,     &MakeScene<MarkdownScene>);
    Register("入力テスト", IconID::Keyboard, &MakeScene<InputTestScene>);

    LOG_SYS_OK("App Setup has succeeded! (%d apps)", Count());
}
