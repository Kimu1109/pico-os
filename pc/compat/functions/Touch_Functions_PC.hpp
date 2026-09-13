#pragma once

#include "consts.hpp"
#include "OS_Data.hpp"
#include "functions/Log_Functions.hpp"

// PCビルド用のタッチ入力。
//
// 実機版(src/functions/Touch_Functions.hpp)はXPT2046からSPIで座標を読むが、
// PCではSDLのマウスをタッチとして扱う。タッチ取得はハードウェアそのものなので、
// 座標変換を逆算して実機版へ流し込むより、この層ごと差し替えるほうが素直。
//
// OSData側へ渡す状態(isTouched / isTouchStart / isTouchEnd / isTouchMove)の
// 作り方は実機版と揃えてある。いずれも1tickだけ立つ。
namespace PICO_Touch
{
    inline int prev_x = 0;
    inline int prev_y = 0;

    inline void Setup(){
        LOG_SYS_OK("Touch Setup has succeeded! (PC: マウス入力)");
    }

    inline void Update(){
        if(OSData::isTouchStart)
            OSData::isTouchStart = false;

        if(OSData::isTouchMove) OSData::isTouchMove = false;
        if(prev_x != OSData::touchX || prev_y != OSData::touchY){
            OSData::isTouchMove = true;
        }

        prev_x = OSData::touchX;
        prev_y = OSData::touchY;

        //LovyanGFXのSDLパネルがマウスをタッチとして報告してくれる
        int32_t x = 0, y = 0;
        const bool touched = OSData::lcd && OSData::lcd->getTouch(&x, &y);

        if(touched){
            OSData::touchX = (int)((x < 0) ? 0 : (x >= SCREEN_WIDTH  ? SCREEN_WIDTH  - 1 : x));
            OSData::touchY = (int)((y < 0) ? 0 : (y >= SCREEN_HEIGHT ? SCREEN_HEIGHT - 1 : y));
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
