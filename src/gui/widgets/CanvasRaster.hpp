#pragma once

#include "gui/widgets/Widget.hpp"
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

        CanvasRaster(int16_t x, int16_t y, int16_t w, int16_t h);

        void render() override;
        
        void causeOnPressStart() override;
        void causeOnPressMove() override;
        void causeOnPressEnd() override;

        void canvasClear();

        void setBrush(int8_t brush_color, float brush_radius){
            this->brush_color = brush_color;
            this->brush_radius = brush_radius;
        }

        void setBrushColor(int8_t brush_color){
            this->brush_color = brush_color;
        }
        int8_t getBrushColor() { return this->brush_color; }

        void setBrushRadius(float radius){
            this->brush_radius = radius;
        }
        float getBrushRadius() { return this->brush_radius; }

        void setMode(Canvas::Mode mode){
            this->mode = mode;
        }
        Canvas::Mode getMode(){
            return this->mode;
        }

        void drawArrow(LGFX_Sprite *canvas, int x0, int y0, int x1, int y1);
};