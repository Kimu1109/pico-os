#include "apps/reader/ui/ReaderView.hpp"
#include "gui/core/Font_Functions.hpp"
#include "gui/core/GFX_Functions.hpp"
#include "OS_Data.hpp"

#include <cstdlib>
#include <cstring>

ReaderView::ReaderView(int16_t x, int16_t y, int16_t w, int16_t h){
    this->l_rect = {x, y, w, h};
    this->clearWidthCache();
}

void ReaderView::clearWidthCache(){
    memset(this->ascii_w_, -1, sizeof(this->ascii_w_));
    memset(this->wide_key_, 0, sizeof(this->wide_key_));
    this->kanji_w_ = -1;
}

void ReaderView::setLargeFont(bool large){
    if(large == this->large_) return;
    this->large_ = large;
    this->clearWidthCache();
    this->needsRender();
}

int ReaderView::fontPx() const {
    return FontFn::GetFontSize(this->large_ ? FontFn::Normal : FontFn::Small);
}

int ReaderView::linePitch() const {
    // 行間は文字の高さの約1/3
    return this->fontPx() + (this->large_ ? 8 : 6);
}

void ReaderView::setFont() const {
    if(this->large_) FontFn::SetNormal();
    else FontFn::SetSmall();
}

int ReaderView::measure(uint32_t cp){
    const lgfx::v1::U8g2font* font = this->large_ ? FontFn::GetNormal() : FontFn::GetSmall();
    if(cp < 0x80){
        int8_t& w = this->ascii_w_[cp];
        if(w < 0){
            const char one[2] = { (char)cp, '\0' };
            w = (int8_t)OSData::frame->textWidth(one, font);
        }
        return w;
    }
    char buf[5];
    const int n = Aozora::EncodeUtf8(cp, buf);
    buf[n] = '\0';
    // 漢字は全部同じ幅(プロポーショナルなのはかな・英数字・記号)
    if(cp >= 0x4E00 && cp <= 0x9FFF){
        if(this->kanji_w_ < 0) this->kanji_w_ = (int16_t)OSData::frame->textWidth(buf, font);
        return this->kanji_w_;
    }
    const uint32_t slot = (cp ^ (cp >> 7)) % kWideSlots;
    if(this->wide_key_[slot] != cp){
        this->wide_key_[slot] = cp;
        this->wide_w_[slot] = (uint8_t)OSData::frame->textWidth(buf, font);
    }
    return this->wide_w_[slot];
}

int ReaderView::Measure(uint32_t cp, void* ctx){
    return static_cast<ReaderView*>(ctx)->measure(cp);
}

ReaderLayout::Params ReaderView::params(){
    ReaderLayout::Params p;
    p.width = this->l_rect.w - kPadX * 2;
    const int text_h = this->l_rect.h - kPadTop - kFooterH;
    p.lines = text_h / this->linePitch();
    if(p.lines > ReaderLayout::kMaxLines) p.lines = ReaderLayout::kMaxLines;
    if(p.lines < 1) p.lines = 1;
    p.em = this->fontPx();
    p.measure = &ReaderView::Measure;
    p.ctx = this;
    return p;
}

void ReaderView::setPage(const ReaderLayout::Page* page){
    this->page_ = page;
    this->needsRender();
}

void ReaderView::setMessage(const char* text){
    this->message_.assign(text ? text : "");
    this->needsRender();
}

void ReaderView::setFooter(const char* left, const char* right, float progress){
    if(progress < 0) progress = 0;
    if(progress > 1) progress = 1;
    if(this->footer_left_ == left && this->footer_right_ == right &&
       (int)(progress * this->l_rect.w) == (int)(this->progress_ * this->l_rect.w)) return;
    this->footer_left_.assign(left ? left : "");
    this->footer_right_.assign(right ? right : "");
    this->progress_ = progress;
    const Rect g = this->getScreenRect();
    // 下の帯だけ描き直せばよいが、描くのは全体(render()は1枚で描く)
    PICO_GFX::MarkDirty({g.x, (int16_t)(g.y + g.h - kFooterH), g.w, (int16_t)kFooterH});
    this->needs_redraw = true;
}

void ReaderView::setBookmarked(bool on){
    if(on == this->bookmarked_) return;
    this->bookmarked_ = on;
    this->needsRender();
}

void ReaderView::setMenuBands(int top_h, int bottom_h){
    if(top_h == this->band_top_ && bottom_h == this->band_bottom_) return;
    this->band_top_ = top_h;
    this->band_bottom_ = bottom_h;
    this->needsRender();
}

void ReaderView::causeOnPressStart(){
    Widget::causeOnPressStart();
    this->start_x_ = OSData::touchX;
    this->start_y_ = OSData::touchY;
}

