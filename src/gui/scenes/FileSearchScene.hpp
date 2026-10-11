#pragma once

#include "gui/scenes/Scene.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/ScrollList.hpp"
#include "storage/Sd_Walker.hpp"
#include "util/FixedString.hpp"

// ファイル検索。SD全体(またはrootの下)を、名前に検索語を含むファイル/フォルダで探す。
// ファイルアプリの「他」→「名前で検索」からPush()される。
//
// - 探すのは onUpdate() で1フレームに kStepBudgetUs ぶんだけ(SdWalkerで1件ずつ進む)。フレームを止めない
// - 比べ方は大文字/小文字を区別しない(ASCIIだけ)部分一致。日本語はバイト列のまま
// - 見つけたものは kMaxResults 件まで(それより多ければ「さらにあり」と出して探すのをやめる)
// - 結果を2回タップ: ファイルはファイルビューワーで開く、フォルダはファイルアプリへ戻ってそのフォルダを開く
//   (TakeJump()。ファイルアプリが次の onEnter() で受け取る)
class FileSearchScene : public Scene {
    public:
        static constexpr int kMaxResults = 32;
        static constexpr uint32_t kStepBudgetUs = 6000;

        FileSearchScene(const char* root, const char* query);

        const char* getName() const override { return "FileSearch"; }
        void onEnter() override;
        void onExit() override;
        void onUpdate() override;

        // フォルダの結果を選んだとき、ファイルアプリが開くべきフォルダ(1回きり)
        static bool TakeJump(FixedString<PICO_PATH_LEN>& out);

        // 名前が検索語を含むか(ASCIIの大文字/小文字を区別しない)
        static bool NameMatches(const char* name, const char* query);

        int resultCount() const { return this->result_count; }
        const char* resultAt(int i) const { return (i >= 0 && i < this->result_count) ? this->results[i].c_str() : ""; }
        bool finished() const { return this->done; }

    private:
        static constexpr int MARGIN = 3;

        FixedString<PICO_PATH_LEN> root;
        FixedString<PICO_STR_M> query;
        SdWalker walker;
        bool started = false;
        bool done = false;
        bool overflow = false;
        uint32_t scanned = 0;

        FixedString<PICO_PATH_LEN> results[kMaxResults];
        bool result_is_dir[kMaxResults] = {};
        int result_count = 0;
        int shown_count = -1;     // 一覧に並べた数(増えたら足す)
        bool shown_done = false;

        Button* back_button = nullptr;
        Button* again_button = nullptr;
        Label<PICO_STR_L>* status_label = nullptr;
        ScrollList* list = nullptr;

        void restart();
        void step();
        void refreshList();
        void refreshStatus();
        void openResult(int index);
        void askQuery();

        static inline FixedString<PICO_PATH_LEN> pending_jump;
        static inline bool has_jump = false;
};
