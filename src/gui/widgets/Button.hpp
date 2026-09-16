#pragma once

#include "gui/widgets/Widget.hpp"
#include "gui/widgets/interfaces/IFontImplementation.hpp"
#include "gui/widgets/interfaces/IBorderColor.hpp"
#include "gui/widgets/interfaces/ITextColor.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"
#include "Arduino.h"

class Button :
    public Widget,
    public IFontImplementation,
    public IBorderColor,
    public ITextColor
{
    private:
        FixedString<PICO_STR_M> text;

        const int TEXT_SPACING = 6;
        const int _3D_PIX_LEN = 2;

        int text_w;
        int text_h;

        // setW()/setH()で明示的に指定された箱の大きさ(0 = 文字の実寸に合わせる)。
        // setText()やsetFontSize()で文字を測り直した時に指定を上書きしないよう覚えておく
        // — ラベルが変わるたびに幅が伸び縮みすると、並べたボタンの位置がずれてしまうため。
        int fixed_w = 0;
        int fixed_h = 0;

        bool allowTextSpacing = true;

        void calcTextSize(const char* text);

    public:

        template<size_t N>
        Button(int x, int y, FixedString<N> text){
            this->l_rect.x = x;
            this->l_rect.y = y;
            this->calcTextSize(text.c_str());
            this->text.assign(text);
            this->needs_redraw = true;
        }
        Button(int x, int y, const char* text) : Button(x, y, FixedString<PICO_STR_M>(text)) {}
        template<size_t N>
        Button(FixedString<N> text){
            this->calcTextSize(text.c_str());
            this->text.assign(text);
            this->needs_redraw = true;
        }
        Button(const char* text) : Button(FixedString<PICO_STR_M>(text)) {}

        void causeOnPressStart() override {
            if(this->on_press_start) this->on_press_start();
            this->needsRender();
        }
        void causeOnPressEnd() override {
            if(this->on_press_end) this->on_press_end();
            this->needsRender();
        }

        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::Button; }

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
            this->calcTextSize(this->text.c_str());
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

        // ボタンの文字列を差し替える。setW()/setH()で与えた大きさは保たれる
        void setText(const char* text);
        template<size_t N>
        void setText(const FixedString<N>& text){ this->setText(text.c_str()); }

        const FixedString<PICO_STR_M>& getText() const { return this->text; }

        void setW(int w){
            this->fixed_w = w;
            this->l_rect.w = w;
        }
        void setH(int h){
            this->fixed_h = h;
            this->l_rect.h = h;
        }

        void setAllowTextSpacing(bool v){
            this->allowTextSpacing = v;
            this->needsRender();
        }
        bool getAllowTextSpacing() { return this->allowTextSpacing; }
};