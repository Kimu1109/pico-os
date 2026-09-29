#pragma once

#include "gui/scenes/Scene.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/TextView.hpp"
#include "gui/widgets/ImageView.hpp"
#include "gui/widgets/apps/MarkdownView.hpp"
#include "util/FixedString.hpp"

// ファイルビューワー。ファイル1つの中身を見るだけの画面。
//
// ファイルを探すのはファイルアプリ(FileExplorerScene)の仕事で、そこでファイルを
// 2回タップするとこのシーンがPush()される(開くファイルのパスをコンストラクタで受け取る)。
// 「戻る」でファイルアプリへ戻る。拡張子で表示を変える:
//   .md / .markdown … MarkdownView(Markdownブラウザと同じ描画)
//   .pimg           … ImageView(開いたときに1回だけ解いて、以降はメモリから描く)
//   それ以外        … TextView(見えている行だけを描くプレーンテキストの表示欄)
// 表示に使う部品は開いたファイルの種類の1つだけを作る(MarkdownViewは約40KBあるため)。
class FileViewerScene : public Scene {
    private:
        // プレーンテキストとして読む上限。超えたぶんは読まず、その旨を上に出す
        static constexpr size_t kMaxTextBytes = PICO_STR_16KiB;

        static constexpr int MARGIN = 3;

        FixedString<PICO_PATH_LEN> path;

        Button* back_button = nullptr;
        Label<PICO_STR_L>* status_label = nullptr;
        MarkdownView* md_view = nullptr;
        TextView* text_view = nullptr;
        ImageView* image_view = nullptr;

        // TextViewが指す本文。シーンのメンバなので、表示中は必ず生きている
        FixedString<kMaxTextBytes> text_buf;

        Rect bodyRect(int top_row_h) const;
        void showText(const Rect& body, const char* message);
        bool loadPlainText(const Rect& body, bool& truncated);

    public:
        explicit FileViewerScene(const char* path) { this->path.assign(path ? path : ""); }

        const char* getName() const override { return "FileViewer"; }

        void onEnter() override;
        void onExit() override;
};
