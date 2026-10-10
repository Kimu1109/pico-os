#include "gui/widgets/apps/MusicPlayerPanel.hpp"

#include <cstring>
#include "functions/GFX_Functions.hpp"
#include "functions/Font_Functions.hpp"
#include "functions/Focus_Functions.hpp"
#include "OS_Data.hpp"

namespace {
    // 時間の表示は m:ss(1時間を超えたら h:mm:ss)
    void FormatTime(FixedString<PICO_STR_S>& out, uint32_t ms){
        const uint32_t sec = ms / 1000;
        out.assign("");
        if(sec >= 3600) out.appendFormat("%u:%02u:%02u", (unsigned)(sec / 3600), (unsigned)(sec / 60 % 60), (unsigned)(sec % 60));
        else            out.appendFormat("%u:%02u", (unsigned)(sec / 60), (unsigned)(sec % 60));
    }

    // 2pxの太さの線(drawWideLine()は4bppのパレットで使えないため、1pxずらして2本引く)
    void Line2(int x0, int y0, int x1, int y1, int8_t c){
        OSData::frame->drawLine(x0, y0, x1, y1, c);
        OSData::frame->drawLine(x0, y0 + 1, x1, y1 + 1, c);
    }

    // text を幅 w に収まる所で切って描く(UTF-8の文字の途中では切らない)。描いたバイト数を返す。
    // フォントは呼び出し側で設定済みであること
    size_t FitBytes(const char* text, int w){
        if(OSData::frame->textWidth(text) <= w) return strlen(text);
        FixedString<PICO_STR_256B> head;
        const int chars = FixedString<PICO_STR_256B>::charCount(text);
        size_t fit = 0;
        for(int i = 1; i <= chars; i++){
            const size_t b = (size_t)FixedString<PICO_STR_256B>::byteOffsetOfChar(text, i);
            head.assign(text, b);
            if(OSData::frame->textWidth(head.c_str()) > w) break;
            fit = b;
        }
        return fit;
    }
}

// ---------------------------------------------------------------- 値の流し込み

void MusicPlayerPanel::setTrack(const char* t, const char* s, int8_t color){
    if(this->title == t && this->sub == s && this->sub_color == color) return;
    this->title.assign(t ? t : "");
    this->sub.assign(s ? s : "");
    this->sub_color = color;
    this->markRow(0, kTimeY - 2);
}

void MusicPlayerPanel::setPlaying(bool a, bool p){
    if(a == this->active && p == this->paused) return;
    const bool active_changed = a != this->active;
    this->active = a;
    this->paused = p;
    this->markRow(kButtonsY, kButtonsH);
    //止まったら時間の行も消す
    if(active_changed){
        this->shown_sec = 0xFFFFFFFF;
        this->markRow(kTimeY - 4, kButtonsY - kTimeY + 4);
    }
}

void MusicPlayerPanel::setModes(bool s, uint8_t r){
    if(s == this->shuffle && r == this->repeat) return;
    this->shuffle = s;
    this->repeat = r;
    this->markRow(kButtonsY, kButtonsH);
}

void MusicPlayerPanel::setHasTracks(bool has){
    if(has == this->has_tracks) return;
    this->has_tracks = has;
    this->markRow(kButtonsY, kButtonsH);
}

void MusicPlayerPanel::setTime(uint32_t pos, uint32_t total, bool lp, bool sk){
    //終わりの無い曲は一周の中の位置を見せる
    if(lp && total > 0) pos %= total;
    const bool shape = total != this->total_ms || lp != this->loops || sk != this->seekable;
    this->pos_ms = pos;
    this->total_ms = total;
    this->loops = lp;
    this->seekable = sk;
    const int px = this->knobPx();
    const uint32_t sec = (this->seeking ? this->seek_ms : pos) / 1000;
    if(!shape && sec == this->shown_sec && px == this->shown_px) return;
    this->shown_sec = sec;
    this->shown_px = px;
    this->markRow(kTimeY - 4, kButtonsY - kTimeY + 4);
}

void MusicPlayerPanel::markRow(int y, int h){
    const Rect g = this->getScreenRect();
    this->markdirty({g.x, (int16_t)(g.y + y), g.w, (int16_t)h});
}

// ---------------------------------------------------------------- 配置

