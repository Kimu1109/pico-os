#pragma once

#include "gui/scenes/Scene.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/ScrollList.hpp"
#include "util/FixedString.hpp"

// シンプルなテキストエディタ(行単位)。
//
// オンスクリーンキーボードの入力バッファは192バイト(FixedString<PICO_STR_LL>)なので、
// 文書全体を1つのTextboxで編集するとキーボードを開いた時点で切り詰まってしまう。
// そこで文書を行の一覧(ScrollList)として見せ、**1行ずつ**InputDialogで編集する方式にした。
//   - 行を2回タップ(ScrollListの流儀)= その行を編集
//   - [追加]  = 選んだ行の下へ空行を足してそのまま編集
//   - [削除]  = 選んだ行を消す
//   - [新規] / [開く] / [保存](上書き。名前が無ければ名前を付けて保存)
//
// 文書は固定長の1本のバッファ(kMaxBytes)に'\n'区切りで持ち、行の位置は必要なときに数える
// (行ごとにヒープを確保しない)。上限を超えるファイル(容量・行数・1行の長さ)は
// 黙って切り詰めると保存で内容が消えるので、開かずに理由を出す。
class TextEditorScene : public Scene {
    private:
        static constexpr int kMaxBytes = PICO_STR_4KiB;
        static constexpr int kMaxLines = 100;
        // InputDialogの入力欄(FixedString<PICO_STR_LL>)に収まる最大バイト数
        static constexpr int kMaxLineBytes = PICO_STR_LL - 1;

        static constexpr int MARGIN = 3;

        // ダイアログからダイアログへは1フレーム空ける(MarkdownScene::Pendingと同じ理由)
        enum class Pending { None, OpenPicker };

        char text[kMaxBytes + 1] = {0};
        int  len = 0;
        bool dirty = false;
        FixedString<PICO_PATH_LEN> path; // 空なら未保存の新規文書

        Pending pending = Pending::None;
        int pending_wait_frames = 0;

        Button* back_button = nullptr;
        Button* new_button = nullptr;
        Button* open_button = nullptr;
        Button* save_button = nullptr;
        Button* add_button = nullptr;
        Button* del_button = nullptr;
        Label<PICO_STR_L>* status_label = nullptr;
        ScrollList* list = nullptr;

        int lineCount() const;
        // i行目の[start, end)。endは'\n'の位置(または文書の終わり)
        void lineRange(int i, int& start, int& end) const;
        bool replaceLine(int i, const char* s);
        bool insertLineAfter(int i);
        void deleteLine(int i);

        void refreshList();
        void refreshItem(int i);
        void refreshStatus();
        void setDirty(bool value);

        void editLine(int i);
        void addLine();
        void deleteSelected();

        void confirmDiscard(const char* msg, std::function<void()> then);
        void newDocument();
        bool loadFile(const char* path);
        void openPicker();
        void saveFile();
        void saveAsPicker();
        bool writeFile(const char* path);

    public:
        const char* getName() const override { return "TextEditor"; }

        void onEnter() override;
        void onExit() override;
        void onUpdate() override;
};
