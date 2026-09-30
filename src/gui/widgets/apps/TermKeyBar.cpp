#include "gui/widgets/apps/TermKeyBar.hpp"
#include "functions/Font_Functions.hpp"
#include "OS_Data.hpp"

namespace {
    const char* const kLabels[TermKeyBar::KeyCount] = { "Esc", "Tab", "Ctrl", "", "", "", "", "^C" };
}

int TermKeyBar::keyAt(int local_x) const {
    for(int i = 0; i < KeyCount; i++){
        if(local_x >= this->keyX(i) && local_x < this->keyX(i + 1)) return i;
    }
    return -1;
}

void TermKeyBar::causeOnPressStart(){
    Widget::causeOnPressStart();
    const Rect g = this->getScreenRect();
    this->pressed = this->keyAt(OSData::touchX - g.x);
    this->needsRender();
}

void TermKeyBar::causeOnPressOut(){
    Widget::causeOnPressOut();
    this->pressed = -1;
    this->needsRender();
}

void TermKeyBar::causeOnPressEnd(){
    Widget::causeOnPressEnd();
    const int key = this->pressed;
    this->pressed = -1;
    this->needsRender();
    if(key < 0) return;
    if(key == Ctrl){
        this->setCtrl(!this->ctrl);
    }
    if(this->on_key) this->on_key((Key)key);
}

void TermKeyBar::render(){
    if(!this->needs_redraw) return;
    if(!this->visible) return;

    const Rect g = this->getScreenRect();
    markdirty(g);
    LGFX_Sprite* f = OSData::frame;
    f->fillRect(g.x, g.y, g.w, g.h, PICO_LIGHTGREY);

    FontFn::SetSmall();
    for(int i = 0; i < KeyCount; i++){
        const int x = g.x + this->keyX(i);
        const int w = this->keyX(i + 1) - this->keyX(i);
        const bool lit = (i == this->pressed) || (i == Ctrl && this->ctrl);
        const uint8_t bg = lit ? PICO_BLACK : PICO_WHITE;
        const uint8_t fg = lit ? PICO_WHITE : PICO_BLACK;
        f->fillRect(x + 1, g.y + 1, w - 2, g.h - 2, bg);
        f->drawRect(x + 1, g.y + 1, w - 2, g.h - 2, PICO_DARKGREY);

        const int cx = x + w / 2;
        const int cy = g.y + g.h / 2;
        switch(i){
            case Up:    f->fillTriangle(cx - 5, cy + 3, cx + 5, cy + 3, cx, cy - 4, fg); break;
            case Down:  f->fillTriangle(cx - 5, cy - 3, cx + 5, cy - 3, cx, cy + 4, fg); break;
            case Left:  f->fillTriangle(cx + 3, cy - 5, cx + 3, cy + 5, cx - 4, cy, fg); break;
            case Right: f->fillTriangle(cx - 3, cy - 5, cx - 3, cy + 5, cx + 4, cy, fg); break;
            default: {
                const int tw = f->textWidth(kLabels[i]);
                f->setTextColor(fg);
                f->drawString(kLabels[i], cx - tw / 2, cy - f->fontHeight() / 2);
                break;
            }
        }
    }
    FontFn::SetDefault();
    this->needs_redraw = false;
}
