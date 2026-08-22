#pragma once

#include "widgets/Widget.hpp"
#include <LovyanGFX.hpp>

namespace Canvas {
    enum Mode {
        Line,
        Rect,
        Ellipse,
        Arrow
    };
}

class CanvasRaster : public Widget {
    private:
        Canvas::Mode mode = Canvas::Mode::Line;

        LGFX_Sprite* sp;

        int16_t sx;
        int16_t sy;

        int8_t brush_color = PICO_BLACK;
        float brush_radius = 1.5;

        int16_t relX(int16_t x){
            return x - this->getScreenX();
        }
        int16_t relY(int16_t y){
            return y - this->getScreenY();
        }
        int16_t absX(int16_t x){
            return x + this->getScreenX();
        }
        int16_t absY(int16_t y){
            return y + this->getScreenY();
        }

    public:
    
        using Widget::onPressStart;
        using Widget::onPressMove;
        using Widget::onPressEnd;

        CanvasRaster(int16_t x, int16_t y, int16_t w, int16_t h);

        void render() override;
        
        void onPressStart() override;
        void onPressMove() override;
        void onPressEnd() override;

        void CanvasClear();

        void Brush(int8_t brush_color, float brush_radius){
            this->brush_color = brush_color;
            this->brush_radius = brush_radius;
        }

        void BrushColor(int8_t brush_color){
            this->brush_color = brush_color;
        }
        int8_t BrushColor() { return this->brush_color; }

        void BrushRadius(float radius){
            this->brush_radius = radius;
        }
        float BrushRadius() { return this->brush_radius; }

        void Mode(Canvas::Mode mode){
            this->mode = mode;
        }
        Canvas::Mode Mode(){
            return this->mode;
        }

        void drawArrow(LGFX_Sprite *canvas, int x0, int y0, int x1, int y1);
};