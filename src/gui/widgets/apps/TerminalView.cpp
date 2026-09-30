#include "gui/widgets/apps/TerminalView.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Font_Functions.hpp"
#include "OS_Data.hpp"

#include <cstring>

namespace {
    // ANSIの16色 → パレット番号(パレットがVGA風の16色なのでそのまま対応が取れる)
    constexpr uint8_t kAnsiToPico[16] = {
        PICO_BLACK, PICO_MAROON, PICO_DARKGREEN, PICO_OLIVE,
        PICO_NAVY, PICO_PURPLE, PICO_DARKCYAN, PICO_LIGHTGREY,
        PICO_DARKGREY, PICO_RED, PICO_GREEN, PICO_YELLOW,
        PICO_BLUE, PICO_MAGENTA, PICO_CYAN, PICO_WHITE,
    };

    // 罫線(U+2500〜257F)の向き。上=1 下=2 左=4 右=8。0なら罫線として描かない
    uint8_t BoxSegments(uint32_t cp){
        enum { U = 1, D = 2, L = 4, R = 8 };
        if(cp < 0x2500 || cp > 0x257F) return 0;
        if(cp <= 0x2501) return L | R;
        if(cp <= 0x2503) return U | D;
        if(cp <= 0x250B) return ((cp - 0x2504) / 2) % 2 == 0 ? (L | R) : (U | D);
        if(cp <= 0x250F) return D | R;
        if(cp <= 0x2513) return D | L;
        if(cp <= 0x2517) return U | R;
        if(cp <= 0x251B) return U | L;
        if(cp <= 0x2523) return U | D | R;
        if(cp <= 0x252B) return U | D | L;
        if(cp <= 0x2533) return L | R | D;
        if(cp <= 0x253B) return L | R | U;
        if(cp <= 0x254B) return U | D | L | R;
        if(cp <= 0x254D) return L | R;
        if(cp <= 0x254F) return U | D;
        if(cp == 0x2550) return L | R;
        if(cp == 0x2551) return U | D;
        if(cp <= 0x2554) return D | R;
        if(cp <= 0x2557) return D | L;
        if(cp <= 0x255A) return U | R;
        if(cp <= 0x255D) return U | L;
        if(cp <= 0x2560) return U | D | R;
        if(cp <= 0x2563) return U | D | L;
        if(cp <= 0x2566) return L | R | D;
        if(cp <= 0x2569) return L | R | U;
        if(cp <= 0x256C) return U | D | L | R;
        if(cp == 0x256D) return D | R;
        if(cp == 0x256E) return D | L;
        if(cp == 0x256F) return U | L;
        if(cp == 0x2570) return U | R;
        switch(cp){
            case 0x2574: case 0x2578: return L;
            case 0x2575: case 0x2579: return U;
            case 0x2576: case 0x257A: return R;
            case 0x2577: case 0x257B: return D;
            case 0x257C: case 0x257E: return L | R;
            case 0x257D: case 0x257F: return U | D;
            default: return 0; // 斜線はフォントに任せる
        }
    }

    // 1文字をUTF-8にする(drawStringへ渡すため)
    void EncodeUtf8(uint32_t cp, char out[5]){
        if(cp < 0x80){ out[0] = (char)cp; out[1] = 0; return; }
        if(cp < 0x800){
            out[0] = (char)(0xC0 | (cp >> 6));
            out[1] = (char)(0x80 | (cp & 0x3F));
            out[2] = 0;
            return;
        }
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        out[3] = 0;
    }
}

TerminalView::TerminalView(int16_t x, int16_t y, int16_t w, int16_t h, VtTerminal* term) : term(term) {
    this->l_rect = { x, y, w, h };
    this->background_color = PICO_BLACK;
}

void TerminalView::setSmallFont(bool value){
    if(value == this->small) return;
    this->small = value;
    this->needsRender();
}

void TerminalView::setH(int h){
    if(h == this->l_rect.h) return;
    this->markdirty(this->getScreenRect()); //縮むときは、はみ出していた部分を下の物に描き直させる
    this->l_rect.h = (int16_t)h;
    this->needsRender();
}

