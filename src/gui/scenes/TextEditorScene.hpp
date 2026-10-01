#pragma once

#include "gui/scenes/Scene.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/TextView.hpp"
#include "gui/widgets/interfaces/ITextInputTarget.hpp"
#include "util/FixedString.hpp"

#include <functional>

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
class TextEditorScene : public Scene, public ITextInputTarget {
    private:
        static constexpr int kMaxBytes = PICO_STR_32KiB;
        static constexpr int kMaxLines = 1000;
        // キーボードの入力バッファ(FixedString<PICO_STR_LL>)に収まる1行の最大バイト数
        static constexpr int kMaxLineBytes = PICO_STR_LL - 1;

        static constexpr int MARGIN = 3;

        // ダイアログからダイアログへは1フレーム空ける(MarkdownScene::Pendingと同じ理由)
        enum class Pending { None, OpenPicker };

        char text[kMaxBytes + 1] = {0};
        int  len = 0;
        bool dirty = false;
        FixedString<PICO_PATH_LEN> path; // 空なら未保存の新規文書
        // 起動時に開くファイル(ファイルビューワーの「編集」から渡される)。
        // 最初のonEnter()で1回だけ読む(Pop()で戻ってきたときに読み直して編集内容を捨てないため)
        FixedString<PICO_PATH_LEN> initial_path;
        bool initial_loaded = false;

        // キーボードへ渡している行(キーボードが開いている間だけ意味を持つ)
        int cur_line = 0;
        // キーボードへsetText()等をしている最中。そこから返ってくる通知は無視する
        bool syncing = false;
        // 最後に合わせた本文欄の高さ(キーボードの出し入れを検出する)
        int last_view_bottom = -1;

        Pending pending = Pending::None;
        int pending_wait_frames = 0;

        Button* back_button = nullptr;
        Button* new_button = nullptr;
        Button* open_button = nullptr;
        Button* save_button = nullptr;
        Button* kb_button = nullptr;
        Label<PICO_STR_L>* status_label = nullptr;
        TextView* view = nullptr;

        int lineCount() const;
        int lineOfByte(int byte_offset) const;
        // i行目の[start, end)。endは'\n'の位置(または文書の終わり)
        void lineRange(int i, int& start, int& end) const;
        // [start, end)をsの先頭n バイトで置き換える
        bool replaceRange(int start, int end, const char* s, int n);

        void refreshView();
        void refreshStatus(const char* message = nullptr);
        void setDirty(bool value);

        void toggleKeyboard();
        void openKeyboard();
        // 開いているキーボードへline行目を渡し、col(行内のバイト位置)へカーソルを置く
        void attachLine(ITextInputWidget* kb, int line, int col);
        void syncFromKeyboard(ITextInputWidget* kb);
        void layoutView();

        void confirmDiscard(const char* msg, std::function<void()> then);
        void newDocument();
        bool loadFile(const char* path);
        void openPicker();
        void saveFile();
        void saveAsPicker();
        bool writeFile(const char* path);

    public:
        // initial_pathを渡すと、そのファイルを開いた状態で始まる(開けなければ空の新規文書)
        explicit TextEditorScene(const char* initial_path = nullptr){
            if(initial_path) this->initial_path.assign(initial_path);
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

        // 物理キーボード: ↑↓/PageUp/PageDownで行を移る、Ctrl+Sで保存、
        // キーボードが閉じていれば打ったときに開く(文字そのものはキー盤が入れる)
        bool onKey(const KeyInputFunctions::Event& ev) override;
        bool getIsSingleLine() override { return false; }
        void setIsSingleLine(bool) override {}
};
