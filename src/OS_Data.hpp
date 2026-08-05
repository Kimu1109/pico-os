#pragma once

#include "config/LGFX_Config.hpp"
#include <LovyanGFX.h>
#include <SdFat.h>

namespace OSData {
    //タッチ系統
    int touchX = 0;
    int touchY = 0;
    int touchZ = 0;
    bool isTouched = false;
    bool isTouchStart = false;
    bool isTouchEnd = false;
    bool isTouchMove = false;

    //グラフィック系統
    LGFX* lcd = new LGFX();
    LGFX_Sprite* frame;

    //SD系統
    bool SD_usable = false;
    SdFat SD;
}