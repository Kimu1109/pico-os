#include "widgets/Image.hpp"
#include "functions/GFX_Functions.hpp"
#include "icons/icon_render.h"
#include "OS_Data.hpp"

void Image::render(){
    if(!this->needs_redraw) return;
    if(!this->visible) return;
    if(!this->imgFile) return;

    if(this->rect != this->prev_rect)
        PICO_GFX::markDirty(this->prev_rect);

    OSData::frame->fillRect(this->rect.x, this->rect.y, this->rect.w, this->rect.h, PICO_RED);
    IconRender::DrawImageRLE4bpp(this->imgFile, this->rect.x, this->rect.y);

    PICO_GFX::markDirty(this->rect);
    this->prev_rect.copy(this->rect);

    this->needs_redraw = false;
}

void Image::updatePath(){
    this->imgFile = OSData::SD.open(this->path);
    if(this->imgFile){
        IconRender::PimgHeader head;
        if(IconRender::ReadPimgHeader(this->imgFile, head)){
            this->rect.w = head.width;
            this->rect.h = head.height;
        }
    }
}