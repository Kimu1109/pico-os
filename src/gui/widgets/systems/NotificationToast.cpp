#include "gui/widgets/systems/NotificationToast.hpp"
#include "gui/icons/icon_render.h"
#include "functions/Font_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "OS_Data.hpp"

namespace {
    // 今のクリップと(x,y,w,h)の重なりへ一時的に絞って文字を書く(はみ出した分は切れる)
    void PrintClipped(const char* text, int x, int y, int w, int h){
        int32_t cx, cy, cw, ch;
        OSData::frame->getClipRect(&cx, &cy, &cw, &ch);
        const Rect cur = { (int16_t)cx, (int16_t)cy, (int16_t)cw, (int16_t)ch };
        const Rect r = cur.intersection({ (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h });
        if(r.w <= 0 || r.h <= 0) return;
        OSData::frame->setClipRect(r.x, r.y, r.w, r.h);
        OSData::frame->setCursor(x, y);
        OSData::frame->print(text);
        OSData::frame->setClipRect(cx, cy, cw, ch);
    }
}

void NotificationToast::show(const NotificationFunctions::Entry& e, unsigned long now_ms){
    this->seq = e.seq;
    this->shown_ms = now_ms;
    this->title = e.content.title;
    this->body = e.content.body;
    this->app = e.content.app;
    if(!this->visible) this->setVisible(true);
    else this->needsRender();
}

void NotificationToast::hide(){
    if(!this->visible) return;
    this->visible = false;
    this->seq = 0;
    //下が半透明のダイアログ/キーボードでも、トーストの跡を残さず描き直させる
    PICO_GFX::MarkDirtyBelow(this->getScreenRect());
}

bool NotificationToast::contains(int x, int y) const {
    const Rect r = this->getScreenRect();
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

bool NotificationToast::hitClose(int x, int y) const {
    const Rect r = this->getScreenRect();
    return this->contains(x, y) && x >= r.x + r.w - kCloseW;
}

void NotificationToast::render(){
    //合成(FlushDirty)の中でだけ描く。UpdateAll()からの呼び出しで描いても直後の合成で描き直されるだけ
    if(!PICO_GFX::isDirtyDeactivates) return;
    if(!this->visible) return;

    const Rect g = this->getScreenRect();
    OSData::frame->drawRect(g.x, g.y, g.w, g.h, PICO_BLACK);
    OSData::frame->drawRect(g.x + 1, g.y + 1, g.w - 2, g.h - 2, PICO_BLACK);

    IconRender::DrawIcon(IconID::Bell, IconSize::Px16, g.x + 6, g.y + 5, PICO_BLACK);

    //右端の×
    const int cx = g.x + g.w - kCloseW / 2 - 1;
    const int cy = g.y + g.h / 2;
    OSData::frame->drawLine(cx - 4, cy - 4, cx + 4, cy + 4, PICO_DARKGREY);
    OSData::frame->drawLine(cx - 4, cy + 4, cx + 4, cy - 4, PICO_DARKGREY);
    OSData::frame->drawFastVLine(g.x + g.w - kCloseW, g.y + 6, g.h - 12, PICO_LIGHTGREY);

    const int text_x = g.x + 26;
    const int text_w = g.w - 26 - kCloseW - 2;
    OSData::frame->setFont(FontFn::GetSmall());
    OSData::frame->setTextColor(PICO_BLACK);
    PrintClipped(this->title.c_str(), text_x, g.y + 4, text_w, 18);

    //本文が無ければアプリ名を薄く出す(どこから来たか分かるように)
    OSData::frame->setTextColor(PICO_DARKGREY);
    const char* second = !this->body.empty() ? this->body.c_str() : this->app.c_str();
    PrintClipped(second, text_x, g.y + 22, text_w, 18);
    FontFn::SetNormal();

    this->needs_redraw = false;
}
