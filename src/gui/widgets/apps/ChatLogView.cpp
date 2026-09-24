#include "gui/widgets/apps/ChatLogView.hpp"

#include "functions/Font_Functions.hpp"
#include "util/Utf8Byte.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstring>
#include <ctime>

// 上下左右の余白、発言と発言の間
static constexpr int kPad = 3;
static constexpr int kGap = 5;
// 本文の字下げ(名前の行と区別しやすくする)
static constexpr int kIndent = 6;
// 右端のスクロールの目印
static constexpr int kBarW = 3;
// 1行ぶんを描くときの一時バッファ。幅240pxに入る文字数よりずっと大きい
static constexpr int kLineBuf = 192;

// 名前の色。白地で読める濃い色だけ(CalendarSceneのカレンダーの色と同じ並び)
static const int8_t kNameColors[] = {
    PICO_BLUE, PICO_DARKGREEN, PICO_RED, PICO_PURPLE,
    PICO_DARKCYAN, PICO_MAROON, PICO_NAVY, PICO_OLIVE,
};

int8_t ChatLogView::NameColor(const char* name){
    //FNV-1a。同じ名前なら端末が違っても同じ色になる
    uint32_t h = 2166136261u;
    for(const char* p = name; p && *p; p++){
        h ^= (uint8_t)*p;
        h *= 16777619u;
    }
    return kNameColors[h % (sizeof(kNameColors) / sizeof(kNameColors[0]))];
}

int ChatLogView::WrapLine(const char* text, int len, int start, int max_w, int& next){
    int w = 0;
    int i = start;
    char one[5];
    while(i < len){
        if(text[i] == '\n'){
            next = i + 1;
            return i;
        }
        int n = Utf8CharBytesFromLeadByte((uint8_t)text[i]);
        if(i + n > len) n = len - i;
        //描くときの一時バッファに入りきらないほど長い行も、ここで切る
        if(i - start + n >= kLineBuf) break;
        memcpy(one, text + i, (size_t)n);
        one[n] = '\0';
        const int cw = OSData::frame->textWidth(one);
        if(w + cw > max_w && i > start) break;
        w += cw;
        i += n;
    }
    next = i;
    return i;
}

int ChatLogView::lineH() const {
    return OSData::frame->fontHeight() + 1;
}

int ChatLogView::textWidth() const {
    return this->l_rect.w - kPad * 2 - kIndent - kBarW;
}

int ChatLogView::maxScroll() const {
    const int m = this->total_h + kPad * 2 - this->l_rect.h;
    return (m > 0) ? m : 0;
}

int ChatLogView::measure(const ChatProto::Message& m) const {
    const char* t = m.text.c_str();
    const int len = (int)m.text.length();
    const int max_w = this->textWidth();
    int lines = 0;
    int pos = 0;
    while(pos < len){
        int next = pos;
        WrapLine(t, len, pos, max_w, next);
        lines++;
        pos = next;
    }
    if(lines == 0) lines = 1;
    //名前の行 + 本文 + 間
    return (int16_t)(this->lineH() * (1 + lines) + kGap);
}

void ChatLogView::formatHeader(const ChatProto::Message& m, char* out, size_t cap) const {
    //今日の発言は時刻だけ、それ以外は日付も付ける
    const time_t t = (time_t)m.epoch;
    const time_t now = time(nullptr);
    struct tm lt, ln;
    localtime_r(&t, &lt);
    localtime_r(&now, &ln);
    if(lt.tm_year == ln.tm_year && lt.tm_yday == ln.tm_yday){
        snprintf(out, cap, "%02d:%02d", lt.tm_hour, lt.tm_min);
    }else{
        snprintf(out, cap, "%d/%d %02d:%02d", lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min);
    }
}

void ChatLogView::refresh(){
    const int n = this->source ? this->source->messageCount() : 0;
    const int use = (n < kMaxEntries) ? n : kMaxEntries;
    const int first = n - use;

    FontFn::SetSmall();

    //測ったことのある発言は控えを使い回す(新着1件で全件を折り返し直さない)
    uint32_t new_ids[kMaxEntries];
    int16_t new_heights[kMaxEntries];
    int total = 0;
    for(int k = 0; k < use; k++){
        const ChatProto::Message& m = this->source->messageAt(first + k);
        int16_t h = -1;
        for(int j = 0; j < this->count; j++){
            if(this->ids[j] == m.id){ h = this->heights[j]; break; }
        }
        if(h < 0){
            h = (int16_t)this->measure(m);
            this->measured++;
        }
        new_ids[k] = m.id;
        new_heights[k] = h;
        total += h;
    }
    FontFn::SetDefault();

    memcpy(this->ids, new_ids, sizeof(uint32_t) * (size_t)use);
    memcpy(this->heights, new_heights, sizeof(int16_t) * (size_t)use);
    this->count = use;
    this->total_h = total;

    if(this->stick_bottom) this->scroll_y = this->maxScroll();
    if(this->scroll_y > this->maxScroll()) this->scroll_y = this->maxScroll();
    this->needsRender();
}

