#pragma once

#include "gui/core/Scene.hpp"
#include "apps/reader/Reader_Book.hpp"
#include "apps/reader/Reader_File.hpp"
#include "apps/reader/Reader_Store.hpp"
#include "gui/widgets/Label.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

class ReaderView;
class Button;
class SeekBar;
class PickerDialog;

// 読書アプリの本を読む画面(本1冊)。本棚(ReaderLibraryScene)やファイルビューワーの「読む」から Push() される。
//
// 本文は ReaderView が1ページずつ描く。左右を叩く・払う・→← キーでページを送る。真ん中を叩くとメニュー:
//   上の帯 … [本棚](戻る)と題名
//   下の帯 … 本の中の位置のバー(離すとそこへ飛ぶ)/ [しおり](今のページに付ける。付いていれば[外す])/
//            [一覧](目次としおりを位置の順に並べて飛ぶ)/ [文字](16px⇔24px)/ [ルビ](出す⇔隠す)
// 続きの位置は ReaderStore に覚える(ページを送って3秒たったら・画面を離れるとき)。次に開くとそこから。
//
// ページの索引(ページ番号・前へ・目次)は onUpdate() で毎フレーム少しずつ作る。数え終わるまで
// ページ番号は「12/…」と出す。
class ReaderScene : public Scene {
    public:
        explicit ReaderScene(const char* path);
        ~ReaderScene() override;

        const char* getName() const override { return "Reader"; }

        void onEnter() override;
        void onExit() override;
        void onUpdate() override;
        bool onKey(const KeyInputFunctions::Event& ev) override;

    private:
        static constexpr int kTopBand = 30;
        static constexpr int kBottomBand = 58;
        static constexpr uint32_t kSaveDelayMs = 3000;
        static constexpr uint32_t kIndexBudgetMs = 8;

        FixedString<PICO_PATH_LEN> path_;
        FixedString<PICO_STR_L> title_;
        ReaderFile file_;
        ReaderBook* book_ = nullptr;
        ReaderStore::Settings settings_;
        ReaderStore::Mark marks_[ReaderStore::kMaxMarks];
        int mark_count_ = 0;
        // 画面を離れて戻ってきたとき(Pop()で戻る等)に同じ所を開くため
        ReaderLayout::State resume_;
        bool has_resume_ = false;
        bool loaded_ = false;      // しおりを読んだ(初めて開いたときだけ読む)

        bool dirty_ = false;       // 続きの位置が変わってまだ書いていない
        uint32_t changed_ms_ = 0;
        int shown_page_ = -2;
        bool shown_complete_ = false;

        ReaderView* view_ = nullptr;
        Button* back_button_ = nullptr;
        Label<PICO_STR_L>* title_label_ = nullptr;
        SeekBar* seek_ = nullptr;
        Button* mark_button_ = nullptr;
        Button* list_button_ = nullptr;
        Button* font_button_ = nullptr;
        Button* ruby_button_ = nullptr;
        PickerDialog* picker_ = nullptr;
        bool menu_ = false;

        // 一覧(目次・しおり)に出している項目。ScrollListの項目は1つ約260バイトあるので数を抑える
        static constexpr int kMaxListItems = 64;
        struct ListItem {
            bool mark;
            int index;
            uint32_t pos;
        };
        ListItem list_items_[kMaxListItems];
        int list_count_ = 0;

        bool openBook();
        void closeBook();
        void buildMenu();
        void setMenu(bool on);
        void refresh();
        void refreshFooter();
        void turn(int dir);
        void markChanged();
        void save();
        int markOnPage() const;
        void toggleMark();
        void openList();
        void applyParams();
};