Rect MusicPlayerPanel::buttonRect(int index) const {
    const int w = this->l_rect.w / kButtons;
    //幅の余りは真ん中(再生)のボタンへ足す
    const int extra = this->l_rect.w - w * kButtons;
    int x = index * w + (index > kPlay ? extra : 0);
    const int bw = w + (index == kPlay ? extra : 0);
    return {(int16_t)x, (int16_t)kButtonsY, (int16_t)bw, (int16_t)kButtonsH};
}

Rect MusicPlayerPanel::barRect() const {
    const int x = kPad + kTimeW;
    return {(int16_t)x, (int16_t)(kTimeY + 7), (int16_t)(this->l_rect.w - x - kPad - kTotalW), (int16_t)3};
}

int MusicPlayerPanel::knobPx() const {
    const Rect b = this->barRect();
    const uint32_t pos = this->seeking ? this->seek_ms : this->pos_ms;
    if(this->total_ms == 0) return 0;
    const uint32_t p = pos > this->total_ms ? this->total_ms : pos;
    return (int)((uint64_t)p * (uint32_t)(b.w - 1) / this->total_ms);
}

int MusicPlayerPanel::buttonAt(int lx, int ly) const {
    for(int i = 0; i < kButtons; i++){
        const Rect r = this->buttonRect(i);
        if(lx >= r.x && lx < r.x + r.w && ly >= r.y && ly < r.y + r.h) return i;
    }
    return -1;
}

uint32_t MusicPlayerPanel::msAt(int lx) const {
    const Rect b = this->barRect();
    int px = lx - b.x;
    if(px < 0) px = 0;
    if(px > b.w - 1) px = b.w - 1;
    return b.w > 1 ? (uint32_t)((uint64_t)px * this->total_ms / (uint32_t)(b.w - 1)) : 0;
}

Rect MusicPlayerPanel::focusRect() const {
    const Rect g = this->getScreenRect();
    if(this->focus_slot == 0){
        const Rect b = this->barRect();
        return {(int16_t)(g.x + kPad), (int16_t)(g.y + kTimeY - 4), (int16_t)(g.w - kPad * 2), (int16_t)(b.y - kTimeY + 15)};
    }
    const Rect r = this->buttonRect(this->focus_slot - 1);
    return {(int16_t)(g.x + r.x), (int16_t)(g.y + r.y), r.w, r.h};
}

// ---------------------------------------------------------------- タッチ

void MusicPlayerPanel::causeOnPressStart(){
    Widget::causeOnPressStart();
    const Rect g = this->getScreenRect();
    const int lx = OSData::touchX - g.x, ly = OSData::touchY - g.y;

    //シークバーの行(上下に広めに取る)
    if(this->active && this->seekable && this->total_ms > 0 && ly >= kTimeY - 6 && ly < kButtonsY){
        this->seeking = true;
        this->seek_ms = this->msAt(lx);
        this->shown_px = -1;
        this->markRow(kTimeY - 4, kButtonsY - kTimeY + 4);
        return;
    }
    if(ly < kTimeY - 6){
        this->pressed = kTextArea;
        return;
    }
    const int b = this->buttonAt(lx, ly);
    if(b >= 0){
        this->pressed = (int8_t)b;
        this->markRow(kButtonsY, kButtonsH);
    }
}

void MusicPlayerPanel::causeOnPressMove(){
    Widget::causeOnPressMove();
    const Rect g = this->getScreenRect();
    const int lx = OSData::touchX - g.x, ly = OSData::touchY - g.y;
    if(this->seeking){
        const uint32_t ms = this->msAt(lx);
        if(ms != this->seek_ms){
            this->seek_ms = ms;
            this->markRow(kTimeY - 4, kButtonsY - kTimeY + 4);
        }
        return;
    }
    if(this->pressed == kTextArea){
        if(ly < 0 || ly >= kTimeY - 6 || lx < 0 || lx >= this->l_rect.w) this->pressed = -1;
        return;
    }
    //ボタンの外へ指が出たら押していない見た目へ戻す(離しても押したことにならない)
    if(this->pressed >= 0 && this->buttonAt(lx, ly) != this->pressed){
        this->pressed = -1;
        this->markRow(kButtonsY, kButtonsH);
    }
}

