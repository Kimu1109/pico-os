#include "apps/calculator/ui/BaseView.hpp"
#include "apps/calculator/Calc_Prog.hpp"
#include "gui/core/Font_Functions.hpp"
#include "gui/core/GFX_Functions.hpp"
#include "OS_Data.hpp"

#include <cstring>
#include <cstdio>

namespace {
    const char* const kTags[BaseView::kRowCount] = { "HEX", "DEC", "OCT", "BIN" };
    const int kBases[BaseView::kRowCount] = { 16, 10, 8, 2 };

    // 見出しの箱の幅(Font0の3文字+余白)
    constexpr int kTagW = 24;
    // 2進の1桁の幅(Font0)と、4桁ごとの隙間
    constexpr int kBinDigitW = 6;
    constexpr int kBinGap = 2;
}

int BaseView::RowHeight(){ return 17; }
int BaseView::BinLineHeight(){ return 10; }
int BaseView::FittingHeight(){ return RowHeight() * (kRowCount - 1) + BinLineHeight() * 2 + 2; }

int BaseView::BaseOfRow(int row){ return (row >= 0 && row < kRowCount) ? kBases[row] : 10; }
int BaseView::RowOfBase(int base){
    for(int i = 0; i < kRowCount; i++) if(kBases[i] == base) return i;
    return 1;
}

int BaseView::rowY(int row) const { return row * RowHeight(); }
int BaseView::rowH(int row) const {
    if(row == kRowCount - 1) return this->l_rect.h - RowHeight() * (kRowCount - 1);
    return RowHeight();
}

void BaseView::setValue(int64_t v, int word_bits){
    if(this->has_value && this->value == v && this->bits == word_bits && this->error.empty()) return;
    this->value = v;
    this->bits = word_bits;
    this->has_value = true;
    this->error.clear();
    this->needsRender();
}

void BaseView::clearValue(){
    if(!this->has_value && this->error.empty()) return;
    this->has_value = false;
    this->error.clear();
    this->needsRender();
}

void BaseView::setError(const char* message){
    this->has_value = false;
    this->error.assign(message ? message : "");
    this->needsRender();
}

void BaseView::rowText(int row, char* out, size_t cap) const {
    if(!out || cap == 0) return;
    out[0] = '\0';
    if(!this->has_value || row < 0 || row >= kRowCount) return;
    const int b = kBases[row];
    if(b != 2){
        CalcProg::Format(this->value, b, this->bits, out, cap);
        return;
    }
    //2進は語長ぶんを0埋め
    const uint64_t u = (uint64_t)this->value & CalcProg::Mask(this->bits);
    size_t o = 0;
    for(int i = this->bits - 1; i >= 0 && o + 1 < cap; i--) out[o++] = ((u >> i) & 1) ? '1' : '0';
    out[o] = '\0';
}

void BaseView::rowCenter(int row, int& x, int& y) const {
    const Rect g = this->getScreenRect();
    x = g.x + g.w / 2;
    y = g.y + this->rowY(row) + this->rowH(row) / 2;
}

void BaseView::causeOnPressStart(){
    Widget::causeOnPressStart();
    const Rect g = this->getScreenRect();
    const int ly = OSData::touchY - g.y;
    for(int row = 0; row < kRowCount; row++){
        if(ly < this->rowY(row) || ly >= this->rowY(row) + this->rowH(row)) continue;
        const int b = kBases[row];
        if(b != this->base){
            this->base = b;
            this->needsRender();
            if(this->on_select_base) this->on_select_base(b);
        }
        return;
    }
}

void BaseView::render(){
    if(!this->needs_redraw) return;
    if(!this->visible) return;

    const Rect g = this->getScreenRect();
    markdirty(g);

    LGFX_Sprite* f = OSData::frame;
    f->fillRect(g.x, g.y, g.w, g.h, this->background_color);

    char text[72];
    for(int row = 0; row < kRowCount; row++){
        const int y = g.y + this->rowY(row);
        const int h = this->rowH(row);
        const bool selected = (kBases[row] == this->base);

        //見出し: 選んでいる基数は反転(TabBarの選択中と同じ見た目)
        const int tag_h = (row == kRowCount - 1) ? RowHeight() - 2 : h - 2;
        if(selected) f->fillRect(g.x, y + 1, kTagW, tag_h, PICO_BLACK);
        else         f->drawRect(g.x, y + 1, kTagW, tag_h, PICO_DARKGREY);
        f->setFont(&fonts::Font0);
        f->setTextSize(1);
        f->setTextColor(selected ? PICO_WHITE : PICO_BLACK);
        f->setCursor(g.x + (kTagW - f->textWidth(kTags[row])) / 2, y + 1 + (tag_h - f->fontHeight()) / 2);
        f->print(kTags[row]);

        if(!this->has_value) continue;
        this->rowText(row, text, sizeof(text));
        const int right = g.x + g.w - 2;
        f->setTextColor(selected ? PICO_FORECOLOR : PICO_DARKGREY);

        if(kBases[row] != 2){
            FontFn::SetSmall();
            const int w = f->textWidth(text);
            f->setCursor(right - w, y + (h - f->fontHeight()) / 2);
            f->print(text);
            continue;
        }

        //2進: 1行に32桁まで、4桁ごとに隙間。上の行が上位
        const int len = (int)strlen(text);
        const int lines = (len + 31) / 32;
        for(int line = 0; line < lines; line++){
            const int start = line * 32;
            const int n = (len - start) < 32 ? (len - start) : 32;
            const int line_w = n * kBinDigitW + ((n - 1) / 4) * kBinGap;
            int x = right - line_w;
            const int ly = y + 2 + line * BinLineHeight();
            for(int i = 0; i < n; i++){
                if(i > 0 && i % 4 == 0) x += kBinGap;
                const char digit[2] = { text[start + i], '\0' };
                f->setCursor(x, ly);
                f->print(digit);
                x += kBinDigitW;
            }
        }
    }

    //誤り: 値の欄(見出しの右)へ赤字で
    if(!this->has_value && !this->error.empty()){
        FontFn::SetSmall();
        f->setTextColor(PICO_RED);
        f->setCursor(g.x + kTagW + 4, g.y + this->rowY(1) + (RowHeight() - f->fontHeight()) / 2);
        f->print(this->error.c_str());
    }

    f->setTextColor(PICO_BLACK);
    FontFn::SetDefault();
    this->needs_redraw = false;
}
