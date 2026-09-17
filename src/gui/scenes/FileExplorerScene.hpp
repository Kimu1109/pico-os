#pragma once

#include "gui/scenes/Scene.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/apps/FileExplorer.hpp"

// 標準アプリのファイルエクスプローラー。
//
// SD上のファイル一覧・フォルダ作成/削除・選択はFileExplorerウィジェットが
// 一通り持っているので、このシーンは「戻る」ボタンを足して起動時に
// ウィジェットを生成するだけの薄い皮(ClocksScene/CalculatorSceneと同じ形)。
class FileExplorerScene : public Scene {
    private:
        Button* back_button    = nullptr;
        FileExplorer* explorer = nullptr;

        constexpr static int MARGIN = 3;

    public:
        const char* getName() const override { return "FileExplorer"; }

        void onEnter() override;
        void onExit() override;
};