void TerminalView::setPreedit(const char* text){
    if(strcmp(text ? text : "", this->preedit.c_str()) == 0) return;
    //前に出していた位置と、これから出す位置の両方を描き直す
    if(this->preedit_row >= 0){
        for(int r = this->preedit_row; r < this->preedit_row + 3; r++) this->markRow(r);
    }
    this->preedit.assign(text ? text : "");
    this->preedit_row = this->preedit.empty() ? -1 : this->term->cursorY() + this->scroll_back;
    if(this->preedit_row >= 0){
        for(int r = this->preedit_row; r < this->preedit_row + 3; r++) this->markRow(r);
    }
}

void TerminalView::scrollToBottom(){
    if(this->scroll_back == 0) return;
    this->scroll_back = 0;
    this->needsRender();
}

void TerminalView::markRow(int r){
    if(r < 0) return;
    const int ch = this->cellH();
    const Rect g = this->getScreenRect();
    const int y = g.y + r * ch;
    if(y >= g.y + g.h) return;
    int h = ch;
    if(y + h > g.y + g.h) h = g.y + g.h - y;
    this->markdirty({ g.x, (int16_t)y, g.w, (int16_t)h });
}

void TerminalView::onFrame(){
    if(!this->visible) return;

    const uint64_t d = this->term->takeDirty();
    const uint32_t scrolled = this->term->scrolledLines();
    const uint32_t delta = scrolled - this->last_scrolled;
    this->last_scrolled = scrolled;

    if(this->scroll_back > 0){
        //遡って見ている間は、新しい行が来ても見ている場所を保つ
        if(delta > 0){
            this->scroll_back += (int)delta;
            if(this->scroll_back > this->term->scrollbackCount()) this->scroll_back = this->term->scrollbackCount();
        }
        if(d || delta) this->needsRender();
    }else if(d){
        const int rows = this->term->rows();
        for(int r = 0; r < rows && r < 64; r++){
            if(d & (1ull << r)) this->markRow(r);
        }
    }

    const int cx = this->term->cursorX();
    const int cy = this->term->cursorY();
    const bool vis = this->term->cursorVisible();
    if(cx != this->last_cx || cy != this->last_cy || vis != this->last_cursor_visible){
        this->markRow(this->last_cy + this->scroll_back);
        this->markRow(cy + this->scroll_back);
        this->last_cx = cx;
        this->last_cy = cy;
        this->last_cursor_visible = vis;
        if(!this->preedit.empty()) this->setPreedit(this->preedit.c_str());
    }
}

const VtTerminal::Cell* TerminalView::lineAt(int r) const {
    if(r < this->scroll_back) return this->term->scrollbackRow(this->scroll_back - 1 - r);
    return this->term->row(r - this->scroll_back);
}

