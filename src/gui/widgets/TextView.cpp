#include "gui/widgets/TextView.hpp"
#include "functions/Font_Functions.hpp"
#include "util/Utf8Byte.hpp"
#include "OS_Data.hpp"

#include <cstring>

namespace {
    // 文字の幅の控え(Smallフォント専用)。ASCIIは表で、それ以外はUTF-8のバイト列を
    // 32bitへ詰めた値をキーに直接写像で覚える(衝突したら上書きするだけ)
    int8_t ascii_w[128];
    bool ascii_ready = false;

    struct WideEntry { uint32_t key; uint8_t w; };
    constexpr int kWideSlots = 256;
    WideEntry wide_w[kWideSlots];
}

int TextView::CharWidth(const char* s, int n){
    if(n == 1 && (uint8_t)s[0] < 128){
        if(!ascii_ready){
            memset(ascii_w, -1, sizeof(ascii_w));
            ascii_ready = true;
        }
        int8_t& w = ascii_w[(uint8_t)s[0]];
        if(w < 0){
            char one[2] = { s[0], '\0' };
            w = (int8_t)OSData::frame->textWidth(one, FontFn::GetSmall());
        }
        return w;
    }

    uint32_t key = 0;
    for(int i = 0; i < n; i++) key = (key << 8) | (uint8_t)s[i];
    WideEntry& e = wide_w[(key ^ (key >> 11)) % kWideSlots];
    if(e.key != key || e.key == 0){
        char buf[5];
        memcpy(buf, s, n);
        buf[n] = '\0';
        e.key = key;
        e.w = (uint8_t)OSData::frame->textWidth(buf, FontFn::GetSmall());
    }
    return e.w;
}

void TextView::layout(){
    if(!this->layout_dirty) return;
    this->layout_dirty = false;

    FontFn::SetSmall();
    this->line_h = OSData::frame->fontHeight() + 1;
    FontFn::SetDefault();

    const int max_w = this->contentW();
    int count = 0;
    int start = 0;
    int x = 0;
    int i = 0;
    while(i <= this->len && count < kMaxRows){
        if(i == this->len || this->text[i] == '\n'){
            this->rows[count++] = { (uint16_t)start, (uint16_t)i };
            i++;
            start = i;
            x = 0;
            continue;
        }
        int n = Utf8CharBytesFromLeadByte((uint8_t)this->text[i]);
        if(i + n > this->len) n = this->len - i;
        const int cw = CharWidth(this->text + i, n);
        //幅を超える / 描くときの一時バッファを超えるなら折り返す(1文字も入らない行は作らない)
        if(i > start && (x + cw > max_w || i - start + n >= kRowBuf)){
            this->rows[count++] = { (uint16_t)start, (uint16_t)i };
            start = i;
            x = 0;
            continue;
        }
        x += cw;
        i += n;
    }
    this->truncated = (i <= this->len);
    if(count == 0) this->rows[count++] = {0, 0};
    this->row_count = count;
}

int TextView::maxScroll(){
    this->layout();
    const int content = this->row_count * this->line_h + kPad * 2;
    return content > this->l_rect.h ? content - this->l_rect.h : 0;
}

int TextView::rowOf(size_t byte_offset){
    this->layout();
    for(int r = 0; r < this->row_count; r++){
        const Row& row = this->rows[r];
        if(byte_offset < row.start) return r > 0 ? r - 1 : 0;
        if(byte_offset <= row.end){
            //折り返し位置ちょうどは次の行の頭として扱う
            if(byte_offset == row.end && r + 1 < this->row_count && this->rows[r + 1].start == row.end) continue;
            return r;
        }
    }
    return this->row_count - 1;
}

int TextView::widthOf(int from, int to) const {
    int w = 0;
    int i = from;
    while(i < to){
        int n = Utf8CharBytesFromLeadByte((uint8_t)this->text[i]);
        if(i + n > to) n = to - i;
        w += CharWidth(this->text + i, n);
        i += n;
    }
    return w;
}

void TextView::setDocument(const char* text, int len){
    this->text = text ? text : "";
    this->len = len;
    this->layout_dirty = true;
    if(this->cursor > (size_t)len) this->cursor = len;
    const int max = this->maxScroll();
    if(this->scroll_y > max) this->scroll_y = max;
    this->needsRender();
}

void TextView::setCursor(size_t byte_offset){
    if(byte_offset > (size_t)this->len) byte_offset = this->len;
    if(byte_offset == this->cursor) return;
    this->cursor = byte_offset;
    this->needsRender();
}

void TextView::setCursorVisible(bool v){
    if(v == this->show_cursor) return;
    this->show_cursor = v;
    this->needsRender();
}

void TextView::setComposition(size_t start, size_t len){
    if(start == this->comp_start && len == this->comp_len) return;
    this->comp_start = start;
    this->comp_len = len;
    this->needsRender();
}

