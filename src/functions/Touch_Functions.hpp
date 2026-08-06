
#pragma once

#include <XPT2046_Touchscreen.h>
#include <SPI.h>
#include "consts.hpp"
#include "OS_Data.hpp"

namespace PICO_Touch
{
    inline SPIClassRP2040 touchSPI(spi0, TOUCH_MISO, TOUCH_CS, TOUCH_SCK, TOUCH_MOSI);
    inline XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

    inline const int TS_MINX = 300;
    inline const int TS_MAXX = 3800;
    inline const int TS_MINY = 300;
    inline const int TS_MAXY = 3800;

    inline const int SCREEN_W = 240;
    inline const int SCREEN_H = 320;

    inline int prev_x = 0;
    inline int prev_y = 0;

    inline void Setup(){
        touchSPI.begin();
        ts.begin(touchSPI);
        ts.setRotation(1); // lcdのsetRotationと合わせる
    }

    inline void Update(){
        if(OSData::isTouchStart)
            OSData::isTouchStart = false;

        // 比較
        if(OSData::isTouchMove) OSData::isTouchMove = false;
        if(prev_x != OSData::touchX || prev_y != OSData::touchY){
            OSData::isTouchMove = true;
        }

        // 保存
        prev_x = OSData::touchX;
        prev_y = OSData::touchY;

        touchSPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
        {
            if (!ts.touched()) {

                //タッチ終了のお知らせ(1tick)
                if(OSData::isTouchEnd)
                    OSData::isTouchEnd = false;
                if(OSData::isTouched){
                    OSData::isTouchEnd = true;
                }

                OSData::isTouched = false;
                return;
            }

            TS_Point p = ts.getPoint();

            // p.x と p.y を入れ替え、横方向(x)は反転してマッピング
            int16_t x = map(p.y, TS_MINY, TS_MAXY, SCREEN_W - 1, 0);
            int16_t y = map(p.x, TS_MINX, TS_MAXX, 0, SCREEN_H - 1);

            // はみ出し防止
            x = constrain(x, 0, SCREEN_W - 1);
            y = constrain(y, 0, SCREEN_H - 1);

            OSData::touchX = x;
            OSData::touchY = y;
        }
        touchSPI.endTransaction();

        //タッチ開始のおしらせ(1tick)
        if(!OSData::isTouched)
            OSData::isTouchStart = true;

        OSData::isTouched = true;
    } 
}
