#pragma once

#include "gui/widgets/Widget.hpp"
#include "ssh/Vt_Terminal.hpp"
#include "util/FixedString.hpp"

#include <functional>

// 端末の画面(SSHアプリ専用)。VtTerminalの文字の格子をそのまま描く。
//
// 「子を持たずrender()で直接描く」型。GameBoyViewと同じく、UpdateAll()から来るrender()では
// dirtyを積むだけで描かず、FlushDirty()の合成の中(今のクリップの内側)でだけ描く。
// onFrame()を毎フレーム呼ぶと、端末で変わった行(とカーソルの行)だけをdirtyにする。
//
// 文字の大きさは2通り(setSmallFont()):
//   大 … 1セル8x16。ASCIIは AsciiFont8x16、全角は日本語の16pxフォント(2セル)。240pxで30桁
//   小 … 1セル6x8。 ASCIIは Font0、全角は16pxフォントを半分に縮めて描く(読みにくいが形は分かる)。40桁
// 罫線(U+2500〜)とブロック(U+2580〜)は線と塗りで描く(フォントの幅に依らず升目に揃える)。
//
// 上下のドラッグでスクロールバック(流れ去った行)を遡る。動かさずに離すと setOnTap() を呼ぶ。
// 変換中の読み(日本語入力)は setPreedit() でカーソルの位置に重ねて出す。
class TerminalView : public Widget {
    public:
        TerminalView(int16_t x, int16_t y, int16_t w, int16_t h, VtTerminal* term);

        void setSmallFont(bool small);
        bool smallFont() const { return this->small; }
        int cellW() const { return this->small ? 6 : 8; }
        int cellH() const { return this->small ? 8 : 16; }
        // 今の大きさに収まる桁数/行数
        int fitCols() const { return this->l_rect.w / this->cellW(); }
        int fitRows() const { return this->l_rect.h / this->cellH(); }

        void setH(int h);
        void setPreedit(const char* text);
        void scrollToBottom();

        // 毎フレーム呼ぶ。変わった行だけをdirtyにする
        void onFrame();

        void setOnTap(std::function<void()> fn){ this->on_tap = fn; }

        void causeOnPressStart() override;
        void causeOnPressMove() override;
        void causeOnPressEnd() override;
        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::TerminalView; }
        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::OPAQUE; }

    private:
        static constexpr int kDragThreshold = 6;

        VtTerminal* term;
        bool small = false;
        int scroll_back = 0;      // 何行ぶん遡っているか(0なら今の画面)
        uint32_t last_scrolled = 0;
        int last_cx = -1, last_cy = -1;
        bool last_cursor_visible = false;
        FixedString<PICO_STR_LL> preedit;
        int preedit_row = -1;

        int ref_touch_y = 0;
        int ref_scroll_back = 0;
        bool dragging = false;
        std::function<void()> on_tap = nullptr;

        void markRow(int r);
        const VtTerminal::Cell* lineAt(int r) const;
        void drawCell(int px, int py, const VtTerminal::Cell& c, bool cursor);
        void drawBox(uint32_t cp, int px, int py, int w, uint8_t color);
        void drawPreedit(const Rect& g);
};
