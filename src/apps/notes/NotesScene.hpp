#pragma once

#include "gui/core/Scene.hpp"
#include "gui/widgets/Label.hpp"
#include "apps/notes/Note_Parse.hpp"
#include "storage/Sd_Walker.hpp"
#include "util/FixedString.hpp"
#include "util/Text_Matcher.hpp"
#include "consts.hpp"

class Button;
class ScrollList;

// メモ(ランチャの「メモ」)。SDの /notes/ の下のテキスト(*.txt / *.md)を、一覧・タグ・全文検索で引く。
//
//   [←][新規][検索][タグ]
//   #仕事「会議」 2件            [×]   … 今の絞り込み。[×]で解く
//   ・題名(最初の行)                    … 最近開いた順、次に名前の新しい順(新規メモの名前は日時)
// 2回タップでテキストエディタ(TextEditorScene)で開く。[新規]は /notes/ に日時の名前で空のメモを作って開く。
//
// - 題名とタグは NoteParser が中身から拾う。タグは本文に書いた "#言葉"(別のファイルを持たない)
// - 検索は全文(TextMatcher)。タグと検索は重ねて絞り込める
// - 中身を読むのは onUpdate() で1フレームに kStepBudgetUs ぶんだけ。メモを開いて戻ってくるたびに読み直す
//   (エディタで題名やタグが変わっているかもしれないので)。検索語を変えたときも読み直す
// - メモの一覧(Index)は画面を出している間だけ確保する(エディタへ行くと onExit() で放す)。
//   覚えておくのは絞り込み(タグの名前と検索語)だけ
class NotesScene : public Scene {
    public:
        static constexpr const char* kAppName = "メモ";
        static constexpr int kMaxNotes = 64;
        static constexpr int kMaxTags = 32;
        static constexpr uint32_t kStepBudgetUs = 6000;
        static constexpr int kChunkBytes = 512;
        // これより後ろは読まない(題名とタグは頭のほうにある。エディタで開けるのは32KiBまで)
        static constexpr uint32_t kMaxReadBytes = 64u * 1024u;

        struct Note {
            FixedString<PICO_PATH_LEN> path;
            FixedString<NoteParser::kTitleBytes + 1> title;
            uint32_t tags = 0;          // Index::tags の何番目を持つか(ビット)
            uint16_t hits = 0;          // 検索語が出てきた数
            int16_t recent = -1;        // 最近開いたファイルの何番目か(-1 = 入っていない)
        };
        struct Tag {
            FixedString<NoteParser::kMaxTagBytes + 1> name;
            uint16_t count = 0;
        };
        struct Index {
            Note notes[kMaxNotes];
            int count = 0;
            bool overflow = false;      // kMaxNotes を超えたぶんは載せていない
            Tag tags[kMaxTags];
            int tag_count = 0;
            int16_t order[kMaxNotes];   // 絞り込んで並べた順(notes の番号)
            int shown = 0;
        };

        const char* getName() const override { return "Notes"; }
        void onEnter() override;
        void onExit() override;
        void onUpdate() override;
        ~NotesScene() override;

        // 絞り込み(テスト・外から)。空なら解く。どちらも読み直しは onUpdate() で
        void setQuery(const char* q);
        void setTagFilter(const char* tag);

        // ---- テスト用 ----
        bool scanning() const { return this->scanning_; }
        const Index* index() const { return this->index_; }
        // 絞り込んだ一覧の i 番目
        const Note* shownAt(int i) const;
        int shownCount() const { return this->index_ ? this->index_->shown : 0; }
        // 新しいメモのパスを作る(/notes/ の中で重ならない名前)
        static bool MakeNewNotePath(FixedString<PICO_PATH_LEN>& out);
        static bool IsNoteFile(const char* name);

    private:
        static constexpr int MARGIN = 3;

        Button* back_button_ = nullptr;
        Button* new_button_ = nullptr;
        Button* search_button_ = nullptr;
        Button* tag_button_ = nullptr;
        Button* clear_button_ = nullptr;
        Label<PICO_STR_L>* status_label_ = nullptr;
        Label<PICO_STR_LL>* empty_label_ = nullptr;
        ScrollList* list_ = nullptr;

        // 絞り込み(画面を出ても残す)
        FixedString<PICO_STR_M> query_;
        FixedString<NoteParser::kMaxTagBytes + 1> tag_filter_;

        // 読み込み
        Index* index_ = nullptr;
        SdWalker walker_;
        FsFile file_;
        uint32_t file_read_ = 0;
        NoteParser parser_;
        TextMatcher matcher_;
        bool scanning_ = false;
        bool rescan_ = false;

        void startScan();
        void stopScan();
        void stepScan();
        // 読んでいるメモを少し読む。読み終えたら一覧へ足して閉じる
        void stepFile();
        void finishFile();
        void finishScan();
        static void OnTag(void* ctx, const char* tag);
        int tagIndexOf(const char* name) const;
        // タグを名前の順に並べた番号を order へ。数を返す
        int sortedTags(int* order) const;

        void applyFilter();
        void refreshList();
        void refreshStatus();

        void onNew();
        void askQuery();
        void pickTag();
        void open(int shown_index);
};
