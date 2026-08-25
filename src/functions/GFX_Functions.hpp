#pragma once

#include "model/Rect.hpp"
#include "LovyanGFX.h"

namespace PICO_GFX {

    inline const static int COLORS[16] = {
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

    inline std::vector<Rect> dirtyRects;
    inline bool isDirtyDeactivates;

    void Setup();
    void markDirty(const Rect& rect);

    void flushDirty();

    void drawDialogBackground();
}