void TextView::setH(int h){
    if(h == this->l_rect.h) return;
    markdirty(this->getScreenRect()); //縮むときは、はみ出していた部分を下の物に描き直させる
    this->l_rect.h = h;
    const int max = this->maxScroll();
    if(this->scroll_y > max) this->scroll_y = max;
    this->needsRender();
}

void TextView::scrollToTop(){
    if(this->scroll_y == 0) return;
    this->scroll_y = 0;
    this->needsRender();
}

void TextView::ensureCursorVisible(){
    const int r = this->rowOf(this->cursor);
    const int top = r * this->line_h;
    const int bottom = top + this->line_h + kPad * 2;
    int y = this->scroll_y;
    if(top < y) y = top;
    if(bottom > y + this->l_rect.h) y = bottom - this->l_rect.h;
    const int max = this->maxScroll();
    if(y > max) y = max;
    if(y < 0) y = 0;
    if(y == this->scroll_y) return;
    this->scroll_y = y;
    this->needsRender();
}

void TextView::causeOnPressStart(){
    Widget::causeOnPressStart();
    this->ref_touch_y = OSData::touchY;
    this->ref_scroll_y = this->scroll_y;
    this->dragging = false;
}

void TextView::causeOnPressMove(){
    Widget::causeOnPressMove();
    const int dy = OSData::touchY - this->ref_touch_y;
    if(!this->dragging && (dy > kDragThreshold || dy < -kDragThreshold)) this->dragging = true;
    if(!this->dragging) return;

    int y = this->ref_scroll_y - dy;
    const int max = this->maxScroll();
    if(y < 0) y = 0;
    if(y > max) y = max;
    if(y == this->scroll_y) return;
    this->scroll_y = y;
    this->needsRender();
}

void TextView::causeOnPressEnd(){
    Widget::causeOnPressEnd();
    if(this->dragging){
        this->dragging = false;
        return;
    }
    if(!this->on_tap) return;

    this->layout();
    const Rect g = this->getScreenRect();
    int r = (OSData::touchY - g.y - kPad + this->scroll_y) / this->line_h;
    if(r < 0) r = 0;
    if(r >= this->row_count) r = this->row_count - 1;

    const Row& row = this->rows[r];
    const int tx = OSData::touchX - g.x - kPad;
    int x = 0;
    int i = row.start;
    while(i < row.end){
        int n = Utf8CharBytesFromLeadByte((uint8_t)this->text[i]);
        if(i + n > row.end) n = row.end - i;
        const int cw = CharWidth(this->text + i, n);
        if(tx < x + cw / 2) break;
        x += cw;
        i += n;
    }
    this->on_tap((size_t)i);
}

void TextView::render(){
    if(!this->needs_redraw) return;
    if(!this->visible) return;

    this->layout();

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
    OSData::frame->setTextColor(PICO_BLACK);

    const int lh = this->line_h;
    const int first = this->scroll_y / lh;
    const size_t comp_end = this->comp_start + this->comp_len;
    char buf[kRowBuf];

    for(int r = first; r < this->row_count; r++){
        const int y = g.y + kPad + r * lh - this->scroll_y;
        if(y >= g.y + g.h) break;
        const Row& row = this->rows[r];
        const int x0 = g.x + kPad;

        const int bytes = row.end - row.start;
        memcpy(buf, this->text + row.start, bytes);
        buf[bytes] = '\0';
        OSData::frame->setCursor(x0, y);
        OSData::frame->print(buf);

        //変換中の読みに下線
        if(this->comp_len > 0 && this->comp_start < row.end && comp_end > row.start){
            const int a = this->comp_start > row.start ? (int)this->comp_start : row.start;
            const int b = comp_end < row.end ? (int)comp_end : row.end;
            const int ux = x0 + this->widthOf(row.start, a);
            OSData::frame->drawFastHLine(ux, y + lh - 2, this->widthOf(a, b), PICO_BLACK);
        }
    }

    //カーソル(2px幅の縦棒)
    if(this->show_cursor){
        const int r = this->rowOf(this->cursor);
        const Row& row = this->rows[r];
        const int cx = g.x + kPad + this->widthOf(row.start, (int)this->cursor);
        const int cy = g.y + kPad + r * lh - this->scroll_y;
        OSData::frame->fillRect(cx, cy, 2, lh - 1, PICO_BLUE);
    }

    //スクロールの目印(中身が表示より長いときだけ)
    const int content = this->row_count * lh + kPad * 2;
    if(content > g.h){
        int bar_h = g.h * g.h / content;
        if(bar_h < 10) bar_h = 10;
        const int bar_y = g.y + (g.h - bar_h) * this->scroll_y / this->maxScroll();
        OSData::frame->fillRect(g.x + g.w - kBarW, bar_y, kBarW, bar_h, PICO_LIGHTGREY);
    }

    FontFn::SetDefault();
    OSData::frame->setClipRect(orig.x, orig.y, orig.w, orig.h);

    this->needs_redraw = false;
}
