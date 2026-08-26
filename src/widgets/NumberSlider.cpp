#include "widgets/NumberSlider.hpp"
#include "OS_Data.hpp"
#include "functions/Font_Functions.hpp"
#include "functions/GFX_Functions.hpp"

void NumberSlider::updateTextW(){
    FontFn::SetSmall();
    int maxV_W = OSData::frame->textWidth(String(this->maxValue, this->decimalPlacesNum));
    int minV_W = OSData::frame->textWidth(String(this->minValue, this->decimalPlacesNum));
    text_max_w = max(maxV_W, minV_W);
    FontFn::SetDefault();
}

void NumberSlider::causeOnPressMove(){
    Widget::causeOnPressMove();

    int numW = 0;
    if(this->visibleNum){
        numW = text_max_w + 2;
    }

    const Rect g_rect = this->getScreenRect();

    int before_value = this->value;

    this->value = this->minValue + ((float)(OSData::touchX - g_rect.x - numW) / (float)(g_rect.w - numW)) * (float)(this->maxValue - this->minValue);
    this->value = min(max(this->value, this->minValue), this->maxValue);

    if(before_value != this->value){
        this->causeOnValueChanged();
    }

    this->needsRender();
}

void NumberSlider::render(){
    if(!this->visible) return;
    if(!this->needs_redraw) return;

    if(this->prev_l_rect != this->l_rect)
        markdirty(this->getScreenPrevRect());

    const Rect g_rect = this->getScreenRect();

    int numW = 0;
    if(this->visibleNum){
        FontFn::SetSmall();
        String numStr = String(this->value, this->decimalPlacesNum);
        numW = text_max_w + 2;
        OSData::frame->setCursor(g_rect.x, g_rect.y + (g_rect.h - OSData::frame->fontHeight()) / 2);
        OSData::frame->setTextColor(this->color);
        OSData::frame->print(numStr);
        OSData::frame->setTextColor(PICO_FORECOLOR);
        FontFn::SetNormal();
    }

    //始点、終点
    OSData::frame->fillRect(
        g_rect.x + numW, g_rect.y,
        2, 21, this->color
    );
    OSData::frame->fillRect(
        g_rect.x + g_rect.w - 2, g_rect.y,
        2, 21, this->color
    );
    
    //横線
    OSData::frame->fillRect(
        g_rect.x + numW, g_rect.y + 10,
        g_rect.w, 2, this->color
    );
    
    //現在値
    int slide = ((float)this->value / (float)(this->maxValue - this->minValue)) * (g_rect.w - numW);
    OSData::frame->fillRect(
        g_rect.x - 2 + slide + numW, g_rect.y,
        2 * 2 + 1, 21,
        this->color
    );

    markdirty(g_rect);
    this->prev_l_rect.copy(this->l_rect);

    this->needs_redraw = false;
}