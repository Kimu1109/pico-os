#pragma once

#include "widgets/Widget.hpp"
#include <LovyanGFX.hpp>

class CanvasPixel : public Widget {
    private:
        LGFX_Sprite* sp;

        int16_t sx;
        int16_t sy;

    public:
        using Widget::onPressStart;
        using Widget::onPressMove;
        using Widget::onPressEnd;

        CanvasPixel(int16_t x, int16_t y, int16_t w, int16_t h);

        void render() override;
        
        void onPressStart() override;
        void onPressMove() override;
        void onPressEnd() override;

        void CanvasClear();
};