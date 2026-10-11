#include "gui/widgets/systems/LockScreen.hpp"
#include "gui/widgets/Label.hpp"
#include "functions/KeyInput_Functions.hpp"
#include "functions/Time_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "OS_Data.hpp"

#include "Arduino.h"
#include <cstdio>
#include <cstring>

namespace {
    const char* const kWday[7] = { "日", "月", "火", "水", "木", "金", "土" };
    constexpr int kSideMargin = 6;
    constexpr int kCols = 3;
}

void LockScreen::reset(){
    memset(this->pin, 0, sizeof(this->pin));
    this->len = 0;
    this->pressed_key = -1;
    this->needsRender();
}

void LockScreen::keyRect(int key, int& x, int& y, int& w, int& h) const {
    if(key == kKeyRing){
        x = kSideMargin; y = kRingY; w = SCREEN_WIDTH - kSideMargin * 2; h = kRingH;
        return;
    }
    const int cw = (SCREEN_WIDTH - kSideMargin * 2 - kKeyGap * (kCols - 1)) / kCols;
    const int col = key % kCols, row = key / kCols;
    x = kSideMargin + col * (cw + kKeyGap);
    y = kKeyTop + row * (kKeyH + kKeyGap);
    w = cw;
    h = kKeyH;
}

int LockScreen::keyAt(int px, int py) const {
    if(this->shown_ringing){
        int x, y, w, h;
        this->keyRect(kKeyRing, x, y, w, h);
        if(px >= x && px < x + w && py >= y && py < y + h) return kKeyRing;
    }
    for(int k = 0; k < 12; k++){
        int x, y, w, h;
        this->keyRect(k, x, y, w, h);
        if(px >= x && px < x + w && py >= y && py < y + h) return k;
    }
    return -1;
}

void LockScreen::submit(){
    if(this->len == 0) return;
    LockFunctions::Submit(this->pin, millis());
    //通っても違っても打ったものは消す(覚えておかない)
    memset(this->pin, 0, sizeof(this->pin));
    this->len = 0;
    this->needsRender();
}

void LockScreen::pressKey(int key){
    if(key < 0) return;
    if(key == kKeyRing){
        if(LockFunctions::ringing_stop) LockFunctions::ringing_stop();
        this->needsRender();
        return;
    }
    if(key == kKeyOk){ this->submit(); return; }
    if(key == kKeyBack){
        if(this->len > 0){
            this->pin[--this->len] = '\0';
        }else if(!LockFunctions::IsLocked()){
            LockFunctions::Cancel();   //変更/解除の途中なら「取消」
            return;
        }
        this->needsRender();
        return;
    }
    if(this->len >= LockFunctions::kMaxPin) return;
    const char d = (key == kKeyZero) ? '0' : (char)('1' + key);
    this->pin[this->len++] = d;
    this->pin[this->len] = '\0';
    this->needsRender();
}

void LockScreen::onKey(const KeyInputFunctions::Event& ev){
    using KeyInputFunctions::Key;
    switch(ev.key){
        case Key::Char:
            if(ev.cp >= '0' && ev.cp <= '9') this->pressKey(ev.cp == '0' ? kKeyZero : (int)(ev.cp - '1'));
            break;
        case Key::Backspace: if(this->len > 0) this->pressKey(kKeyBack); break;
        case Key::Enter: this->pressKey(kKeyOk); break;
        case Key::Escape:
            if(!LockFunctions::IsLocked()) LockFunctions::Cancel();
            else this->reset();
            break;
        default: break;
    }
}

void LockScreen::tick(){
    const int minute = TimeFunctions::timeinfo.tm_hour * 60 + TimeFunctions::timeinfo.tm_min;
    if(minute != this->shown_minute){ this->shown_minute = minute; this->needsRender(); }
    const unsigned long wait = (LockFunctions::WaitRemainingMs(millis()) + 999) / 1000;
    if(wait != this->shown_wait_sec){ this->shown_wait_sec = wait; this->needsRender(); }
    const bool ringing = LockFunctions::ringing_query && LockFunctions::ringing_query();
    if(ringing != this->shown_ringing){ this->shown_ringing = ringing; this->needsRender(); }
}

void LockScreen::causeOnPressStart(){
    Widget::causeOnPressStart();
    this->pressed_key = this->keyAt(OSData::touchX, OSData::touchY);
    this->needsRender();
}

