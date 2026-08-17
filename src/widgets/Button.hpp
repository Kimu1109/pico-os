#pragma once

#include "widgets/Widget.hpp"
#include "widgets/interfaces/IFontImplementation.hpp"
#include "widgets/interfaces/IBorderColor.hpp"
#include "widgets/interfaces/ITextColor.hpp"
#include "consts.hpp"
#include "Arduino.h"

class Button :
    public Widget,
    public IFontImplementation,
    public IBorderColor,
    public ITextColor
{
    private:
        String text;

        const int TEXT_SPACING = 6;
        const int _3D_PIX_LEN = 2;

        int text_w;
        int text_h;

        bool allowTextSpacing = true;

        void calcTextSize(String text);

    public:
        using Widget::onPressStart;
        using Widget::onPressMove;

        using Widget::W;
        using Widget::H;

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
            const int text_spacing = this->allowTextSpacing ? TEXT_SPACING : 0;

            const int16_t BOX_W = this->rect.w + text_spacing + _3D_PIX_LEN + 1;
            const int16_t BOX_H = this->rect.h + text_spacing + _3D_PIX_LEN + 1;
    
            return {
                this->rect.x,
                this->rect.y,
                BOX_W,
                BOX_H
            };
        }

        bool isOpaque() const override { return true; }

        void SetFontSize(FontFn::FontSize size) override {
            this->f_size = size;
            this->calcTextSize(this->text);
            this->needsRender();
        }
        void SetBorderColor(int8_t palette_color) override {
            this->border_color = palette_color;
            this->needsRender();
        }
        void SetTextColor(int8_t palette_color) override {
            this->text_color = palette_color;
            this->needsRender();
        }

        void W(int w){
            this->rect.w = w;
        }
        void H(int h){
            this->rect.h = h;
        }

        void AllowTextSpacing(bool v){
            this->allowTextSpacing = v;
            this->needsRender();
        }
        bool AllowTextSpacing() { return this->allowTextSpacing; }
};