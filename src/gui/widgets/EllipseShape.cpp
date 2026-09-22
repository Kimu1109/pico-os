#include "gui/widgets/EllipseShape.hpp"
#include "OS_Data.hpp"

void EllipseShape::render() {
    if (!this->needs_redraw) return;
    if (!this->visible) return;

    if (this->prev_l_rect != this->l_rect)
        markdirty(getScreenPrevRect());

    const Rect g_rect = this->getScreenRect();
    markdirty(g_rect);

    const int cx = g_rect.x + g_rect.w / 2;
    const int cy = g_rect.y + g_rect.h / 2;
    const int rx = g_rect.w / 2;
    const int ry = g_rect.h / 2;

    if (this->filled) {
        OSData::frame->fillEllipse(cx, cy, rx, ry, this->color);
    } else {
        for (int i = 0; i < this->thickness; i++) {
            const int irx = rx - i;
            const int iry = ry - i;
            if (irx <= 0 || iry <= 0) break;
            OSData::frame->drawEllipse(cx, cy, irx, iry, this->color);
        }
    }

    this->prev_l_rect.copy(this->l_rect);
    this->needs_redraw = false;
}
