#include "functions/Widget_Functions.hpp"
#include "config/LGFX_Config.hpp"
#include "OS_Data.hpp"
#include <SPI.h>

#include "GFX_Functions.hpp"

void PICO_GFX::Setup(){

    const static int COLORS[16] = {
        TFT_BLACK,
        TFT_NAVY,
        TFT_DARKGREEN,
        TFT_DARKCYAN,
        TFT_MAROON,
        TFT_PURPLE,
        TFT_OLIVE,
        TFT_LIGHTGREY,
        TFT_DARKGREY,
        TFT_BLUE,
        TFT_GREEN,
        TFT_CYAN,
        TFT_RED,
        TFT_MAGENTA,
        TFT_YELLOW,
        TFT_WHITE
    };

    OSData::lcd->init();
    OSData::lcd->setBaseColor(TFT_WHITE);
    OSData::lcd->clear(TFT_WHITE);
    OSData::lcd->setFont(&lgfxJapanGothicP_24);
    OSData::lcd->setTextColor(TFT_BLACK);

    LGFX_Sprite* frame = new LGFX_Sprite(OSData::lcd);
    frame->setColorDepth(4);
    frame->createSprite(SCREEN_WIDTH, SCREEN_HEIGHT);
    for(int i = 0; i < 16; i++){
        frame->setPaletteColor(i, COLORS[i]);
    }
    frame->setBaseColor(PICO_WHITE);
    frame->clear(PICO_WHITE);
    frame->setFont(&lgfxJapanGothicP_24);
    frame->setTextColor(PICO_BLACK);
    OSData::frame = frame;
    markDirtyXYWH(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

    pinMode(22, OUTPUT); //LED ON
    digitalWrite(22, HIGH);

    dirtyRects.reserve(48);
}

void PICO_GFX::markDirtyXYWH(int16_t x, int16_t y, int16_t w, int16_t h) {
    dirtyRects.push_back({x, y, w, h});
}
void PICO_GFX::markDirty(const Rect& rect) {
    dirtyRects.push_back(rect);
}

void PICO_GFX::flushDirty() {                                                                                                                                                                                                                                                                                          
    for (auto& d : dirtyRects) {                                                                                                                                                                                                                                                                                       
        std::vector<Widget*> hit;                                                                                                                                                                                                                                                                                      
        for (auto* w : WidgetFunctions::widgets) {                                                                                                                                                                                                                                                                     
            if (w && w->Visible() && w->getRect().intersects(d)) hit.push_back(w);                                                                                                                                                                                                                                     
        }                                                                                                                                                                                                                                                                                                              
                                                                                                                                                                                                                                                                                                                        
        // ★ 1. 描画前に、この Dirty 領域 d の背景を一度だけ白クリアする                                                                                                                                                                                                                                               
        fillBackground(d);                                                                                                                                                                                                                                                                                             
                                                                                                                                                                                                                                                                                                                        
        // ★ 2. 下から上へ Widget を重ね描きする (各 Widget は自分の文字/グラフィックのみを描く)                                                                                                                                                                                                                       
        for (size_t i = 0; i < hit.size(); ++i) {                                                                                                                                                                                                                                                                      
            Rect clip = hit[i]->getRect().intersection(d);                                                                                                                                                                                                                                                             
            if (clip.w <= 0 || clip.h <= 0) continue;                                                                                                                                                                                                                                                                  
            OSData::frame->setClipRect(clip.x, clip.y, clip.w, clip.h);                                                                                                                                                                                                                                                
            hit[i]->renderForce();                                                                                                                                                                                                                                                                                     
            OSData::frame->clearClipRect();                                                                                                                                                                                                                                                                            
        }

        // ★ 3. 液晶へ転送
        OSData::lcd->setClipRect(d.x, d.y, d.w, d.h);
        OSData::frame->pushSprite(OSData::lcd, 0, 0);
        OSData::lcd->clearClipRect();
    }
    dirtyRects.clear();
}


void PICO_GFX::fillBackgroundXYWH(int16_t x, int16_t y, int16_t w, int16_t h){
    fillBackground({x, y, w, h});
}
void PICO_GFX::fillBackground(const Rect& rect){
    OSData::frame->fillRect(rect.x, rect.y, rect.w, rect.h, PICO_BACKGROUND);
    markDirty(rect);
}
