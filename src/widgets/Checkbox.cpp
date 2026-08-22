#include "widgets/Checkbox.hpp"
#include "OS_Data.hpp"
#include "functions/GFX_Functions.hpp"
#include "icons/icon_render.h"
#include "icons/icons_data.h"

void Checkbox::render(){
    if(!this->visible) return;
    if(!this->needs_redraw) return;

    if(this->prev_l_rect != this->l_rect)
        PICO_GFX::markDirty(getScreenPrevRect());

    int font_pix = FontFn::GetFontSize(this->f_size);

    const Rect g_rect = this->getScreenRect();

    IconRender::DrawIcon(
        this->isChecked ? IconID::CheckboxOn : IconID::CheckboxOff,
        IconRender::GetIconSize(font_pix),
        g_rect.x, g_rect.y, this->text_color
    );
    OSData::frame->setCursor(g_rect.x + font_pix + 2, g_rect.y);
    this->fontApply();
    this->textColorApply();
    OSData::frame->print(this->text);
    this->textColorDefault();
    this->fontDefault();

    PICO_GFX::markDirty(g_rect);
    this->prev_l_rect.copy(this->l_rect);

    this->needs_redraw = false;
}

void Checkbox::onPressStart(){
    if(this->on_press_start) this->on_press_start();

    const Rect g_rect = getScreenRect();

    if(
        OSData::touchX >= g_rect.x && OSData::touchX <= g_rect.x + 24 &&
        OSData::touchY >= g_rect.y && OSData::touchY <= g_rect.y + g_rect.h
    ){
        this->isChecked = !this->isChecked;
        this->needsRender();
    }
}

void Checkbox::setText(String text){
    this->text = text;

    this->fontApply();

    int iconSize = FontFn::GetFontSize(this->f_size);

    this->l_rect.w = OSData::frame->textWidth(text) + iconSize + 2;
    this->l_rect.h = OSData::frame->fontHeight();

    this->fontDefault();

    if(this->getScreenX() + this->l_rect.w > SCREEN_WIDTH){
        this->l_rect.w = SCREEN_WIDTH - this->getScreenX();
    }
}