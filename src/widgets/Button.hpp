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
        void needsRender();

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
    
        bool hitTest(int px, int py) override {
            // getRect() 全体をタッチ有効領域とする
            const Rect r = this->getRect();
            return px >= r.x && px < r.x + r.w &&
                py >= r.y && py < r.y + r.h;
        }

        void Visible(bool visible) override;
};