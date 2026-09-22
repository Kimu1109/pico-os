#include "gui/widgets/TriangleShape.hpp"
#include "gui/widgets/ShapeDraw.hpp"
#include "OS_Data.hpp"
#include <algorithm>

void TriangleShape::recomputeBounds() {
    // filled==falseのときは輪郭をDrawThickLine(fillTriangleベース)で描くため、
    // 軸線からthickness/2ぶんはみ出す。filled==trueでも同じ余白を持たせておけば
    // thicknessを切り替えても外接矩形を再計算し忘れる心配がない
    const int16_t margin = (int16_t)std::max(1, this->thickness / 2 + 1);

    const int16_t min_x = std::min({this->x1, this->x2, this->x3});
    const int16_t min_y = std::min({this->y1, this->y2, this->y3});
    const int16_t max_x = std::max({this->x1, this->x2, this->x3});
    const int16_t max_y = std::max({this->y1, this->y2, this->y3});

    this->l_rect.x = min_x - margin;
    this->l_rect.y = min_y - margin;
    this->l_rect.w = (max_x - min_x) + margin * 2;
    this->l_rect.h = (max_y - min_y) + margin * 2;
}

void TriangleShape::render() {
    if (!this->needs_redraw) return;
    if (!this->visible) return;

    if (this->prev_l_rect != this->l_rect)
        markdirty(getScreenPrevRect());

    const Rect g_rect = this->getScreenRect();
    markdirty(g_rect);

    const int screen_origin_x = this->getScreenX() - this->l_rect.x;
    const int screen_origin_y = this->getScreenY() - this->l_rect.y;

    const int sx1 = screen_origin_x + this->x1;
    const int sy1 = screen_origin_y + this->y1;
    const int sx2 = screen_origin_x + this->x2;
    const int sy2 = screen_origin_y + this->y2;
    const int sx3 = screen_origin_x + this->x3;
    const int sy3 = screen_origin_y + this->y3;

    if (this->filled) {
        OSData::frame->fillTriangle(sx1, sy1, sx2, sy2, sx3, sy3, this->color);
    } else {
        ShapeDraw::DrawThickLine(OSData::frame, sx1, sy1, sx2, sy2, this->thickness, this->color);
        ShapeDraw::DrawThickLine(OSData::frame, sx2, sy2, sx3, sy3, this->thickness, this->color);
        ShapeDraw::DrawThickLine(OSData::frame, sx3, sy3, sx1, sy1, this->thickness, this->color);
    }

    this->prev_l_rect.copy(this->l_rect);
    this->needs_redraw = false;
}