void TerminalView::drawBox(uint32_t cp, int px, int py, int w, uint8_t color){
    LGFX_Sprite* f = OSData::frame;
    const int h = this->cellH();
    const int mx = px + w / 2 - (w >= 8 ? 1 : 0);
    const int my = py + h / 2 - 1;

    if(cp >= 0x2580 && cp <= 0x259F){
        switch(cp){
            case 0x2580: f->fillRect(px, py, w, h / 2, color); return;
            case 0x2588: f->fillRect(px, py, w, h, color); return;
            case 0x2590: f->fillRect(px + w / 2, py, w - w / 2, h, color); return;
            case 0x2594: f->fillRect(px, py, w, h / 8 > 0 ? h / 8 : 1, color); return;
            case 0x2595: f->fillRect(px + w - 1, py, 1, h, color); return;
            case 0x2591: case 0x2592: case 0x2593: {
                //網かけ(濃さは3段階とも同じ市松で表す)
                for(int y = 0; y < h; y++){
                    for(int x = (y & 1); x < w; x += 2) f->drawPixel(px + x, py + y, color);
                }
                return;
            }
            default: break;
        }
        if(cp >= 0x2581 && cp <= 0x2587){ //下から n/8
            const int n = (int)(cp - 0x2580) * h / 8;
            f->fillRect(px, py + h - n, w, n, color);
            return;
        }
        if(cp >= 0x2589 && cp <= 0x258F){ //左から (8-k)/8
            const int n = (int)(0x2590 - cp) * w / 8;
            f->fillRect(px, py, n > 0 ? n : 1, h, color);
            return;
        }
        //4分割(左上=1 右上=2 左下=4 右下=8)
        static const uint8_t quads[10] = { 4, 8, 1, 1 | 4 | 8, 1 | 8, 1 | 2 | 4, 1 | 2 | 8, 2, 2 | 4, 2 | 4 | 8 };
        const uint8_t q = quads[cp - 0x2596];
        const int hw = w / 2, hh = h / 2;
        if(q & 1) f->fillRect(px, py, hw, hh, color);
        if(q & 2) f->fillRect(px + hw, py, w - hw, hh, color);
        if(q & 4) f->fillRect(px, py + hh, hw, h - hh, color);
        if(q & 8) f->fillRect(px + hw, py + hh, w - hw, h - hh, color);
        return;
    }

    //DECの走査線(⎺⎻⎼⎽)
    if(cp >= 0x23BA && cp <= 0x23BD){
        const int y = py + (int)(cp - 0x23BA) * (h - 1) / 3;
        f->drawFastHLine(px, y, w, color);
        return;
    }

    const uint8_t s = BoxSegments(cp);
    if(s & 4) f->drawFastHLine(px, my, mx - px + 1, color);
    if(s & 8) f->drawFastHLine(mx, my, px + w - mx, color);
    if(s & 1) f->drawFastVLine(mx, py, my - py + 1, color);
    if(s & 2) f->drawFastVLine(mx, my, py + h - my, color);
}

void TerminalView::drawCell(int px, int py, const VtTerminal::Cell& c, bool cursor){
    LGFX_Sprite* f = OSData::frame;
    uint8_t fg = c.fg();
    uint8_t bg = c.bg();
    if(c.attr & VtTerminal::AttrReverse){
        const uint8_t t = fg; fg = bg; bg = t;
    }
    if((c.attr & VtTerminal::AttrBold) && fg < 8) fg += 8;
    if(cursor){
        const uint8_t t = fg; fg = bg; bg = t;
        if(fg == bg){ fg = 0; bg = 15; }
    }
    const uint8_t fgc = kAnsiToPico[fg & 15];
    const uint8_t bgc = kAnsiToPico[bg & 15];

    const int cw = this->cellW();
    const int ch = this->cellH();
    const int w = (c.attr & VtTerminal::AttrWide) ? cw * 2 : cw;
    f->fillRect(px, py, w, ch, bgc);

    const uint32_t cp = c.ch;
    if(cp != 0 && cp != ' '){
        if(cp > 0x20 && cp < 0x7F){
            if(this->small) f->setFont(&fonts::Font0); else f->setFont(&fonts::AsciiFont8x16);
            f->setTextSize(1);
            f->setTextColor(fgc);
            f->drawChar((uint16_t)cp, px, py);
        }else if((cp >= 0x2500 && cp <= 0x259F && (BoxSegments(cp) || cp >= 0x2580)) || (cp >= 0x23BA && cp <= 0x23BD)){
            this->drawBox(cp, px, py, w, fgc);
        }else{
            char s[5];
            EncodeUtf8(cp, s);
            f->setFont(FontFn::GetSmall());
            if(this->small) f->setTextSize(0.5f); else f->setTextSize(1);
            f->setTextColor(fgc);
            f->drawString(s, px, py);
            f->setTextSize(1);
        }
    }
    if(c.attr & VtTerminal::AttrUnderline) f->drawFastHLine(px, py + ch - 1, w, fgc);
}

