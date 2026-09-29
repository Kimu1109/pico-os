#pragma once

#include "gui/scenes/Scene.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/Image.hpp"
#include "gui/widgets/ScrollContainer.hpp"
#include "gui/widgets/apps/FileExplorer.hpp"
#include "gui/widgets/apps/MarkdownView.hpp"
#include "util/FixedString.hpp"

// ファイルビューワー。
//
// FileExplorer(既存のファイルエクスプローラー部品)でSD上を一覧し、ファイルを
// 2回タップ(ScrollListの「2回タップで開く」流儀。FileExplorer::setOnFileTap()、
// 新設)すると拡張子に応じて中身を表示する:
//   .md / .markdown … MarkdownView(既存のMarkdownブラウザと同じ描画。画像参照等も解決する)
//   .pimg           … Image(このOS唯一の画像形式。onRAM=falseでSDからストリーミング描画。
//                      MarkdownViewの画像ブロックと同じ経路)
//   それ以外        … プレーンテキストとしてそのまま表示
//                      (Label+setDisableAutoTextDecoration(true)。**や~をマークアップとして
//                      解釈させない。DictScene/EventDetailDialogの本文欄と同じ考え方)
//
// 「一覧」と「表示」は同じシーン内の2モードとして持つ(ChatScene::applyMode()/
// ClocksScene::applyVisibility()と同じ「表示に関わる全ウィジェットを起動時に作っておき、
// 出し分けだけ切り替える」流儀)。表示用の3種(MarkdownView/プレーンテキスト用
// ScrollContainer/画像用ScrollContainer)は同時にはどれか1つしか見えないが、
// 切り替えのたびにnew/deleteするより単純で安全なためonEnter()でまとめて作る
// (MarkdownViewだけで約40KB。このOSの中でも重い部類のシーンになるが、
// MarkdownScene/ChatScene/CalendarScene同様「シーン本体は数十バイト」の例外として扱う)。
class FileViewerScene : public Scene {
    private:
        enum class Mode { Browse, View };
        enum class ViewKind { None, Markdown, Text, Image };

        // プレーンテキスト表示の上限。MarkdownViewのkMdMaxSourceBytesと同じ8KiBに
        // 揃えてある(「SD上の文書を丸ごと開く」という用途としては同格のため)。
        static constexpr size_t kMaxTextBytes = PICO_STR_8KiB;

        static constexpr int MARGIN = 3;
        // ScrollContainerの縦スクロールバー幅(ScrollContainer::SCROLL_Lと一致させること。
        // EventDetailDialogのBODY_SCROLLBAR_Wと同じ理由)とテキストの内側余白
        static constexpr int SCROLLBAR_W = 15;
        static constexpr int TEXT_PADDING = 2;

        Mode mode = Mode::Browse;
        ViewKind view_kind = ViewKind::None;

        Button* back_button = nullptr;
        // Viewモードでのみ表示。開いているファイル名、または失敗時の理由を出す
        Label<PICO_STR_L>* status_label = nullptr;

        FileExplorer* explorer = nullptr;

        MarkdownView* md_view = nullptr;

        ScrollContainer* text_scroll = nullptr;
        Label<kMaxTextBytes>* text_label = nullptr; // 所有権はtext_scroll

        ScrollContainer* image_scroll = nullptr;
        Image* image_view = nullptr; // 所有権はimage_scroll

        // SDからの読み込み用の一時領域。MarkdownView::doc_textと同じ理由でメンバに持つ
        // (ファイルサイズぶんの一時バッファをヒープへ一度に要求しないよう、256Bずつ
        // 読み進めてここへ追記してからtext_label->setText()を1回だけ呼ぶ)
        FixedString<kMaxTextBytes> text_buf;

        int top_row_h = 0;

        void applyVisibility();
        void openFile(const char* path);
        void showTextMessage(const char* message);
        bool loadPlainText(const char* path);
        Rect bodyRect() const;

    public:
        const char* getName() const override { return "FileViewer"; }

        void onEnter() override;
        void onExit() override;
};