void ReaderView::causeOnPressEnd(){
    Widget::causeOnPressEnd();
    const int dx = OSData::touchX - this->start_x_;
    const int dy = OSData::touchY - this->start_y_;
    const int adx = dx < 0 ? -dx : dx;
    const int ady = dy < 0 ? -dy : dy;
    const Rect g = this->getScreenRect();

    // メニューを出している間は、帯の外を叩くと閉じる
    if(this->band_top_ > 0 || this->band_bottom_ > 0){
        const int ty = this->start_y_ - g.y;
        if(ty >= this->band_top_ && ty < g.h - this->band_bottom_ && this->on_menu_) this->on_menu_();
        return;
    }
    if(adx >= kSwipePx && adx > ady){
        if(this->on_turn_) this->on_turn_(dx < 0 ? +1 : -1);
        return;
    }
    if(adx >= kSwipePx || ady >= kSwipePx) return;
    const int tx = this->start_x_ - g.x;
    if(tx < g.w / 3){
        if(this->on_turn_) this->on_turn_(-1);
    }else if(tx >= g.w * 2 / 3){
        if(this->on_turn_) this->on_turn_(+1);
    }else if(this->on_menu_){
        this->on_menu_();
    }
}

void ReaderView::render(){
    if(!this->needs_redraw) return;
    if(!this->visible) return;

    const Rect g = this->getScreenRect();
    markdirty(g);
    LGFX_Sprite* f = OSData::frame;
    f->fillRect(g.x, g.y, g.w, g.h, this->background_color);

    int32_t ox, oy, ow, oh;
    f->getClipRect(&ox, &oy, &ow, &oh);
    const Rect orig = { (int16_t)ox, (int16_t)oy, (int16_t)ow, (int16_t)oh };
    const Rect clip = orig.intersection(g);
    f->setClipRect(clip.x, clip.y, clip.w, clip.h);

    this->setFont();
    if(this->page_){
        const int pitch = this->linePitch();
        // 行の上下の真ん中に文字を置く
        const int y0 = g.y + kPadTop + (pitch - this->fontPx()) / 2;
        char seg[ReaderLayout::kLineBytes];
        for(int i = 0; i < this->page_->count; i++){
            const ReaderLayout::Line& L = this->page_->lines[i];
            const int y = y0 + i * pitch;
            f->setCursor(g.x + kPadX + L.x, y);
            // ルビ(kRubyOn〜kRubyOff)は色を変える
            const char* p = L.text;
            bool ruby = false;
            while(*p){
                const char* q = p;
                while(*q && *q != ReaderLayout::kRubyOn && *q != ReaderLayout::kRubyOff) q++;
                const size_t n = (size_t)(q - p);
                if(n > 0){
                    memcpy(seg, p, n);
                    seg[n] = '\0';
                    f->setTextColor(ruby ? PICO_DARKGREY : PICO_BLACK);
                    f->print(seg);
                }
                if(!*q) break;
                ruby = (*q == ReaderLayout::kRubyOn);
                p = q + 1;
            }
        }
    }else if(!this->message_.empty()){
        f->setTextColor(PICO_DARKGREY);
        const int w = f->textWidth(this->message_.c_str());
        f->setCursor(g.x + (g.w - w) / 2, g.y + g.h / 2 - this->fontPx() / 2);
        f->print(this->message_.c_str());
    }

    // しおりの印(右上のリボン)
    if(this->bookmarked_){
        const int bx = g.x + g.w - 16;
        f->fillRect(bx, g.y, 9, 12, PICO_RED);
        f->fillTriangle(bx, g.y + 12, bx + 8, g.y + 12, bx + 4, g.y + 8, this->background_color);
    }

    // 下の帯: ページ番号と進み具合
    const int fy = g.y + g.h - kFooterH;
    f->setFont(&fonts::Font0);
    f->setTextSize(1);
    f->setTextColor(PICO_DARKGREY);
    f->setCursor(g.x + kPadX, fy + 1);
    f->print(this->footer_left_.c_str());
    const int rw = f->textWidth(this->footer_right_.c_str());
    f->setCursor(g.x + g.w - kPadX - rw, fy + 1);
    f->print(this->footer_right_.c_str());
    const int bar_w = (int)(this->progress_ * (float)(g.w - kPadX * 2));
    f->fillRect(g.x + kPadX, g.y + g.h - 2, g.w - kPadX * 2, 1, PICO_LIGHTGREY);
    if(bar_w > 0) f->fillRect(g.x + kPadX, g.y + g.h - 3, bar_w, 2, PICO_DARKGREY);

    // メニューの帯
    if(this->band_top_ > 0){
        f->fillRect(g.x, g.y, g.w, this->band_top_, PICO_WHITE);
        f->drawFastHLine(g.x, g.y + this->band_top_ - 1, g.w, PICO_DARKGREY);
    }
    if(this->band_bottom_ > 0){
        const int by = g.y + g.h - this->band_bottom_;
        f->fillRect(g.x, by, g.w, this->band_bottom_, PICO_WHITE);
        f->drawFastHLine(g.x, by, g.w, PICO_DARKGREY);
    }

    FontFn::SetDefault();
    f->setClipRect(orig.x, orig.y, orig.w, orig.h);
    this->needs_redraw = false;
}
