#pragma once

#include "gui/widgets/Widget.hpp"
#include "consts.hpp"

// 図形ウィジェットの1つ。l_rect(x,y,w,h)をそのまま矩形として描く、
// Icon/NumberSlider等と同じ「自分のl_rectだけで完結する」単純な部品。
//
// クラス名を"Rect"にしなかったのは、util/Rect.hppのstruct Rectと衝突するため
// (Widget.hppがそれを既にRect l_rect;として使っている)。Lua側からは
// WidgetFactory::TypeFromName()で"Rect"として見せる(LuaCanvas/"Canvas"と同じ手筋)。
class RectShape : public Widget {
    private:
        int8_t color = PICO_BLACK;
        bool filled = true;
        // filled==falseのときの枠線の太さ(内側へ重ね描きする。単位px)
        int thickness = 1;

    public:
        RectShape(int16_t x, int16_t y, int16_t w, int16_t h) {
            this->l_rect = {x, y, w, h};
        }

        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::RectShape; }

        void setW(int w) { this->l_rect.w = (int16_t)w; this->needsRender(); }
        void setH(int h) { this->l_rect.h = (int16_t)h; this->needsRender(); }

        int8_t getColor() const { return this->color; }
        void setColor(int8_t color) { this->color = color; this->needsRender(); }

        bool getFilled() const { return this->filled; }
        void setFilled(bool filled) { this->filled = filled; this->needsRender(); }

        int getThickness() const { return this->thickness; }
        void setThickness(int thickness) {
            this->thickness = thickness < 1 ? 1 : thickness;
            this->needsRender();
        }
};
