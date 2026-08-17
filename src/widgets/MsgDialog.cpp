#include "widgets/MsgDialog.hpp"
#include "OS_Data.hpp"
#include "functions/GFX_Functions.hpp"

void MsgDialog::render(){
    if(!needs_redraw) return;
    if(!visible) return;

    PICO_GFX::markDirty(this->rect);

    OSData::frame->drawRect(this->rect.x, this->rect.y, this->rect.w, this->rect.h, PICO_BLACK);

    needs_redraw = false;
}