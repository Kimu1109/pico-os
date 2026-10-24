#pragma once

#include "gui/core/Scene.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/ScrollList.hpp"
#include "storage/Sd_Walker.hpp"
#include "util/FixedString.hpp"
#include "util/Text_Matcher.hpp"

// ファイル検索。SD全体(またはrootの下)を、名前に検索語を含むファイル/フォルダ(Mode::Name)か、
// 中身に検索語を含む文字のファイル(Mode::Content、全文検索)で探す。
// ファイルアプリの「他」→「検索(名前)」「検索(中身)」からPush()される。
//
// - 探すのは onUpdate() で1フレームに kStepBudgetUs ぶんだけ(SdWalkerで1件ずつ進む)。フレームを止めない
// - 比べ方は大文字/小文字を区別しない(ASCIIだけ)部分一致。日本語はバイト列のまま
// - 全文検索は、文字のファイル(FileIcon::IsText。.txt .md .lua .csv …)を kChunkBytes ずつ読んで TextMatcher へ流す。
//   ファイルを開いたまま次のフレームへ持ち越すので、大きなファイルでもフレームを止めない。
//   /sys/ の下(OSの設定・鍵)と kMaxContentBytes より大きなファイルは見ない。
//   一致したら、最初の一致の行番号と、その行の抜粋を一覧に出す
// - 見つけたものは kMaxResults 件まで(それより多ければ「さらにあり」と出して探すのをやめる)
// - 結果を2回タップ: ファイルはファイルビューワーで開く、フォルダはファイルアプリへ戻ってそのフォルダを開く
//   (TakeJump()。ファイルアプリが次の onEnter() で受け取る)
class FileSearchScene : public Scene {
    public:
        enum class Mode : uint8_t { Name, Content };

        static constexpr int kMaxResults = 32;
        static constexpr uint32_t kStepBudgetUs = 6000;
        static constexpr int kChunkBytes = 512;
        static constexpr uint32_t kMaxContentBytes = 1024u * 1024u;

        FileSearchScene(const char* root, const char* query, Mode mode = Mode::Name);
        ~FileSearchScene() override { this->closeFile(); }

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
        // 全文検索の結果の、最初に一致した行(1始まり)と抜粋
        uint32_t resultLine(int i) const { return (i >= 0 && i < this->result_count) ? this->result_line[i] : 0; }
        const char* resultSnippet(int i) const { return (i >= 0 && i < this->result_count) ? this->result_snippet[i].c_str() : ""; }
        bool finished() const { return this->done; }
        Mode mode() const { return this->mode_; }

    private:
        static constexpr int MARGIN = 3;

        FixedString<PICO_PATH_LEN> root;
        FixedString<PICO_STR_M> query;
        Mode mode_ = Mode::Name;
        SdWalker walker;
        // 全文検索で読んでいる途中のファイル(フレームをまたいで開いたまま)
        FsFile file;
        FixedString<PICO_PATH_LEN> file_path;
        TextMatcher matcher;
        bool started = false;
        bool done = false;
        bool overflow = false;
        uint32_t scanned = 0;

        FixedString<PICO_PATH_LEN> results[kMaxResults];
        bool result_is_dir[kMaxResults] = {};
        uint32_t result_line[kMaxResults] = {};
        FixedString<PICO_STR_L> result_snippet[kMaxResults];
        int result_count = 0;
        int shown_count = -1;     // 一覧に並べた数(増えたら足す)
        bool shown_done = false;

        Button* back_button = nullptr;
        Button* again_button = nullptr;
        Label<PICO_STR_L>* status_label = nullptr;
        ScrollList* list = nullptr;

        void restart();
        void step();
        // 全文検索: 今のファイルを少し読む。読み終えた(一致した・終わりまで読んだ)ら閉じる
        void stepFile();
        void addResult(const char* path, bool is_dir);
        void closeFile();
        void refreshList();
        void refreshStatus();
        void openResult(int index);
        void askQuery();

        static inline FixedString<PICO_PATH_LEN> pending_jump;
        static inline bool has_jump = false;
};
