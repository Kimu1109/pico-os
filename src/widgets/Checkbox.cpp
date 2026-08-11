#include "widgets/Checkbox.hpp"
#include "OS_Data.hpp"
#include "functions/GFX_Functions.hpp"
#include "icons/icon_render.h"
#include "icons/icons_data.h"

void Checkbox::render(){
    if(!this->visible) return;
    if(!this->needs_redraw) return;

    if(this->prev_rect != this->rect)
        PICO_GFX::markDirty(this->prev_rect);

    IconRender::DrawIcon(this->isChecked ? IconID::CheckboxOn : IconID::CheckboxOff, IconSize::Px24, this->rect.x, this->rect.y, PICO_BLACK);
    OSData::frame->setCursor(this->rect.x + 24 + 2, this->rect.y);
    OSData::frame->print(this->text);

    PICO_GFX::markDirty(this->rect);
    this->prev_rect.copy(this->rect);

    this->needs_redraw = false;
}

void Checkbox::onPressStart(){
    if(this->on_press_start) this->on_press_start();

    if(
        OSData::touchX >= this->rect.x && OSData::touchX <= this->rect.x + 24 &&
        OSData::touchY >= this->rect.y && OSData::touchY <= this->rect.y + this->rect.h
    ){
        this->isChecked = !this->isChecked;
        this->needsRender();
    }
}

void Checkbox::setText(String text){
    this->text = text;
    this->rect.w = OSData::frame->textWidth(text) + 24 + 2;
    this->rect.h = 24;
    if(this->rect.x + this->rect.w > SCREEN_WIDTH){
        this->rect.w = SCREEN_WIDTH - this->rect.x;
    }
}