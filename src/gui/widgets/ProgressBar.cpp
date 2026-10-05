#include "gui/widgets/ProgressBar.hpp"
#include "OS_Data.hpp"

void ProgressBar::setValue(float v) {
    if (v < this->min_value) v = this->min_value;
    if (v > this->max_value) v = this->max_value;
    if (v == this->value) return;
    this->value = v;
    this->needsRender();
}

void ProgressBar::setMinValue(float v) {
    if (v > this->max_value) return;
    this->min_value = v;
    if (this->value < v) this->value = v;
    this->needsRender();
}

void ProgressBar::setMaxValue(float v) {
    if (v < this->min_value) return;
    this->max_value = v;
    if (this->value > v) this->value = v;
    this->needsRender();
}

void ProgressBar::render() {
    if (!this->needs_redraw) return;
    if (!this->visible) return;

    if (this->prev_l_rect != this->l_rect)
        markdirty(getScreenPrevRect());

    const Rect g = this->getScreenRect();
    markdirty(g);

    // 背景(OPAQUEなので自分で塗る)→塗り→枠の順
    OSData::frame->fillRect(g.x, g.y, g.w, g.h, this->background_color);
    const float span = this->max_value - this->min_value;
    const float ratio = span > 0.0f ? (this->value - this->min_value) / span : 0.0f;
    const int inner_w = g.w - 2;
    int fill_w = (int)(ratio * (float)inner_w + 0.5f);
    if (fill_w > inner_w) fill_w = inner_w;
    if (fill_w > 0 && g.h > 2)
        OSData::frame->fillRect(g.x + 1, g.y + 1, fill_w, g.h - 2, this->color);
    OSData::frame->drawRect(g.x, g.y, g.w, g.h, this->border);

    this->prev_l_rect.copy(this->l_rect);
    this->needs_redraw = false;
}
