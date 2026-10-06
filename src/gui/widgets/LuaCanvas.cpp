#include "gui/widgets/LuaCanvas.hpp"
#include "OS_Data.hpp"
#include "functions/GFX_Functions.hpp"

LuaCanvas::LuaCanvas(int16_t x, int16_t y, int16_t w, int16_t h) {
    this->l_rect = {x, y, w, h};
}

void LuaCanvas::render() {
    // 他ウィジェットと同じ「変化が無ければ再描画しない」流儀。FlushDirty()の
    // renderForce()はこの前に needs_redraw=true を立ててから呼ぶので、
    // dirty矩形に重なった場合は必ずここを通る
    if (!needs_redraw) return;

    // UpdateAll()(Widget::update())から来た場合は描かずにdirtyだけ積む。描くのはFlushDirty()の中だけ
    // (GameBoyViewと同じ)。ここでLuaのrenderを呼ぶと、FlushDirty()でもう一度呼ばれて2回描くうえ、
    // 合成の外なのでpico.draw_*がそれぞれ自分の範囲をdirtyに積み、液晶への転送が倍以上になっていた
    // (pico.gameのスクロールで1フレームに画面2枚ぶん送っていた)
    if (!PICO_GFX::isDirtyDeactivates) {
        if (prev_l_rect != l_rect) markdirty(getScreenPrevRect());
        markdirty(getScreenRect());
        prev_l_rect = l_rect;
        needs_redraw = false;
        return;
    }
    needs_redraw = false;

    if (on_render) on_render();

    // pico.set_draw_area()の呼び忘れ対策の安全弁。OSData::frameは全ウィジェット共有の
    // スプライトで、クリップ矩形も1個しか持たないため、Lua側のrenderコールバックが
    // set_draw_area()したままclear_draw_area()を呼ばずに戻ると、以降このCanvas以外の
    // 描画(他ウィジェットのrender()を含む)まで同じ矩形に切り詰められてしまう。
    // ここで無条件にclearしておけば、その事故だけは防げる
    // (LuaEngine.hppのl_set_draw_area/l_clear_draw_areaのコメントも参照)。
    OSData::frame->clearClipRect();
}
