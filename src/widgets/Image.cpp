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

    if(this->l_rect != this->prev_l_rect)
        markdirty(getScreenPrevRect());

    const Rect g_rect = this->getScreenRect();

    if(this->onRAM){
        IconRender::DrawPimgSprite(this->sprite, g_rect.x, g_rect.y);
    }else{
        IconRender::DrawImageRLE4bpp(this->imgFile, g_rect.x, g_rect.y);
    }
    Serial.println("Image widget render!");

    markdirty(g_rect);
    this->prev_l_rect.copy(this->l_rect);

    this->needs_redraw = false;
}

void Image::updatePath(){
    this->imgFile = OSData::SD.open(this->path);
    if(this->imgFile){
        IconRender::PimgHeader head;
        if(IconRender::ReadPimgHeader(this->imgFile, head)){
            this->l_rect.w = head.width;
            this->l_rect.h = head.height;
        }
    }
}

void Image::updateSprite(){
    this->imgFile = OSData::SD.open(this->path);

    if(this->imgFile){
        if(IconRender::LoadPimgToSprite(this->imgFile, this->sprite)){
            this->l_rect.w = this->sprite.width;
            this->l_rect.h = this->sprite.height;
        }
        this->imgFile.close();
    }
}