void LockScreen::causeOnPressMove(){
    Widget::causeOnPressMove();
    const int k = this->keyAt(OSData::touchX, OSData::touchY);
    if(k != this->pressed_key){ this->pressed_key = k; this->needsRender(); }
}

void LockScreen::causeOnPressEnd(){
    Widget::causeOnPressEnd();
    const int k = this->pressed_key;
    this->pressed_key = -1;
    this->needsRender();
    if(k >= 0 && k == this->keyAt(OSData::touchX, OSData::touchY)) this->pressKey(k);
}

void LockScreen::render(){
    if(!this->needs_redraw) return;
    if(!this->visible) return;
    const Rect g = this->getScreenRect();
    markdirty(g);
    if(PICO_GFX::render_suppressed){ this->needs_redraw = false; return; }

    LGFX_Sprite* f = OSData::frame;
    f->fillRect(g.x, g.y, g.w, g.h, PICO_BACKGROUND);

    auto center = [&](FontFn::FontSize size, int8_t color, int y, const char* text){
        const int w = Label<PICO_STR_S>::GetTextWidth(size, text);
        Label<PICO_STR_S>::DrawPlain(size, color, (SCREEN_WIDTH - w) / 2, y, SCREEN_WIDTH, text);
    };

    // 時刻(時計が合っていなければ出さない)
    const struct tm& t = TimeFunctions::timeinfo;
    if(t.tm_year + 1900 >= 2020){
        char buf[32];
        snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
        center(FontFn::Big, PICO_FORECOLOR, 12, buf);
        snprintf(buf, sizeof(buf), "%d月%d日(%s)", t.tm_mon + 1, t.tm_mday, kWday[t.tm_wday % 7]);
        center(FontFn::Small, PICO_DARKGREY, 46, buf);
    }else{
        center(FontFn::Normal, PICO_FORECOLOR, 22, "画面ロック");
    }

    // 文言
    char msg[80];
    const unsigned long wait = (LockFunctions::WaitRemainingMs(millis()) + 999) / 1000;
    if(wait > 0) snprintf(msg, sizeof(msg), "%lu秒待ってください", wait);
    else snprintf(msg, sizeof(msg), "%s", LockFunctions::Message());
    center(FontFn::Small, (wait > 0 || LockFunctions::MessageIsError()) ? PICO_RED : PICO_FORECOLOR, 66, msg);

    // 打った桁数
    const int slots = this->len > LockFunctions::kMinPin ? this->len : LockFunctions::kMinPin;
    constexpr int kDot = 14;
    const int dots_w = slots * kDot + (slots - 1) * 8;
    int dx = (SCREEN_WIDTH - dots_w) / 2 + kDot / 2;
    for(int i = 0; i < slots; i++, dx += kDot + 8){
        if(i < this->len) f->fillCircle(dx, 94, kDot / 2 - 1, PICO_FORECOLOR);
        else f->drawCircle(dx, 94, kDot / 2 - 1, PICO_DARKGREY);
    }

    auto key = [&](int k, const char* label, int8_t fg){
        int x, y, w, h;
        this->keyRect(k, x, y, w, h);
        const bool down = (k == this->pressed_key);
        if(down) f->fillRect(x, y, w, h, PICO_FORECOLOR);
        f->drawRect(x, y, w, h, PICO_FORECOLOR);
        const FontFn::FontSize size = (strlen(label) <= 1) ? FontFn::Normal : FontFn::Small;
        const int tw = Label<PICO_STR_S>::GetTextWidth(size, label);
        const int th = Label<PICO_STR_S>::GetLineHeight(size);
        Label<PICO_STR_S>::DrawPlain(size, down ? PICO_BACKGROUND : fg, x + (w - tw) / 2, y + (h - th) / 2, w, label);
    };

    if(this->shown_ringing) key(kKeyRing, "アラーム/タイマーを止める", PICO_RED);

    static const char* const digits[9] = { "1", "2", "3", "4", "5", "6", "7", "8", "9" };
    for(int k = 0; k < 9; k++) key(k, digits[k], PICO_FORECOLOR);
    const bool cancel = (this->len == 0 && !LockFunctions::IsLocked());
    key(kKeyBack, cancel ? "取消" : "消す", PICO_FORECOLOR);
    key(kKeyZero, "0", PICO_FORECOLOR);
    key(kKeyOk, "OK", PICO_FORECOLOR);

    this->needs_redraw = false;
}
