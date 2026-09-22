#include "gui/widgets/LineShape.hpp"
#include "gui/widgets/ShapeDraw.hpp"
#include "OS_Data.hpp"
#include <algorithm>

void LineShape::recomputeBounds() {
    // 太い線はfillTriangleで軸線からthickness/2ぶんはみ出すので、外接矩形にも
    // 同じだけ余白を持たせないとmarkdirty漏れ(消し残り)が起きる
    const int16_t margin = (int16_t)std::max(1, this->thickness / 2 + 1);

    const int16_t min_x = std::min(this->x1, this->x2);
    const int16_t min_y = std::min(this->y1, this->y2);
    const int16_t max_x = std::max(this->x1, this->x2);
    const int16_t max_y = std::max(this->y1, this->y2);

    this->l_rect.x = min_x - margin;
    this->l_rect.y = min_y - margin;
    this->l_rect.w = (max_x - min_x) + margin * 2;
    this->l_rect.h = (max_y - min_y) + margin * 2;
}

void LineShape::render() {
    if (!this->needs_redraw) return;
    if (!this->visible) return;

    if (this->prev_l_rect != this->l_rect)
        markdirty(getScreenPrevRect());

    const Rect g_rect = this->getScreenRect();
    markdirty(g_rect);

    // ローカル座標(x1/y1/x2/y2)をスクリーン座標へ: l_rectの原点(ローカル0,0)が
    // スクリーンのどこに来るかを求め、そこからのオフセットとして点を置く
    const int screen_origin_x = this->getScreenX() - this->l_rect.x;
    const int screen_origin_y = this->getScreenY() - this->l_rect.y;

    const int sx1 = screen_origin_x + this->x1;
    const int sy1 = screen_origin_y + this->y1;
    const int sx2 = screen_origin_x + this->x2;
    const int sy2 = screen_origin_y + this->y2;

    ShapeDraw::DrawThickLine(OSData::frame, sx1, sy1, sx2, sy2, this->thickness, this->color);

    this->prev_l_rect.copy(this->l_rect);
    this->needs_redraw = false;
}
