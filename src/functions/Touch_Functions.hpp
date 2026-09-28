
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
#include "util/TouchFilter.hpp"

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

    // タッチのSPIクロック。TFT(TFT_MAX_SPEED、consts.hpp)と同じSPI0バスを共有しており、
    // ブレッドボード配線ではクロックが高いほど信号の乱れ(リンギング/クロストーク)の
    // 影響を受けやすい。XPT2046は元々低速でも十分動く(データシート上は最大2MHz程度)ので、
    // 安全側に落としてある。2026-09-28、実機で「線がなめらかに引けない」問題の切り分けとして
    // 1MHzから下げた。これで改善するかは実機で様子を見ながら判断すること
    inline const uint32_t TOUCH_SPI_HZ = 1000000;

    inline const int SCREEN_W = 240;
    inline const int SCREEN_H = 320;

    inline int prev_x = 0;
    inline int prev_y = 0;

    // タッチ座標の単発ノイズ(スパイク)を抑える。util/TouchFilter.hpp参照
    inline TouchFilter touch_filter;

    inline void Setup(){
        pinMode(TOUCH_IRQ, INPUT_PULLUP);

        touchSPI.begin();
        ts.begin(touchSPI);
        ts.setRotation(1); // lcdのsetRotationと合わせる

        // XPT2046は起動直後、一度も変換コマンドを受け取っていない状態だと
        // PENIRQ(IRQピン)の生成回路が正しくアクティブ化されず、HIGH固定のまま
        // 動かないことがある。IRQゲート越しにしかSPI通信しないUpdate()だけでは
        // このデッドロックを抜けられないため、起動時に一度だけ強制的に叩いて起こす。
        touchSPI.beginTransaction(SPISettings(TOUCH_SPI_HZ, MSBFIRST, SPI_MODE0));
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
            touchSPI.beginTransaction(SPISettings(TOUCH_SPI_HZ, MSBFIRST, SPI_MODE0));
            TS_Point p = ts.getPoint();
            touched = (p.z >= TOUCH_Z_THRESHOLD);
            if(touched){
                // p.x と p.y を入れ替え、横方向(x)は反転してマッピング
                int16_t x = map(p.y, TS_MINY, TS_MAXY, SCREEN_W - 1, 0);
                int16_t y = map(p.x, TS_MINX, TS_MAXX, 0, SCREEN_H - 1);

                // はみ出し防止
                x = constrain(x, 0, SCREEN_W - 1);
                y = constrain(y, 0, SCREEN_H - 1);

                // 単発ノイズ(スパイク)を抑える(TouchFilter.hpp参照)。新しいタッチの
                // 最初のフレームは履歴をこの座標で埋め直す(前のタッチの名残りが
                // 混ざらないように。!OSData::isTouchedは下のisTouchStart判定と同じ条件)
                int16_t fx, fy;
                if(!OSData::isTouched){
                    touch_filter.reset(x, y);
                    fx = x; fy = y;
                }else{
                    touch_filter.push(x, y, fx, fy);
                }

                OSData::touchX = fx;
                OSData::touchY = fy;
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
