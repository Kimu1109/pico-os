#pragma once

#include "gui/core/Scene.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/TextView.hpp"
#include "gui/widgets/interfaces/ITextInputTarget.hpp"
#include "util/FixedString.hpp"
#include "apps/texteditor/Edit_History.hpp"

#include "util/InplaceFunction.hpp"

// シンプルなテキストエディタ。
//
// スマホの文字入力と同じく、本文(TextView)の下にオンスクリーンキーボードを据え置いて
// 直接書き込む(KeyboardFunctions::Show(..., docked=true))。キーボードは右上のボタンで
// 出し入れでき、本文をタップするとそこへカーソルが移ってキーボードが開く。
//
// キーボードの入力バッファは192バイト(FixedString<PICO_STR_LL>)なので、文書全体ではなく
// **カーソルのある1行だけ**をキーボードへ渡して編集させ、変わるたびに文書へ書き戻す
// (onDisplayChanged())。行をまたぐ操作はシーン側が受け持つ:
//   - 改行キー             … 受け取ったテキストの'\n'で行を割り、後ろ半分を次の行としてキーボードへ渡し直す
//   - 行頭での1文字削除     … 前の行と繋げる(onBackspaceAtStart())
//   - 行頭での← / 行末での→ … 前の行の末尾 / 次の行の頭へ移る(onCursorAtEdge())
//
// 文書は固定長の1本のバッファ(kMaxBytes)に'\n'区切りで持つ(行ごとにヒープを確保しない)。
// 上限を超えるファイル(容量・行数・1行の長さ)は、保存で内容が消えないよう開かずに断る。
//
// 文書を書き換えるのは必ず edit() を通す。edit() が変わった部分だけを EditHistory(4KiB)へ記録するので、
// 元に戻す/やり直し(Ctrl+Z / Ctrl+Y)ができる。未保存かどうかも記録の位置で決まる(元に戻して保存した時点へ
// 戻れば未保存でなくなる)。
//
// 選択範囲は「選択の起点(sel_anchor)〜カーソル」。メニューの「範囲選択」で起点を置いてから終わりをタップする
// (物理キーボードはShift+矢印)。文字を打つと選択は外れる(選択範囲を置き換えはしない)。Backspace/Deleteは
// 選択範囲を消す。コピー/切り取りは選択が無ければ今の行(改行込み)を対象にする。クリップボードはOS全体の
// ClipboardFunctions。
//
// 検索/置換は上部に検索バー(件数・前・次・置換・すべて・閉じる)を出す。見つけた所は選択範囲として見せる。
class TextEditorScene : public Scene, public ITextInputTarget {
    private:
        static constexpr int kMaxBytes = PICO_STR_32KiB;
        static constexpr int kMaxLines = 1000;
        // キーボードの入力バッファ(FixedString<PICO_STR_LL>)に収まる1行の最大バイト数
        static constexpr int kMaxLineBytes = PICO_STR_LL - 1;

        static constexpr int MARGIN = 3;

        // ダイアログからダイアログへは1フレーム空ける(MarkdownScene::Pendingと同じ理由)
        enum class Pending { None, OpenPicker, OpenFind, OpenReplace, OpenReplaceWith, NewDocument, OpenRecent };

        char text[kMaxBytes + 1] = {0};
        int  len = 0;
        bool dirty = false;
        FixedString<PICO_PATH_LEN> path; // 空なら未保存の新規文書
        // 起動時に開くファイル(ファイルビューワーの「編集」から渡される)。
        // 最初のonEnter()で1回だけ読む(Pop()で戻ってきたときに読み直して編集内容を捨てないため)
        FixedString<PICO_PATH_LEN> initial_path;
        bool initial_loaded = false;
        // initial_path はまだ無いファイル。読まずに空の文書として始め、保存はそこへ(メモアプリの「新規」)
        bool initial_is_new = false;
        // 「最近開いたファイル」のダイアログを開いている間だけ確保する一覧
        FixedString<PICO_PATH_LEN>* recent_paths = nullptr;
        int recent_count = 0;
        void freeRecent(){ delete[] this->recent_paths; this->recent_paths = nullptr; this->recent_count = 0; }

        // キーボードへ渡している行(キーボードが開いている間だけ意味を持つ)
        int cur_line = 0;
        // キーボードへsetText()等をしている最中。そこから返ってくる通知は無視する
        bool syncing = false;
        // 最後に合わせた本文欄の高さ(キーボードの出し入れを検出する)
        int last_view_bottom = -1;

        Pending pending = Pending::None;
        int pending_wait_frames = 0;

        EditHistory history;
        // 選択の起点(-1 = 選択していない)。範囲は起点〜カーソル
        int sel_anchor = -1;
        // 選択が検索で見つけた所の目印か。カーソルが動いたら(タップ・←→)外す(範囲選択へ広げない)
        bool sel_from_search = false;
        int sel_search_cursor = -1;

        // 検索
        FixedString<PICO_STR_L> find_text;
        FixedString<PICO_STR_L> replace_text;
        bool search_open = false;
        bool replace_mode = false;

