#pragma once

#include "config/LGFX_Config.hpp"
#include <LovyanGFX.h>
#include <SdFat.h>

// キーボードは Widget のポインタとして持つだけなので前方宣言で足りる
// (Widget.hpp を読むと、どこからでも使う土台の段が UI の段へ依存してしまう)
class Widget;

namespace OSData {
    //タッチ系統
    inline int touchX = 0;
    inline int touchY = 0;
    inline int touchZ = 0;
    inline bool isTouched = false;
    inline bool isTouchStart = false;
    inline bool isTouchEnd = false;
    inline bool isTouchMove = false;

    //グラフィック系統
    inline LGFX* lcd = new LGFX();
    inline LGFX_Sprite* frame;

    //SD系統
    inline bool SD_usable = false;
    inline SdFat SD;

    //キーボード
    inline Widget* keyboard_jpn;
    inline Widget* keyboard_eng;
    inline Widget* keyboard_num;
}