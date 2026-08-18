#include "widgets/Widget.hpp"
#include "OS_Data.hpp"
#include "functions/GFX_Functions.hpp"

void Widget::update() {
    if (OSData::isTouchMove && this->is_pressing) {
        this->onPressMove();
    }
    if (OSData::isTouchEnd && this->is_pressing) {
        this->onPressEnd();
        is_pressing = false;
    }

    render();
}

void Widget::onPressStart() {
    if (on_press_start) on_press_start();
}
void Widget::onPressStart(std::function<void()> callback) {
    this->on_press_start = callback;
}
void Widget::onPressEnd(){
    if (on_press_end) on_press_end();
}
void Widget::onPressEnd(std::function<void()> callback) {
    this->on_press_end = callback;
}
void Widget::onPressMove(){
    if (on_press_move) on_press_move();
}
void Widget::onPressMove(std::function<void()> callback) {
    this->on_press_move = callback;
}

void Widget::Visible(bool visible){
    this->visible = visible;
    this->needsRender();
}
bool Widget::Visible(){
    return this->visible;
}

void Widget::needsRender(){
    PICO_GFX::markDirty(this->getRect());
    this->needs_redraw = true;
}

void Widget::draw_dialog_background(){
    constexpr int16_t kSpacing   = 6;  // 斜線の間隔(px)
    constexpr int16_t kThickness = 2;  // 斜線の太さ(px)
    constexpr uint8_t  kColor    = PICO_BLACK;

    for (int16_t y = 0; y < SCREEN_HEIGHT; ++y) {
        // このy行で最初にヒットするxオフセットを計算
        int16_t offset = ((kSpacing - (y % kSpacing)) % kSpacing);
        for (int16_t x = offset; x < SCREEN_WIDTH; x += kSpacing) {
            for (int16_t t = 0; t < kThickness && x + t < SCREEN_WIDTH; ++t) {
                OSData::frame->writePixel(x + t, y, kColor);
                OSData::frame->writePixel(x + t, SCREEN_HEIGHT - y, kColor);
            }
        }
    }
}