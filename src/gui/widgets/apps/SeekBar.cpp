#include "gui/widgets/apps/SeekBar.hpp"
#include "OS_Data.hpp"

float SeekBar::ratioAt(int screen_x) const {
    const Rect g = this->getScreenRect();
    const int x0 = g.x + kKnobR;
    const int span = g.w - kKnobR * 2;
    if(span <= 0) return 0.0f;
    float r = (float)(screen_x - x0) / (float)span;
    if(r < 0.0f) r = 0.0f;
    if(r > 1.0f) r = 1.0f;
    return r;
}

int SeekBar::pxOf(float r) const {
    const int span = this->l_rect.w - kKnobR * 2;
    return kKnobR + (int)(r * (float)(span > 0 ? span : 0) + 0.5f);
}

void SeekBar::setRatio(float r){
    if(this->dragging) return;
    if(r < 0.0f) r = 0.0f;
    if(r > 1.0f) r = 1.0f;
    this->ratio = r;
    const int px = this->seekable ? this->pxOf(r) : (int)(r * (float)this->l_rect.w);
    if(px == this->shown_px) return;
    this->needsRender();
}

void SeekBar::setSeekable(bool on){
    if(on == this->seekable) return;
    this->seekable = on;
    this->shown_px = -1;
    this->needsRender();
}

void SeekBar::causeOnPressStart(){
    Widget::causeOnPressStart();
    if(!this->seekable) return;
    this->dragging = true;
    this->drag_ratio = this->ratioAt(OSData::touchX);
    this->needsRender();
}

void SeekBar::causeOnPressMove(){
    Widget::causeOnPressMove();
    if(!this->dragging) return;
    const float r = this->ratioAt(OSData::touchX);
    if(r == this->drag_ratio) return;
    this->drag_ratio = r;
    this->needsRender();
}

void SeekBar::causeOnPressEnd(){
    Widget::causeOnPressEnd();
    if(!this->dragging) return;
    this->dragging = false;
    this->ratio = this->drag_ratio;
    this->shown_px = -1;
    this->needsRender();
    if(this->on_seek) this->on_seek(this->ratio);
}

void SeekBar::causeOnPressOut(){
    Widget::causeOnPressOut();
    // 外へ出たまま離した: 取り消す(位置は流し込みに戻る)
    if(!this->dragging) return;
    this->dragging = false;
    this->shown_px = -1;
    this->needsRender();
}

void SeekBar::render(){
    if(!this->needs_redraw) return;
    if(!this->visible) return;

    if(this->prev_l_rect != this->l_rect) markdirty(getScreenPrevRect());
    const Rect g = this->getScreenRect();
    markdirty(g);

    OSData::frame->fillRect(g.x, g.y, g.w, g.h, this->background_color);
    const int cy = g.y + g.h / 2;
    const float r = this->dragging ? this->drag_ratio : this->ratio;
    if(this->seekable){
        const int x0 = g.x + kKnobR;
        const int x1 = g.x + g.w - kKnobR;
        const int kx = g.x + this->pxOf(r);
        OSData::frame->fillRect(x0, cy - 1, x1 - x0, 3, PICO_LIGHTGREY);
        if(kx > x0) OSData::frame->fillRect(x0, cy - 1, kx - x0, 3, PICO_BLACK);
        OSData::frame->fillCircle(kx, cy, kKnobR, this->dragging ? PICO_BLUE : PICO_BLACK);
        this->shown_px = this->pxOf(r);
    }else{
        // 飛べないもの: 細い進捗の線だけ
        const int fw = (int)(r * (float)(g.w - 2));
        OSData::frame->drawRect(g.x, cy - 3, g.w, 7, PICO_DARKGREY);
        if(fw > 0) OSData::frame->fillRect(g.x + 1, cy - 2, fw, 5, PICO_DARKGREY);
        this->shown_px = (int)(r * (float)this->l_rect.w);
    }

    this->prev_l_rect.copy(this->l_rect);
    this->needs_redraw = false;
}
