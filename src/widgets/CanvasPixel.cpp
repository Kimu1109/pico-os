#include "widgets/CanvasPixel.hpp"
#include "OS_Data.hpp"
#include "functions/GFX_Functions.hpp"

CanvasPixel::CanvasPixel(int16_t x, int16_t y, int16_t w, int16_t h){
    this->rect = {x, y, w, h};

    sp = new LGFX_Sprite(OSData::frame);
    sp->setColorDepth(4);
    sp->createSprite(w, h);
    for(int i = 0; i < 16; i++){
        sp->setPaletteColor(i, PICO_GFX::COLORS[i]);
    }
    sp->setBaseColor(PICO_WHITE);
    sp->clear(PICO_WHITE);
    sp->setFont(&lgfxJapanGothicP_24);
    sp->setTextColor(PICO_BLACK);
    sp->setTextWrap(false, false);
}

void CanvasPixel::render(){
    if(!this->visible) return;
    if(!this->needs_redraw) return;

    sp->pushSprite(OSData::frame, this->rect.x, this->rect.y);

    this->needs_redraw = false;
}

void CanvasPixel::onPressStart(){
    sx = OSData::touchX;
    sy = OSData::touchY;
}

void CanvasPixel::onPressMove(){
    if(
        abs(sx - OSData::touchX) >= 2 ||
        abs(sy - OSData::touchY) >= 2
    ){
        sp->drawWideLine(
            sx - this->rect.x,
            sy - this->rect.y,
            OSData::touchX - this->rect.x,
            OSData::touchY - this->rect.y,
            1.5,
            PICO_BLACK
        );
        sx = OSData::touchX;
        sy = OSData::touchY;
        this->needsRender();
    }
}

void CanvasPixel::onPressEnd(){

}

void CanvasPixel::CanvasClear(){
    sp->clear(PICO_WHITE);
    this->needsRender();
}