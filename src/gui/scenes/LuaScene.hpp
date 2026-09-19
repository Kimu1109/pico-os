#pragma once

#include "gui/scenes/Scene.hpp"
#include "lua/LuaEngine.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

// SD上のLuaスクリプトを1本読んで実行する画面。
// AppEntry::argへスクリプトパス("/lua/hello.lua"のようなSD絶対パス)を渡し、
// AppFunctions::MakeSceneWithArg<LuaScene>で登録する(同じLuaScene型を別のargで
// 何個でも登録できるので「Luaスクリプトごとに1タイル」が作れる)。
//
// ウィジェットの生成・削除・イベント配線は全てスクリプト側がpico.*を呼んで行うため、
// このシーン自身が直接newするウィジェットは無い。onEnter/onExitはLuaEngineの
// 生成/破棄と、スクリプトのSDからの読み込みだけを見る。
//
// ランチャへ戻る手段(戻るボタン等)はスクリプト側がpico.create("Button")+
// pico.on(id, "press_start", ...)でpico.pop()を呼ぶ形で自前で用意する
// (ClocksScene/CalculatorScene等、他のアプリの「戻る」ボタンと同じ考え方)。
class LuaScene : public Scene {
    private:
        // このLuaアプリに許すメモリ予算(lua_newstateのカスタムallocへ渡す上限)。
        // CLAUDE.md「RAM/Flash予算」の暫定枠(200KB)
        static constexpr size_t kLuaBudgetBytes = 200 * 1024;

        // スクリプトソースの読み込み上限。MarkdownView::kMdMaxSourceBytes(8KiB)より
        // 大きく取ってある(pico.*呼び出しの羅列でUIを組み立てるスクリプトは
        // Markdown文書より冗長になりがちなため)。超えた分はloadAndRun()が警告して打ち切る。
        // MarkdownViewのdoc_textと同じ理由でメンバ(=ヒープ上のLuaScene本体の一部)に持たせ、
        // スタック上には置かない(16KiBはスタックに置くには大きすぎる)。
        // そのぶんLuaScene自体がMarkdownScene同様「シーン本体は数十バイト」の例外になる
        static constexpr size_t kMaxScriptBytes = PICO_STR_16KiB;
        FixedString<kMaxScriptBytes> script_source;

        FixedString<PICO_PATH_LEN> script_path;
        LuaEngine* engine = nullptr;

        // スクリプトをSDから読み込み、engineへ渡して実行する。
        // 読み込み失敗(ファイルが無い等)やLuaEngine::Run()の失敗はErrorFunctions側で
        // 既にダイアログ表示済みなので、ここでは追加のエラー表示をしない
        void loadAndRun();

    public:
        explicit LuaScene(const char* path) { script_path.assign(path); }

        const char* getName() const override { return "Lua"; }

        void onEnter() override;
        void onExit() override;

        // テスト・デバッグ用の脱出口(LuaEngine::raw()と同じ位置づけ)。
        // アクティブでない間(onExit()後)はnullptr
        LuaEngine* getEngine() const { return engine; }
};