void MusicPlayerPanel::causeOnPressEnd(){
    Widget::causeOnPressEnd();
    if(this->seeking){
        this->seeking = false;
        this->pos_ms = this->seek_ms;
        this->markRow(kTimeY - 4, kButtonsY - kTimeY + 4);
        if(this->on_seek) this->on_seek(this->seek_ms);
        return;
    }
    if(this->pressed == kTextArea){
        this->pressed = -1;
        if(this->on_text_tap) this->on_text_tap();
        return;
    }
    if(this->pressed >= 0){
        const int b = this->pressed;
        this->pressed = -1;
        this->markRow(kButtonsY, kButtonsH);
        if(this->on_button) this->on_button(b);
    }
}

bool MusicPlayerPanel::onFocusKey(FocusKey key){
    const bool bar_ok = this->active && this->seekable && this->total_ms > 0;
    int slot = this->focus_slot;
    switch(key){
        case FocusKey::Activate:
            if(slot > 0 && this->on_button) this->on_button(slot - 1);
            return true;
        case FocusKey::Up:
            if(slot == 0 || !bar_ok) return false;
            slot = 0;
            break;
        case FocusKey::Down:
            if(slot != 0) return false;
            slot = 1 + kPlay;
            break;
        case FocusKey::Left:
        case FocusKey::Right: {
            const int dir = key == FocusKey::Left ? -1 : 1;
            if(slot == 0){
                //10秒戻る/進む
                int64_t ms = (int64_t)this->pos_ms + dir * 10000;
                if(ms < 0) ms = 0;
                if(ms > (int64_t)this->total_ms) ms = this->total_ms;
                if(this->on_seek) this->on_seek((uint32_t)ms);
                return true;
            }
            const int next = slot + dir;
            if(next < 1 || next > kButtons) return false;
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

void MusicPlayerPanel::drawGlyph(int index, int cx, int cy, int8_t c, int8_t bg){
    LGFX_Sprite* f = OSData::frame;
    switch(index){
        case kPlay:
            if(this->active && !this->paused){
                f->fillRect(cx - 6, cy - 7, 4, 15, c);
                f->fillRect(cx + 2, cy - 7, 4, 15, c);
            }else{
                f->fillTriangle(cx - 4, cy - 8, cx - 4, cy + 8, cx + 8, cy, c);
            }
            break;
        case kNext:
            f->fillTriangle(cx - 7, cy - 7, cx - 7, cy + 7, cx + 3, cy, c);
            f->fillRect(cx + 4, cy - 7, 3, 15, c);
            break;
        case kPrev:
            f->fillTriangle(cx + 7, cy - 7, cx + 7, cy + 7, cx - 3, cy, c);
            f->fillRect(cx - 6, cy - 7, 3, 15, c);
            break;
        case kShuffle:
            //左から伸びて交差し、右で矢印になる2本の線
            Line2(cx - 9, cy - 5, cx - 5, cy - 5, c);
            Line2(cx - 5, cy - 5, cx + 3, cy + 4, c);
            Line2(cx - 9, cy + 4, cx - 5, cy + 4, c);
            Line2(cx - 5, cy + 4, cx + 3, cy - 5, c);
            f->fillTriangle(cx + 3, cy - 9, cx + 3, cy - 0, cx + 8, cy - 5, c);
            f->fillTriangle(cx + 3, cy + 0, cx + 3, cy + 9, cx + 8, cy + 4, c);
            break;
        case kRepeat:
            f->drawRoundRect(cx - 9, cy - 6, 19, 13, 4, c);
            f->drawRoundRect(cx - 8, cy - 5, 17, 11, 3, c);
            //上の辺の切れ目と矢印
            f->fillRect(cx - 1, cy - 7, 4, 4, bg);
            f->fillTriangle(cx - 2, cy - 10, cx - 2, cy - 2, cx + 3, cy - 6, c);
            if(this->repeat == 2){
                //1曲リピートは真ん中に「1」
                f->setFont(&fonts::Font0);
                f->setTextColor(c);
                f->setCursor(cx - 2, cy - 3);
                f->print("1");
                FontFn::SetDefault();
            }
            break;
        default: break;
    }
}

void MusicPlayerPanel::render(){
    if(!this->visible) return;

    // UpdateAll()から来た場合: 描かずにdirtyだけ積む(描くのはFlushDirty()の中。setterは行ごとにdirtyを積む)
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
    const int8_t bg = this->background_color;

    //上の区切り線
    f->drawFastHLine(g.x, g.y, g.w, PICO_LIGHTGREY);

    // ---- 曲名と下の行 ----
    FontFn::SetFontSize(FontFn::Small);
    const int text_w = g.w - kPad * 2;
    if(this->title.length() > 0){
        FixedString<PICO_STR_M> t;
        const size_t fit = FitBytes(this->title.c_str(), text_w);
        t.assign(this->title.c_str(), fit);
        f->setTextColor(PICO_BLACK);
        f->setCursor(g.x + kPad, g.y + kTitleY);
        f->print(t.c_str());
    }
    //下の行は2行まで折り返す(読めなかった理由は長い)
    {
        const char* p = this->sub.c_str();
        f->setTextColor(this->sub_color);
        const int lh = f->fontHeight();
        for(int line = 0; line < 2 && *p; line++){
            size_t n = FitBytes(p, text_w);
            //改行があればそこで切る
            const char* nl = strchr(p, '\n');
            if(nl && (size_t)(nl - p) < n) n = (size_t)(nl - p);
            if(n == 0 && !(nl == p)) break;
            FixedString<PICO_STR_256B> part;
            part.assign(p, n);
            f->setCursor(g.x + kPad, g.y + kSubY + line * (lh + 1));
            f->print(part.c_str());
            p += n;
            if(*p == '\n') p++;
        }
    }

    // ---- 時間とシークバー ----
    const Rect b = this->barRect();
    const int bx = g.x + b.x, by = g.y + b.y;
    if(this->active){
        FixedString<PICO_STR_S> t;
        FormatTime(t, this->seeking ? this->seek_ms : this->pos_ms);
        f->setTextColor(PICO_DARKGREY);
        f->setCursor(g.x + kPad, g.y + kTimeY);
        f->print(t.c_str());

        if(this->loops) t.assign("ループ");
        else FormatTime(t, this->total_ms);
        f->setCursor(g.x + g.w - kPad - f->textWidth(t.c_str()), g.y + kTimeY);
        f->print(t.c_str());

        const int px = this->knobPx();
        f->fillRect(bx, by, b.w, b.h, PICO_LIGHTGREY);
        if(px > 0) f->fillRect(bx, by, px, b.h, this->seekable ? PICO_BLUE : PICO_DARKGREY);
        if(this->seekable && this->total_ms > 0){
            f->fillCircle(bx + px, by + 1, this->seeking ? 6 : 5, PICO_BLUE);
        }
    }else{
        f->fillRect(bx, by, b.w, b.h, PICO_LIGHTGREY);
    }

    // ---- ボタン ----
    for(int i = 0; i < kButtons; i++){
        const Rect r = this->buttonRect(i);
        const int cx = g.x + r.x + r.w / 2, cy = g.y + r.y + r.h / 2;
        int8_t c = PICO_BLACK;
        if(i == kShuffle) c = this->shuffle ? PICO_BLUE : PICO_DARKGREY;
        if(i == kRepeat)  c = this->repeat ? PICO_BLUE : PICO_DARKGREY;
        if((i == kPrev || i == kNext) && !this->has_tracks) c = PICO_LIGHTGREY;

        if(i == kPlay){
            //真ん中は黒い丸に白抜き(押している間は濃い灰)
            const int8_t circle = !this->has_tracks ? PICO_LIGHTGREY : (this->pressed == i ? PICO_DARKGREY : PICO_BLACK);
            f->fillCircle(cx, cy, 16, circle);
            this->drawGlyph(i, cx + (this->active && !this->paused ? 0 : 1), cy, PICO_WHITE, circle);
        }else{
            if(this->pressed == i) f->fillCircle(cx, cy, 15, PICO_LIGHTGREY);
            this->drawGlyph(i, cx, cy, c, this->pressed == i ? (int8_t)PICO_LIGHTGREY : bg);
            //入っているモードの印(下の小さな点)
            if((i == kShuffle && this->shuffle) || (i == kRepeat && this->repeat)){
                f->fillCircle(cx, cy + 14, 1, PICO_BLUE);
            }
        }
    }

    //キー/コントローラーで選んでいる所
    if(FocusFunctions::ring_visible && FocusFunctions::focused == this){
        const Rect r = this->focusRect();
        for(int k = 0; k < FocusFunctions::kRingWidth; k++){
            f->drawRect(r.x + k, r.y + k, r.w - 2 * k, r.h - 2 * k, FocusFunctions::kRingColor);
        }
    }

    f->setTextColor(PICO_FORECOLOR);
    FontFn::SetDefault();
}
