
#pragma once

// タッチ入力の取得はハードウェアそのものなので、PCビルドでは丸ごと差し替える
// (PC版はSDLのマウスをタッチとして扱う)。理由はLGFX_Config.hppと同じ。
#if defined(PICOOS_PC)
    #include <functions/Touch_Functions_PC.hpp>
#else

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

    // ライブラリ内部のZ_THRESHOLD(400)はごく僅かな接触でも真になり敏感すぎるため、
    // アプリ側でさらに高い閾値を要求する。ライブラリはこれ未満だと座標そのものを
    // 更新せずz=0を返す仕様なので、getPoint().zをこの値と比較するだけで済む。
    // 実機で様子を見ながら調整すること。
    inline const int16_t TOUCH_Z_THRESHOLD = 500;

    inline const int SCREEN_W = 240;
    inline const int SCREEN_H = 320;

    inline int prev_x = 0;
    inline int prev_y = 0;

    inline void Setup(){
        pinMode(TOUCH_IRQ, INPUT_PULLUP);

        touchSPI.begin();
        ts.begin(touchSPI);
        ts.setRotation(1); // lcdのsetRotationと合わせる

        // XPT2046は起動直後、一度も変換コマンドを受け取っていない状態だと
        // PENIRQ(IRQピン)の生成回路が正しくアクティブ化されず、HIGH固定のまま
        // 動かないことがある。IRQゲート越しにしかSPI通信しないUpdate()だけでは
        // このデッドロックを抜けられないため、起動時に一度だけ強制的に叩いて起こす。
        touchSPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
        ts.touched();
        touchSPI.endTransaction();

        LOG_SYS_OK("Touch Setup has succeeded!");
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

        //IRQゲート: IRQピンがLOW(タッチ検知)の間だけSPI通信を行う
        bool touched = false;
        if(digitalRead(TOUCH_IRQ) == LOW){
            touchSPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
            TS_Point p = ts.getPoint();
            touched = (p.z >= TOUCH_Z_THRESHOLD);
            if(touched){
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
        }

        if(!touched){
            //タッチ終了のお知らせ(1tick)
            if(OSData::isTouchEnd)
                OSData::isTouchEnd = false;
            if(OSData::isTouched){
                OSData::isTouchEnd = true;
            }

            OSData::isTouched = false;
            return;
        }

        //タッチ開始のおしらせ(1tick)
        if(!OSData::isTouched)
            OSData::isTouchStart = true;

        OSData::isTouched = true;
    }
}

#endif // PICOOS_PC
