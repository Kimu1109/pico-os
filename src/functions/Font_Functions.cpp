#include "functions/Font_Functions.hpp"
#include "OS_Data.hpp"

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