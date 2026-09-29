#pragma once

#include "gui/widgets/Widget.hpp"

#include <functional>

// テキストエディタ(TextEditorScene)の本文欄。
//
// 「子を持たずrender()で直接描き、タップ位置から逆算する」型(ChatLogView/AppGrid等と同じ)。
// 文書は呼び出し側(シーン)のバッファをそのまま指し、コピーを持たない。
//   - 行は枠の幅で折り返し、上下のドラッグでスクロールする
//   - カーソル(縦棒)と、変換中の読み(下線)を描く
//   - 動かさずに指を離すと、そこに一番近い文字の境目のバイト位置を setOnTap() で知らせる
//
// 折り返しの計算は文書が変わったときだけ全体をやり直す(setDocument())。1文字ごとの
// textWidth()は重いので、文字の幅は小さな表に覚えておく(フォントはSmall固定)。
class TextEditView : public Widget {
    public:
        static constexpr int kMaxRows = 512;

    private:
        struct Row { uint16_t start; uint16_t end; }; // endは行の終わり('\n'の位置 or 折り返し位置)

        static constexpr int kPad = 3;
        static constexpr int kBarW = 3;
        static constexpr int kDragThreshold = 6;
        static constexpr int kRowBuf = 256;

        const char* text = "";
        int len = 0;

        Row rows[kMaxRows];
        int row_count = 1;
        bool layout_dirty = true;

        size_t cursor = 0;
        bool show_cursor = true;
        size_t comp_start = 0;
        size_t comp_len = 0;

        int scroll_y = 0;
        int line_h = 17;

        int ref_touch_y = 0;
        int ref_scroll_y = 0;
        bool dragging = false;

        std::function<void(size_t byte_offset)> on_tap = nullptr;

        int contentW() const { return this->l_rect.w - kPad * 2 - kBarW; }
        int maxScroll();
        void layout();
        int rowOf(size_t byte_offset);
        int widthOf(int from, int to) const;

        static int CharWidth(const char* s, int n);

    public:
        TextEditView(int16_t x, int16_t y, int16_t w, int16_t h) {
            this->l_rect = {x, y, w, h};
            this->rows[0] = {0, 0};
        }

        // 文書を差し替える(折り返しをやり直す)。textは呼び出し側が持ち続けること
        void setDocument(const char* text, int len);
        // getText()上のバイト位置
        void setCursor(size_t byte_offset);
        size_t getCursor() const { return this->cursor; }
        void setCursorVisible(bool v);
        void setComposition(size_t start, size_t len);

        void setH(int h);

        // カーソルが見える位置までスクロールする
        void ensureCursorVisible();

        void setOnTap(std::function<void(size_t byte_offset)> callback) { this->on_tap = callback; }

        void causeOnPressStart() override;
        void causeOnPressMove() override;
        void causeOnPressEnd() override;
        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::TextEditView; }
        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::OPAQUE; }
};
