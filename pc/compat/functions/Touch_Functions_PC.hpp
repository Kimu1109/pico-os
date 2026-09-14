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
// スクリプト再生("--tap X,Y@FRAME")で使う仕込み。
//
// ヘッドレス(--shot)ではSDLへマウスが来ないので、画面を撮りたい場所まで
// 操作を進める手段が要る。実機のタッチは「押している間ずっと座標が来る」ので、
// ここでも指定フレームからHOLDフレームぶん同じ座標を返す形にしてある。
namespace PicoOsTouchScript {
    struct Tap {
        int frame = 0;   // 何フレーム目で押し始めるか
        int hold = 3;    // 何フレーム押し続けるか
        int x = 0;
        int y = 0;
    };

    constexpr int kMaxTaps = 16;

    inline Tap taps[kMaxTaps];
    inline int count = 0;
    inline int frame = 0;   // PICO_Touch::Update()が呼ばれた回数
    inline bool enabled = false;

    inline bool Add(int x, int y, int at_frame, int hold){
        if(count >= kMaxTaps) return false;
        taps[count++] = Tap{at_frame, (hold > 0 ? hold : 1), x, y};
        enabled = true;
        return true;
    }

    // 今のフレームで押されている座標があれば true
    inline bool Current(int& x, int& y){
        for(int i = 0; i < count; i++){
            const Tap& t = taps[i];
            if(frame >= t.frame && frame < t.frame + t.hold){
                x = t.x;
                y = t.y;
                return true;
            }
        }
        return false;
    }
}

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

        int32_t x = 0, y = 0;
        bool touched = false;

        if(PicoOsTouchScript::enabled){
            //スクリプト再生中はSDLを見ない(ヘッドレスではマウスが来ないため)
            int sx = 0, sy = 0;
            touched = PicoOsTouchScript::Current(sx, sy);
            x = sx;
            y = sy;
            PicoOsTouchScript::frame++;
        }else{
            //LovyanGFXのSDLパネルがマウスをタッチとして報告してくれる
            touched = OSData::lcd && OSData::lcd->getTouch(&x, &y);
        }

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
