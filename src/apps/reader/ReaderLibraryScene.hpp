#pragma once

#include "gui/core/Scene.hpp"
#include "gui/widgets/Label.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

class Button;
class ScrollList;
class FileSelectDialog;

// 読書アプリの本棚(ランチャの「読書」)。
//
//   [←] 本棚                 [開く]
//   [ 続きから: 蜘蛛の糸 (12%) ]      … 最後に読んだ本を、覚えている位置から
//   ・最近読んだ本(読んだ割合つき)
//   ・SDの /books/ にあって、まだ開いていない本(*.txt)
// 項目を叩くとその本を読む画面(ReaderScene)を Push() する。[開く]はファイル選択で SD のどこからでも選べる。
// 一覧のパスは画面を出している間だけ確保する(本を読む画面へ行くと onExit() で放す)。
class ReaderLibraryScene : public Scene {
    public:
        static constexpr const char* kAppName = "読書";

        const char* getName() const override { return "ReaderLibrary"; }

        void onEnter() override;
        void onExit() override;
        bool onKey(const KeyInputFunctions::Event& ev) override;

    private:
        static constexpr int kMaxEntries = 48;
        static constexpr int MARGIN = 4;

        Button* back_button_ = nullptr;
        Button* open_button_ = nullptr;
        Button* continue_button_ = nullptr;
        Label<PICO_STR_L>* empty_label_ = nullptr;
        ScrollList* list_ = nullptr;
        FileSelectDialog* picker_ = nullptr;

        FixedString<PICO_PATH_LEN>* paths_ = nullptr;   // 一覧の項目ごとの本のパス
        int count_ = 0;
        int recent_count_ = 0;
        int list_top_ = 0;            // 一覧の上端(続きから読む本が無いとき)
        int list_top_continue_ = 0;   // 一覧の上端(続きからのボタンの下)
        int list_bottom_ = 0;

        void reload();
        void openBook(const char* path);
        void openPicker();
};