void TerminalView::drawPreedit(const Rect& g){
    if(this->preedit.empty() || this->preedit_row < 0) return;
    LGFX_Sprite* f = OSData::frame;
    FontFn::SetSmall();
    const int tw = f->textWidth(this->preedit.c_str());
    const int th = f->fontHeight();
    int x = g.x + this->term->cursorX() * this->cellW();
    if(x + tw + 2 > g.x + g.w) x = g.x + g.w - tw - 2;
    if(x < g.x) x = g.x;
    int y = g.y + this->preedit_row * this->cellH();
    if(y + th + 2 > g.y + g.h) y = g.y + g.h - th - 2;
    f->fillRect(x, y, tw + 2, th + 2, PICO_WHITE);
    f->setTextColor(PICO_BLACK);
    f->drawString(this->preedit.c_str(), x + 1, y + 1);
    f->drawFastHLine(x + 1, y + th, tw, PICO_BLUE);
}

void TerminalView::causeOnPressStart(){
    Widget::causeOnPressStart();
    this->ref_touch_y = OSData::touchY;
    this->ref_scroll_back = this->scroll_back;
    this->dragging = false;
}

void TerminalView::causeOnPressMove(){
    Widget::causeOnPressMove();
    const int dy = OSData::touchY - this->ref_touch_y;
    if(!this->dragging && (dy > kDragThreshold || dy < -kDragThreshold)) this->dragging = true;
    if(!this->dragging) return;

    int back = this->ref_scroll_back + dy / this->cellH();
    const int max = this->term->altScreen() ? 0 : this->term->scrollbackCount();
    if(back < 0) back = 0;
    if(back > max) back = max;
    if(back == this->scroll_back) return;
    this->scroll_back = back;
    this->needsRender();
}

void TerminalView::causeOnPressEnd(){
    Widget::causeOnPressEnd();
    if(this->dragging){
        this->dragging = false;
        return;
    }
    if(this->on_tap) this->on_tap();
}

void TerminalView::render(){
    if(!this->visible) return;

    // UpdateAll()から来た場合: 描かずにdirtyだけ積む(描くのはFlushDirty()の中。GameBoyViewと同じ)
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

    const Rect g = this->getScreenRect();
    int32_t cx = 0, cy = 0, cw = 0, chh = 0;
    OSData::frame->getClipRect(&cx, &cy, &cw, &chh);
    const Rect area = g.intersection({ (int16_t)cx, (int16_t)cy, (int16_t)cw, (int16_t)chh });
    if(area.w <= 0 || area.h <= 0) return;

    const int cellw = this->cellW();
    const int cellh = this->cellH();
    const int rows = this->term->rows();
    const int cols = this->term->cols();
    int r0 = (area.y - g.y) / cellh;
    int r1 = (area.y + area.h - 1 - g.y) / cellh;
    if(r1 >= rows) r1 = rows - 1;
    int x0 = (area.x - g.x) / cellw - 1; //全角の右半分から始まる場合に左半分から描く
    int x1 = (area.x + area.w - 1 - g.x) / cellw;
    if(x0 < 0) x0 = 0;
    if(x1 >= cols) x1 = cols - 1;

    const bool show_cursor = this->scroll_back == 0 && this->term->cursorVisible();
    const int ccx = this->term->cursorX();
    const int ccy = this->term->cursorY();

    for(int r = r0; r <= r1; r++){
        const VtTerminal::Cell* line = this->lineAt(r);
        if(!line) continue;
        const int py = g.y + r * cellh;
        for(int x = x0; x <= x1; x++){
            const VtTerminal::Cell& c = line[x];
            if(c.ch == VtTerminal::kWideRight){
                if(x == x0 && x > 0 && (line[x - 1].attr & VtTerminal::AttrWide)){
                    //左半分が範囲の外。左半分から描く
                    this->drawCell(g.x + (x - 1) * cellw, py, line[x - 1],
                                   show_cursor && r - this->scroll_back == ccy && x - 1 == ccx);
                }
                continue;
            }
            this->drawCell(g.x + x * cellw, py, c, show_cursor && r - this->scroll_back == ccy && x == ccx);
        }
    }

    //遡って見ている間は右端に目印
    if(this->scroll_back > 0){
        OSData::frame->fillRect(g.x + g.w - 3, g.y, 3, 12, PICO_YELLOW);
    }

    this->drawPreedit(g);
    FontFn::SetDefault();
}
