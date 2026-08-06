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
        using Widget::Visible;

        Button(int x, int y, String text){
            this->x = x;
            this->y = y;
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
            this->needs_redraw = true;
        }
        void onPressEnd() override {
            if(this->on_press_end) this->on_press_end();
            this->needs_redraw = true;
        }

        void render() override;

        bool hitTest(int px, int py) override {
            int BOX_W = this->w + TEXT_SPACING;
            int BOX_H = this->h + TEXT_SPACING;

            if(is_pressing){
                return px >= this->x + _3D_PIX_LEN && px < this->x + BOX_W + _3D_PIX_LEN &&
                    py >= this->y + _3D_PIX_LEN && py < this->y + BOX_H + _3D_PIX_LEN;
            }else{
                return px >= this->x && px < this->x + BOX_W &&
                    py >= this->y && py < this->y + BOX_H;
            }
        }

        void Visible(bool visible) override;
};