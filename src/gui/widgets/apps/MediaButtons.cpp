#include "gui/widgets/apps/MediaButtons.hpp"

#include "functions/GFX_Functions.hpp"
#include "functions/Font_Functions.hpp"
#include "functions/Focus_Functions.hpp"
#include "OS_Data.hpp"

void MediaButtons::setShowPause(bool on){
    if(on == this->show_pause) return;
    this->show_pause = on;
    this->needsRender();
}

void MediaButtons::setSkipEnabled(bool on){
    if(on == this->skip_enabled) return;
    this->skip_enabled = on;
    this->needsRender();
}

void MediaButtons::setPlayEnabled(bool on){
    if(on == this->play_enabled) return;
    this->play_enabled = on;
    this->needsRender();
}

bool MediaButtons::enabled(int index) const {
    return index == kPlay ? this->play_enabled : this->skip_enabled;
}

Rect MediaButtons::buttonRect(int index) const {
    const int w = this->l_rect.w / kButtons;
    //幅の余りは真ん中(再生)のボタンへ足す
    const int extra = this->l_rect.w - w * kButtons;
    const int x = index * w + (index > kPlay ? extra : 0);
    const int bw = w + (index == kPlay ? extra : 0);
    return {(int16_t)x, 0, (int16_t)bw, (int16_t)this->l_rect.h};
}

int MediaButtons::buttonAt(int lx, int ly) const {
    for(int i = 0; i < kButtons; i++){
        const Rect r = this->buttonRect(i);
        if(lx >= r.x && lx < r.x + r.w && ly >= r.y && ly < r.y + r.h) return i;
    }
    return -1;
}

Rect MediaButtons::focusRect() const {
    const Rect g = this->getScreenRect();
    const Rect r = this->buttonRect(this->focus_slot);
    return {(int16_t)(g.x + r.x), (int16_t)(g.y + r.y), r.w, r.h};
}

// ---------------------------------------------------------------- タッチ

void MediaButtons::causeOnPressStart(){
    Widget::causeOnPressStart();
    const Rect g = this->getScreenRect();
    const int b = this->buttonAt(OSData::touchX - g.x, OSData::touchY - g.y);
    if(b >= 0 && this->enabled(b)){
        this->pressed = (int8_t)b;
        this->needsRender();
    }
}

void MediaButtons::causeOnPressMove(){
    Widget::causeOnPressMove();
    if(this->pressed < 0) return;
    const Rect g = this->getScreenRect();
    if(this->buttonAt(OSData::touchX - g.x, OSData::touchY - g.y) != this->pressed){
        this->pressed = -1;
        this->needsRender();
    }
}

void MediaButtons::causeOnPressEnd(){
    Widget::causeOnPressEnd();
    if(this->pressed < 0) return;
    const int b = this->pressed;
    this->pressed = -1;
    this->needsRender();
    if(this->on_button) this->on_button(b);
}

void MediaButtons::causeOnPressOut(){
    Widget::causeOnPressOut();
    if(this->pressed < 0) return;
    this->pressed = -1;
    this->needsRender();
}

bool MediaButtons::onFocusKey(FocusKey key){
    int slot = this->focus_slot;
    switch(key){
        case FocusKey::Activate:
            if(this->enabled(slot) && this->on_button) this->on_button(slot);
            return true;
        case FocusKey::Left:
        case FocusKey::Right: {
            const int next = slot + (key == FocusKey::Left ? -1 : 1);
            if(next < 0 || next >= kButtons) return false;
            slot = next;
            break;
        }
        default:
            return false;
    }
    this->focus_slot = (int8_t)slot;
    this->needsRender();
    return true;
}

// ---------------------------------------------------------------- 描画

void MediaButtons::drawGlyph(int index, int cx, int cy, int8_t c){
    LGFX_Sprite* f = OSData::frame;
    switch(index){
        case kPlay:
            if(this->show_pause){
                f->fillRect(cx - 6, cy - 7, 4, 15, c);
                f->fillRect(cx + 2, cy - 7, 4, 15, c);
            }else{
                f->fillTriangle(cx - 4, cy - 8, cx - 4, cy + 8, cx + 8, cy, c);
            }
            break;
        case kRew:
        case kFwd: {
            //二重の三角と「10」(秒)。Font0(6x8)の2文字で12px
            const int d = index == kFwd ? 1 : -1;
            const int ty = cy - 6;
            f->fillTriangle(cx - d * 8, ty - 5, cx - d * 8, ty + 5, cx, ty, c);
            f->fillTriangle(cx, ty - 5, cx, ty + 5, cx + d * 8, ty, c);
            f->setFont(&fonts::Font0);
            f->setTextColor(c);
            f->setCursor(cx - 5, cy + 5);
            f->print("10");
            FontFn::SetDefault();
            break;
        }
        default: break;
    }
}

void MediaButtons::render(){
    if(!this->visible) return;

    // UpdateAll()から来た場合: 描かずにdirtyだけ積む(描くのはFlushDirty()の中)
    if(!PICO_GFX::isDirtyDeactivates){
        if(this->needs_redraw){
            if(this->prev_l_rect != this->l_rect) this->markdirty(this->getScreenPrevRect());
            this->markdirty(this->getScreenRect());
            this->prev_l_rect = this->l_rect;
            this->needs_redraw = false;
        }
        return;
    }
    this->needs_redraw = false;

    LGFX_Sprite* f = OSData::frame;
    const Rect g = this->getScreenRect();
    f->fillRect(g.x, g.y, g.w, g.h, this->background_color);

    for(int i = 0; i < kButtons; i++){
        const Rect r = this->buttonRect(i);
        const int cx = g.x + r.x + r.w / 2, cy = g.y + r.y + r.h / 2;
        if(i == kPlay){
            //真ん中は黒い丸に白抜き(押している間は濃い灰、押せないときは薄い灰)
            const int8_t circle = !this->play_enabled ? PICO_LIGHTGREY : (this->pressed == i ? PICO_DARKGREY : PICO_BLACK);
            f->fillCircle(cx, cy, 16, circle);
            this->drawGlyph(i, cx + (this->show_pause ? 0 : 1), cy, PICO_WHITE);
        }else{
            if(this->pressed == i) f->fillCircle(cx, cy, 15, PICO_LIGHTGREY);
            this->drawGlyph(i, cx, cy, this->skip_enabled ? PICO_BLACK : PICO_LIGHTGREY);
        }
    }

    if(FocusFunctions::ring_visible && FocusFunctions::focused == this){
        const Rect r = this->focusRect();
        for(int k = 0; k < FocusFunctions::kRingWidth; k++){
            f->drawRect(r.x + k, r.y + k, r.w - 2 * k, r.h - 2 * k, FocusFunctions::kRingColor);
        }
    }
    f->setTextColor(PICO_FORECOLOR);
    FontFn::SetDefault();
}