        Button* back_button = nullptr;
        Button* open_button = nullptr;
        Button* save_button = nullptr;
        Button* kb_button = nullptr;
        Button* undo_button = nullptr;
        Button* menu_button = nullptr;
        Label<PICO_STR_L>* status_label = nullptr;
        TextView* view = nullptr;

        // 検索バー
        Label<PICO_STR_S>* find_count_label = nullptr;
        Button* find_prev_button = nullptr;
        Button* find_next_button = nullptr;
        Button* replace_one_button = nullptr;
        Button* replace_all_button = nullptr;
        Button* find_close_button = nullptr;
        int view_top_normal = 0;   // 検索バーが無いときの本文欄の上端
        int view_top_search = 0;   // 検索バーがあるとき
        int last_view_top = -1;

        int lineCount() const;
        int lineOfByte(int byte_offset) const;
        // i行目の[start, end)。endは'\n'の位置(または文書の終わり)
        void lineRange(int i, int& start, int& end) const;
        // [start, end)をsの先頭n バイトで置き換える(記録しない。edit()と元に戻す/やり直しの下請け)
        bool replaceRange(int start, int end, const char* s, int n);
        // [start, end)をsの先頭nバイトで置き換え、元に戻せるよう記録する。容量を超えるならfalse(何もしない)
        bool edit(int start, int end, const char* s, int n, bool chained = false);
        // 置き換えても容量・行数・1行の長さに収まるか。収まらなければ理由
        const char* checkLimits(int start, int end, const char* s, int n) const;
        int findForward(const char* needle, int n, int from) const;
        int findBackward(const char* needle, int n, int before) const;

        // ---- 選択・クリップボード・元に戻す ----
        int cursorPos() const;
        bool selection(int& start, int& end) const;
        void clearSelection();
        void refreshSelection();
        // シーン側で文書を書き換えた/カーソルを動かした後。キーボードへ今の行を渡し直す
        void moveCursorTo(int byte_offset);
        ITextInputWidget* attachedPanel();
        void undo();
        void redo();
        void copySelection(bool cut);
        void paste();
        void deleteSelection();
        void selectAll();
        void toggleSelect();
        void openMenu();

        // ---- 検索/置換 ----
        void openFindDialog(bool replace);
        void openReplaceWithDialog();
        void setSearchOpen(bool open);
        void refreshFindCount();
        bool findNext(bool backward);
        void replaceOne();
        void replaceAll();

        void refreshView();
        void refreshStatus(const char* message = nullptr);
        void setDirty(bool value);
        void updateDirty() { this->setDirty(!this->history.atSaved()); }

        void toggleKeyboard();
        void openKeyboard();
        // 開いているキーボードへline行目を渡し、col(行内のバイト位置)へカーソルを置く
        void attachLine(ITextInputWidget* kb, int line, int col);
        void syncFromKeyboard(ITextInputWidget* kb);
        void layoutView();

        // 変更があれば確かめてから then を呼ぶ。then はダイアログを閉じるまで discard_then に置く
        // (ダイアログのコールバックの箱に入れるとthisとdialogで埋まるため)
        void confirmDiscard(const char* msg, InplaceFunction<void()> then);
        InplaceFunction<void()> discard_then;
        // 最近開いたファイルから選んだ、変更を破棄してから開くファイル
        FixedString<PICO_PATH_LEN> discard_open_path;
        void newDocument();
        bool loadFile(const char* path);
        void openPicker();
        // 最近開いたファイル(RecentFiles)から選んで開く
        void openRecentPicker();
        void saveFile();
        void saveAsPicker();
        bool writeFile(const char* path);

    public:
        // initial_pathを渡すと、そのファイルを開いた状態で始まる(開けなければ空の新規文書)。
        // create_new なら initial_path はこれから作るファイル(空の文書で始め、[保存]でそこへ書く)
        explicit TextEditorScene(const char* initial_path = nullptr, bool create_new = false){
            if(initial_path) this->initial_path.assign(initial_path);
            this->initial_is_new = create_new;
        }
        ~TextEditorScene() override;

        const char* getName() const override { return "TextEditor"; }

        void onEnter() override;
        void onExit() override;
        void onUpdate() override;

        // ---- ITextInputTarget ----
        void onShow(ITextInputWidget* keyboard) override;
        void onTextChanged(ITextInputWidget* keyboard) override;
        void onHide(ITextInputWidget* keyboard) override;
        void onDisplayChanged(ITextInputWidget* keyboard) override;
        bool onBackspaceAtStart(ITextInputWidget* keyboard) override;
        bool onCursorAtEdge(ITextInputWidget* keyboard, int dir) override;
        bool onDeleteAtEnd(ITextInputWidget* keyboard) override;

        // 物理キーボード: ↑↓/PageUp/PageDownで行を移る、Ctrl+Sで保存、Ctrl+Z/Yで元に戻す/やり直し、
        // Ctrl+C/X/V/Aでクリップボード、Ctrl+F/H/Gで検索・置換・次を検索、Shift+矢印で選択。
        // キーボードが閉じていれば打ったときに開く(文字そのものはキー盤が入れる)
        bool onKey(const KeyInputFunctions::Event& ev) override;
        bool getIsSingleLine() override { return false; }
        void setIsSingleLine(bool) override {}
};
