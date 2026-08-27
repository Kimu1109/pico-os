#pragma once

#include "gui/widgets/Widget.hpp"
#include "gui/widgets/interfaces/IFontImplementation.hpp"
#include "gui/widgets/interfaces/IBorderColor.hpp"
#include "gui/widgets/interfaces/ITextColor.hpp"
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

        Button(int x, int y, String text){
            this->l_rect.x = x;
            this->l_rect.y = y;
            this->calcTextSize(text);
            this->text = text;
            this->needs_redraw = true;
        }
        Button(String text){
            this->calcTextSize(text);
            this->text = text;
            this->needs_redraw = true;
        }

        void causeOnPressStart() override {
            if(this->on_press_start) this->on_press_start();
            this->needsRender();
        }
        void causeOnPressEnd() override {
            if(this->on_press_end) this->on_press_end();
            this->needsRender();
        }

        void render() override;

        Rect getLocalRect() const override { 
            const int text_spacing = this->allowTextSpacing ? TEXT_SPACING : 0;

            const int16_t BOX_W = this->l_rect.w + text_spacing + _3D_PIX_LEN + 1;
            const int16_t BOX_H = this->l_rect.h + text_spacing + _3D_PIX_LEN + 1;
    
            return {
                this->l_rect.x,
                this->l_rect.y,
                BOX_W,
                BOX_H
            };
        }

        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::OPAQUE; }

        void setFontSize(FontFn::FontSize size) override {
            this->f_size = size;
            this->calcTextSize(this->text);
            this->needsRender();
        }
        void setBorderColor(int8_t palette_color) override {
            this->border_color = palette_color;
            this->needsRender();
        }
        void setTextColor(int8_t palette_color) override {
            this->text_color = palette_color;
            this->needsRender();
        }

        void setW(int w){
            this->l_rect.w = w;
        }
        void setH(int h){
            this->l_rect.h = h;
        }

        void setAllowTextSpacing(bool v){
            this->allowTextSpacing = v;
            this->needsRender();
        }
        bool getAllowTextSpacing() { return this->allowTextSpacing; }
};