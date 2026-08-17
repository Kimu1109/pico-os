#include "widgets/NumberSlider.hpp"
#include "OS_Data.hpp"
#include "functions/Font_Functions.hpp"

void NumberSlider::updateTextW(){
    FontFn::SetSmall();
    int maxV_W = OSData::frame->textWidth(String(this->maxValue, this->decimalPlacesNum));
    int minV_W = OSData::frame->textWidth(String(this->minValue, this->decimalPlacesNum));
    text_max_w = max(maxV_W, minV_W);
    FontFn::SetDefault();
}

void NumberSlider::onPressMove(){
    Widget::onPressMove();

    int numW = 0;
    if(this->visibleNum){
        numW = text_max_w + 2;
    }

    this->value = this->minValue + ((float)(OSData::touchX - this->rect.x - numW) / (float)(this->rect.w - numW)) * (float)(this->maxValue - this->minValue);
    this->value = min(max(this->value, this->minValue), this->maxValue);
    this->needsRender();
}

void NumberSlider::render(){
    if(!this->visible) return;
    if(!this->needs_redraw) return;

    int numW = 0;
    if(this->visibleNum){
        FontFn::SetSmall();
        String numStr = String(this->value, this->decimalPlacesNum);
        numW = text_max_w + 2;
        OSData::frame->setCursor(this->rect.x, this->rect.y + (this->rect.h - OSData::frame->fontHeight()) / 2);
        OSData::frame->setTextColor(this->color);
        OSData::frame->print(numStr);
        OSData::frame->setTextColor(PICO_FORECOLOR);
        FontFn::SetNormal();
    }

    //始点、終点
    OSData::frame->fillRect(
        this->rect.x + numW, this->rect.y,
        2, 21, this->color
    );
    OSData::frame->fillRect(
        this->rect.x + this->rect.w - 2, this->rect.y,
        2, 21, this->color
    );
    
    //横線
    OSData::frame->fillRect(
        this->rect.x + numW, this->rect.y + 10,
        this->rect.w, 2, this->color
    );
    
    //現在値
    int slide = ((float)this->value / (float)(this->maxValue - this->minValue)) * (this->rect.w - numW);
    OSData::frame->fillRect(
        this->rect.x - 2 + slide + numW, this->rect.y,
        2 * 2 + 1, 21,
        this->color
    );

    this->needs_redraw = false;
}