#include "gui/widgets/RectShape.hpp"
#include "OS_Data.hpp"

void RectShape::render() {
    if (!this->needs_redraw) return;
    if (!this->visible) return;

    if (this->prev_l_rect != this->l_rect)
        markdirty(getScreenPrevRect());

    const Rect g_rect = this->getScreenRect();
    markdirty(g_rect);

    if (this->filled) {
        OSData::frame->fillRect(g_rect.x, g_rect.y, g_rect.w, g_rect.h, this->color);
    } else {
        // 内側へ重ね描きして太らせる(NumberSlider等と同じくdrawWideLine類は使わない)。
        // w/hを食いつぶしたらそれ以上は描かない(小さい矩形に太い枠を指定した場合の保険)
        for (int i = 0; i < this->thickness; i++) {
            const int w = g_rect.w - i * 2;
            const int h = g_rect.h - i * 2;
            if (w <= 0 || h <= 0) break;
            OSData::frame->drawRect(g_rect.x + i, g_rect.y + i, w, h, this->color);
        }
    }

    this->prev_l_rect.copy(this->l_rect);
    this->needs_redraw = false;
}
