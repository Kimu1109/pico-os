#include "gui/core/Font_Functions.hpp"
#include "OS_Data.hpp"

namespace {
    bool large_text = false;
}

void FontFn::SetSmall(){
    OSData::frame->setFont(&lgfxJapanGothicP_16);
    OSData::frame->setTextSize(1);
}
void FontFn::SetNormal(){
    OSData::frame->setFont(&lgfxJapanGothicP_24);
    OSData::frame->setTextSize(1);
}
void FontFn::SetBig(){
    OSData::frame->setFont(&lgfxJapanGothicP_16);
    OSData::frame->setTextSize(2);
}
void FontFn::SetBigger(){
    OSData::frame->setFont(&lgfxJapanGothicP_24);
    OSData::frame->setTextSize(2);
}
void FontFn::SetText(){
    OSData::frame->setFont(GetText());
    OSData::frame->setTextSize(1);
}
void FontFn::SetDefault(){
    SetNormal();
}

void FontFn::SetFontSize(FontSize size){
    switch (size)
    {
    case FontSize::Small:
        SetSmall();
        break;
    case FontSize::Normal:
        SetNormal();
        break;
    case FontSize::Big:
        SetBig();
        break;
    case FontSize::Bigger:
        SetBigger();
        break;
    case FontSize::Text:
        SetText();
        break;
    default:
        SetSmall();
        break;
    }
}

int FontFn::GetFontSize(FontSize size){
    switch (size)
    {
    case FontSize::Small:
        return 16;
    case FontSize::Normal:
        return 24;
    case FontSize::Big:
        return 32;
    case FontSize::Bigger:
        return 64;
    case FontSize::Text:
        return large_text ? 20 : 16;
    default:
        return 16;
    }
}

const lgfx::v1::U8g2font* FontFn::GetSmall() {
    return &lgfxJapanGothicP_16;
}
const lgfx::v1::U8g2font* FontFn::GetNormal() {
    return &lgfxJapanGothicP_24;
}
const lgfx::v1::U8g2font* FontFn::GetText() {
    // 20pxは本文を大きくしたときだけ使う(フラッシュに約215KB。16px=約160KB、24px=約275KB)
    return large_text ? &lgfxJapanGothicP_20 : &lgfxJapanGothicP_16;
}

void FontFn::SetLargeText(bool large){ large_text = large; }
bool FontFn::IsLargeText(){ return large_text; }

FontFn::FontSize FontFn::FromInt(int value){
    if(value < (int)Small || value > (int)Bigger) return Small;
    return (FontSize)value;
}
