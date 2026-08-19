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