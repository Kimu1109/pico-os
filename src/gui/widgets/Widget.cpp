#include "gui/widgets/Widget.hpp"
#include "OS_Data.hpp"
#include "functions/GFX_Functions.hpp"

void Widget::update() {
    if (OSData::isTouchMove && this->is_pressing) {
        this->causeOnPressMove();
    }
    if (OSData::isTouchEnd && this->is_pressing) {
        this->causeOnPressEnd();
        is_pressing = false;
    }
    if (OSData::isTouchStart && !this->is_pressing){
        this->causeOnPressOut();
    }

    render();
}

void Widget::causeOnPressStart() {
    if (on_press_start) on_press_start();
}
void Widget::clearOnPressStart(){
    on_press_start = nullptr;
}
void Widget::setOnPressStart(std::function<void()> callback) {
    this->on_press_start = callback;
}

void Widget::causeOnPressEnd(){
    if (on_press_end) on_press_end();
}
void Widget::clearOnPressEnd(){
    on_press_end = nullptr;
}
void Widget::setOnPressEnd(std::function<void()> callback) {
    this->on_press_end = callback;
}

void Widget::causeOnPressMove(){
    if (on_press_move) on_press_move();
}
void Widget::clearOnPressMove(){
    on_press_move = nullptr;
}
void Widget::setOnPressMove(std::function<void()> callback) {
    this->on_press_move = callback;
}

void Widget::causeOnPressOut(){
    if (on_press_out) on_press_out();
}
void Widget::clearOnPressOut(){
    on_press_out = nullptr;
}
void Widget::setOnPressOut(std::function<void()> callback){
    this->on_press_out = callback;
}

void Widget::setVisible(bool visible){
    this->visitAll([&visible](Widget* w) {
        w->visible = visible;
    });
    this->needsRender();
}
bool Widget::getVisible(){
    return this->visible;
}

void Widget::needsRender(){
    if(disable_markdirty) return;
    PICO_GFX::MarkDirty(this->getScreenRect());
    this->needs_redraw = true;
}

void Widget::markdirty(Rect rect){
    if(disable_interrupts) return;
    PICO_GFX::MarkDirty(rect);
    this->needs_redraw = true;
}