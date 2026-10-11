#pragma once

#include "gui/scenes/Scene.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/apps/FileExplorer.hpp"
#include "util/FixedString.hpp"

// 標準アプリのファイルエクスプローラー。
//
// SD上のファイル一覧・フォルダ作成/削除・選択はFileExplorerウィジェットが
// 一通り持っている。このシーンはその上に「戻る」と、コピー/移動/名前の変更/貼り付けの
// ボタンを足す(FileSave/FileSelectDialogが使うFileExplorer本体には足さない)。
// ファイルを2回タップすると、その中身をファイルビューワー(FileViewerScene)で開く。
//
// コピー/移動は「選ぶ→[コピー]か[移動]→行き先のフォルダへ移る→[貼付]」の流れ。
// 覚えているのはパス1つだけ(クリップボード)。コピーは何回でも貼れ、移動は1回で消える。
class FileExplorerScene : public Scene {
    private:
        Button* back_button    = nullptr;
        Button* copy_button    = nullptr;
        Button* move_button    = nullptr;
        Button* rename_button  = nullptr;
        Button* paste_button   = nullptr;
        Label<PICO_STR_LL>* status_label = nullptr;
        FileExplorer* explorer = nullptr;

        // クリップボード。シーンのメンバなので、ファイルビューワーへ移って戻っても残る
        FixedString<PICO_PATH_LEN> clip_path;
        bool clip_is_move = false;

        constexpr static int MARGIN = 3;

        void onCopyOrMove(bool is_move);
        void onPaste();
        void onRename();
        void commitRename(const char* old_path, const char* new_name);

        void setStatus(const char* text, bool is_error = false);
        void showClipStatus();
        void clearClip();

    public:
        const char* getName() const override { return "FileExplorer"; }

        void onEnter() override;
        void onExit() override;
};
