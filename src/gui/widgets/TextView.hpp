#pragma once

#include "gui/widgets/Widget.hpp"

#include <functional>

// プレーンテキストの表示欄(汎用)。テキストエディタの本文欄とファイルビューワーが使う。
//
// 「子を持たずrender()で直接描き、タップ位置から逆算する」型(ChatLogView/AppGrid等と同じ)。
// Label+ScrollContainerと違い、**見えている行だけを描く**(長い文書でもスクロールの重さが変わらない)。
// 文書は呼び出し側のバッファをそのまま指し、コピーを持たない(寿命は呼び出し側が保証する)。
//   - 行は枠の幅で折り返し、上下のドラッグでスクロールする
//   - マークアップ(**や~)は解釈しない。書いてあるとおりに出す
//   - 任意でカーソル(縦棒、setCursorVisible())と、変換中の読みの下線(setComposition())を描く
//   - 動かさずに指を離すと、そこに一番近い文字の境目のバイト位置を setOnTap() で知らせる
//
// 折り返しの計算は文書が変わったときだけ全体をやり直す(setDocument())。1文字ごとの
// textWidth()は重いので、文字の幅は小さな表に覚えておく(フォントはSmall固定)。
// 行の表は固定長(kMaxRows、1行4バイト)で、溢れた分は表示しない(isTruncated())。
// 位置は16bitで持つので、文書は64KiB未満であること。
class TextView : public Widget {
    public:
        static constexpr int kMaxRows = 1024;

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
        bool show_cursor = false;
        bool truncated = false;
        size_t comp_start = 0;
        size_t comp_len = 0;

        int scroll_y = 0;
        int line_h = 17;

        int ref_touch_y = 0;
        int ref_scroll_y = 0;
        bool dragging = false;

        std::function<void(size_t byte_offset)> on_tap = nullptr;

        // setOwnedText()が確保した文書(Luaなど、呼び出し側が文書を持ち続けられない場合用)。
        // 確保するのは文書が変わるときだけ。textがこれを指している間だけ有効
        char* owned = nullptr;
        size_t owned_cap = 0;

        int contentW() const { return this->l_rect.w - kPad * 2 - kBarW; }
        int maxScroll();
        void layout();
        int rowOf(size_t byte_offset);
        int widthOf(int from, int to) const;

        static int CharWidth(const char* s, int n);

    public:
        TextView(int16_t x, int16_t y, int16_t w, int16_t h) {
            this->l_rect = {x, y, w, h};
            this->rows[0] = {0, 0};
        }

        ~TextView() override;
        TextView(const TextView&) = delete;
        TextView& operator=(const TextView&) = delete;

        // 文書を差し替える(折り返しをやり直す)。textは呼び出し側が持ち続けること
        void setDocument(const char* text, int len);
        // 文書をこのウィジェットの中へコピーして持つ(Luaから使う用)。
        // 位置が16bitなので kMaxOwnedBytes を超える分は切り捨てる(切ったらfalse)。確保できなければfalse
        static constexpr size_t kMaxOwnedBytes = 16 * 1024;
        bool setOwnedText(const char* text, size_t len);
        const char* getText() const { return this->text; }
        int getTextLength() const { return this->len; }
        int getScrollY() const { return this->scroll_y; }
        void setScrollY(int y);
        int getRowCount() { this->layout(); return this->row_count; }
        void setW(int w);
        // getText()上のバイト位置
        void setCursor(size_t byte_offset);
        size_t getCursor() const { return this->cursor; }
        // 行の表が足りず、文書の終わりまで表示できていない
        bool isTruncated() { this->layout(); return this->truncated; }
        // 一番上へスクロールする
        void scrollToTop();
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

        WidgetType getWidgetType() const override { return WidgetType::TextView; }
        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::OPAQUE; }
};
