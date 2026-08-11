#pragma once

#include "widgets/Widget.hpp"
#include "consts.hpp"
#include "Arduino.h"

class Button : public Widget {
    private:
        String text;

        const int TEXT_SPACING = 6;
        const int _3D_PIX_LEN = 2;

        void calcTextSize(String text);

    public:
        using Widget::onPressStart;
        using Widget::onPressMove;

        Button(int x, int y, String text){
            this->rect.x = x;
            this->rect.y = y;
            this->calcTextSize(text);
            this->text = text;
            this->needs_redraw = true;
        }
        Button(String text){
            this->calcTextSize(text);
            this->text = text;
            this->needs_redraw = true;
        }

        void onPressStart() override {
            if(this->on_press_start) this->on_press_start();
            this->needsRender();
        }
        void onPressEnd() override {
            if(this->on_press_end) this->on_press_end();
            this->needsRender();
        }

        void render() override;

        Rect getRect() const override { 
            const int16_t BOX_W = this->rect.w + TEXT_SPACING + _3D_PIX_LEN + 1;
            const int16_t BOX_H = this->rect.h + TEXT_SPACING + _3D_PIX_LEN + 1;
    
            return {
                this->rect.x,
                this->rect.y,
                BOX_W,
                BOX_H
            };
        }

        bool isOpaque() const override { return true; }
};