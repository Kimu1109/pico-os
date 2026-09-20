#include "gui/widgets/LuaCanvas.hpp"

LuaCanvas::LuaCanvas(int16_t x, int16_t y, int16_t w, int16_t h) {
    this->l_rect = {x, y, w, h};
}

void LuaCanvas::render() {
    // 他ウィジェットと同じ「変化が無ければ再描画しない」流儀。FlushDirty()の
    // renderForce()はこの前に needs_redraw=true を立ててから呼ぶので、
    // dirty矩形に重なった場合は必ずここを通る
    if (!needs_redraw) return;
    needs_redraw = false;

    if (on_render) on_render();
}
