#pragma once

#include "consts.hpp"
#include "hal/TouchCalib.hpp"
#include "OS_Data.hpp"
#include "system/Log_Functions.hpp"

// PCビルド用のタッチ入力。
//
// 実機版(src/hal/Touch_Functions.hpp)はXPT2046からSPIで座標を読むが、
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
        // 離す位置(--drag)。押している間に(x,y)からここへまっすぐ動かす。--tapは押した位置のまま
        int x2 = 0;
        int y2 = 0;
    };

    constexpr int kMaxTaps = 16;

    inline Tap taps[kMaxTaps];
    inline int count = 0;
    inline int frame = 0;   // PICO_Touch::Update()が呼ばれた回数
    inline bool enabled = false;

    inline bool AddDrag(int x, int y, int x2, int y2, int at_frame, int hold){
        if(count >= kMaxTaps) return false;
        taps[count++] = Tap{at_frame, (hold > 0 ? hold : 1), x, y, x2, y2};
        enabled = true;
        return true;
    }
    inline bool Add(int x, int y, int at_frame, int hold){
        return AddDrag(x, y, x, y, at_frame, hold);
    }

    // 今のフレームで押されている座標があれば true
    inline bool Current(int& x, int& y){
        for(int i = 0; i < count; i++){
            const Tap& t = taps[i];
            if(frame >= t.frame && frame < t.frame + t.hold){
                //最初のフレームは押した位置、最後のフレームで離す位置に着く
                const int step = frame - t.frame;
                const int span = t.hold > 1 ? t.hold - 1 : 1;
                x = t.x + (t.x2 - t.x) * step / span;
                y = t.y + (t.y2 - t.y) * step / span;
                return true;
            }
        }
        return false;
    }
}

namespace PICO_Touch
{
    // 直近のタッチの「生の値」。PCにはXPT2046が無いので、マウス位置を旧来の式の逆変換で
    // 実機と同じ形の値にして返す(タッチ補正アプリが動くように)。
    // PCは補正を座標へ掛けない(マウスは元から正確なため)
    inline float raw_u = 0.0f;
    inline float raw_v = 0.0f;

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
            TouchCalib::DefaultInverse(SCREEN_WIDTH, SCREEN_HEIGHT,
                (float)OSData::touchX, (float)OSData::touchY, raw_u, raw_v);
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