void ChatLogView::setPlaceholder(const char* text){
    if(this->placeholder == text) return;
    this->placeholder.assign(text ? text : "");
    if(this->count == 0) this->needsRender();
}

void ChatLogView::scrollToBottom(){
    this->stick_bottom = true;
    this->scroll_y = this->maxScroll();
    this->needsRender();
}

void ChatLogView::causeOnPressStart(){
    Widget::causeOnPressStart();
    this->ref_touch_y = OSData::touchY;
    this->ref_scroll_y = this->scroll_y;
}

void ChatLogView::causeOnPressMove(){
    Widget::causeOnPressMove();
    //中身が指に付いてくる向き(下へなぞると上の発言が見える)
    int y = this->ref_scroll_y - (OSData::touchY - this->ref_touch_y);
    const int max = this->maxScroll();
    if(y < 0) y = 0;
    if(y > max) y = max;
    if(y == this->scroll_y) return;
    this->scroll_y = y;
    //一番下まで戻したら、また新着に付いていく
    this->stick_bottom = (y >= max);
    this->needsRender();
}

void ChatLogView::render(){
    if(!this->needs_redraw) return;
    if(!this->visible) return;

    if(this->prev_l_rect != this->l_rect)
        markdirty(getScreenPrevRect());

    const Rect g = this->getScreenRect();
    markdirty(g);

    OSData::frame->fillRect(g.x, g.y, g.w, g.h, this->background_color);

    //はみ出した行を切り取る(元のクリップ=合成中のdirty矩形と重ねる)
    int32_t ox, oy, ow, oh;
    OSData::frame->getClipRect(&ox, &oy, &ow, &oh);
    const Rect orig = { (int16_t)ox, (int16_t)oy, (int16_t)ow, (int16_t)oh };
    const Rect clip = orig.intersection(g);
    OSData::frame->setClipRect(clip.x, clip.y, clip.w, clip.h);

    FontFn::SetSmall();
    const int lh = this->lineH();

    if(this->count == 0 || !this->source){
        if(!this->placeholder.empty()){
            const int w = OSData::frame->textWidth(this->placeholder.c_str());
            OSData::frame->setTextColor(PICO_DARKGREY);
            OSData::frame->setCursor(g.x + (g.w - w) / 2, g.y + (g.h - lh) / 2);
            OSData::frame->print(this->placeholder.c_str());
        }
    }else{
        const int n = this->source->messageCount();
        const int first = n - this->count;
        const int max_w = this->textWidth();
        int y = g.y + kPad - this->scroll_y;

        char line[kLineBuf];
        for(int k = 0; k < this->count; k++){
            const int h = this->heights[k];
            if(y + h <= g.y){ y += h; continue; }   //上に隠れている
            if(y >= g.y + g.h) break;                 //ここから下は見えない

            const ChatProto::Message& m = this->source->messageAt(first + k);
            //並びが変わった直後(refresh前)に描かれても、別の発言の高さで描かないように
            if(m.id != this->ids[k]) break;

            // ---- 名前と時刻 ----
            OSData::frame->setTextColor(NameColor(m.name.c_str()));
            OSData::frame->setCursor(g.x + kPad, y);
            OSData::frame->print(m.name.c_str());
            char when[24];
            this->formatHeader(m, when, sizeof(when));
            const int name_w = OSData::frame->textWidth(m.name.c_str());
            OSData::frame->setTextColor(PICO_DARKGREY);
            OSData::frame->setCursor(g.x + kPad + name_w + 6, y);
            OSData::frame->print(when);

            // ---- 本文 ----
            OSData::frame->setTextColor(this->border_color);
            const char* t = m.text.c_str();
            const int len = (int)m.text.length();
            int ly = y + lh;
            int pos = 0;
            while(pos < len && ly < g.y + g.h){
                int next = pos;
                const int end = WrapLine(t, len, pos, max_w, next);
                if(ly + lh > g.y){
                    const int bytes = end - pos;
                    memcpy(line, t + pos, (size_t)bytes);
                    line[bytes] = '\0';
                    OSData::frame->setCursor(g.x + kPad + kIndent, ly);
                    OSData::frame->print(line);
                }
                ly += lh;
                pos = next;
            }
            y += h;
        }

        // ---- 右端のスクロールの目印(中身が表示より長いときだけ) ----
        const int content = this->total_h + kPad * 2;
        if(content > g.h){
            int bar_h = g.h * g.h / content;
            if(bar_h < 10) bar_h = 10;
            const int bar_y = g.y + (g.h - bar_h) * this->scroll_y / this->maxScroll();
            OSData::frame->fillRect(g.x + g.w - kBarW, bar_y, kBarW, bar_h, PICO_LIGHTGREY);
        }
    }

    OSData::frame->setClipRect(ox, oy, ow, oh);
    OSData::frame->setTextColor(PICO_BLACK);
    FontFn::SetDefault();

    this->prev_l_rect.copy(this->getLocalRect());
    this->needs_redraw = false;
}
