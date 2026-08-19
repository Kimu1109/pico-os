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
    void markDirtyXYWH(int16_t x, int16_t y, int16_t w, int16_t h);
    void markDirty(const Rect& rect);

    void flushDirty();

    void fillBackgroundXYWH(int16_t x, int16_t y, int16_t w, int16_t h);
    void fillBackground(const Rect& rect);
    void fillBackgroundNoDirty(const Rect& rect);
    void fillBackgroundNoDirtyCC(const Rect& rect, int8_t color);

    void fillBorderRect(int16_t x, int16_t y, int16_t w, int16_t h, int background, int border);

    void drawDialogBackground();
}