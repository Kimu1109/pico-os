#include "widgets/Image.hpp"
#include "functions/GFX_Functions.hpp"
#include "icons/icon_render.h"
#include "OS_Data.hpp"

void Image::render(){
    if(!this->needs_redraw) return;
    if(!this->visible) return;
    if(this->onRAM){
        if(!this->sprite.usable) return;
    }else{
        if(!this->imgFile) return;
    }

    if(this->rect != this->prev_rect)
        PICO_GFX::markDirty(this->prev_rect);

    if(this->onRAM){
        IconRender::DrawPimgSprite(this->sprite, this->rect.x, this->rect.y);
    }else{
        IconRender::DrawImageRLE4bpp(this->imgFile, this->rect.x, this->rect.y);
    }

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

void Image::updateSprite(){
    this->imgFile = OSData::SD.open(this->path);

    if(this->imgFile){
        if(IconRender::LoadPimgToSprite(this->imgFile, this->sprite)){
            this->rect.w = this->sprite.width;
            this->rect.h = this->sprite.height;
        }
        this->imgFile.close();
    }
}