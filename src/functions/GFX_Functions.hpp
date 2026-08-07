#pragma once

#include "model/Rect.hpp"

namespace PICO_GFX {

    inline std::vector<Rect> dirtyRects;

    void Setup();
    void markDirtyXYWH(int16_t x, int16_t y, int16_t w, int16_t h);
    void markDirty(const Rect& rect);

    void flushDirty();

    void fillBackgroundXYWH(int16_t x, int16_t y, int16_t w, int16_t h);
    void fillBackground(const Rect& rect);
}