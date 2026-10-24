#pragma once

#include "gui/core/Scene.hpp"
#include "gui/widgets/Label.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

class Button;
class ScrollList;

// 最近開いたファイル(RecentFiles)の一覧。ファイルアプリの「他」→「最近開いたファイル」からPush()される。
//
//   [←] 最近開いたファイル
//   ・名前  フォルダ         … 新しい順。もう無いファイルは並べる前に一覧から外す
// 2回タップでファイルビューワー(FileViewerScene)で開く(文字のファイルはそこの「編集」でエディタへ)。
// 一覧のパスは画面を出している間だけ確保する(ファイルを開くと onExit() で放す)。
class RecentFilesScene : public Scene {
    public:
        const char* getName() const override { return "RecentFiles"; }

        void onEnter() override;
        void onExit() override;

        int count() const { return this->count_; }
        const char* pathAt(int i) const { return (this->paths_ && i >= 0 && i < this->count_) ? this->paths_[i].c_str() : ""; }

    private:
        static constexpr int MARGIN = 3;

        Button* back_button_ = nullptr;
        Label<PICO_STR_L>* empty_label_ = nullptr;
        ScrollList* list_ = nullptr;

        FixedString<PICO_PATH_LEN>* paths_ = nullptr;
        int count_ = 0;

        void reload();
        void open(int index);
};
