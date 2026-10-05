#pragma once

#include "gui/widgets/Widget.hpp"
#include "consts.hpp"

// 進捗バー(汎用)。l_rect(x,y,w,h)に枠線を引き、min〜maxの中のvalueの割合だけ左から塗る。
// NumberSlider(つまみで操作する入力部品)と違い、タッチには反応しない表示専用の部品。
// 文字は描かない(割合を文字で出したい場合はLabelを重ねる)。
class ProgressBar : public Widget {
    private:
        float value = 0.0f;
        float min_value = 0.0f;
        float max_value = 100.0f;
        int8_t color = PICO_BLACK;
        int8_t border = PICO_BLACK;

    public:
        ProgressBar(int16_t x, int16_t y, int16_t w, int16_t h) {
            this->l_rect = {x, y, w, h};
            this->hit_transparent = true;
        }

        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::ProgressBar; }
        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::OPAQUE; }

        void setW(int w) { this->l_rect.w = (int16_t)w; this->needsRender(); }
        void setH(int h) { this->l_rect.h = (int16_t)h; this->needsRender(); }

        float getValue() const { return this->value; }
        void setValue(float v);
        float getMinValue() const { return this->min_value; }
        void setMinValue(float v);
        float getMaxValue() const { return this->max_value; }
        void setMaxValue(float v);

        int8_t getColor() const { return this->color; }
        void setColor(int8_t c) { this->color = c; this->needsRender(); }
        int8_t getBorderColor() const { return this->border; }
        void setBorderColor(int8_t c) { this->border = c; this->needsRender(); }
};
