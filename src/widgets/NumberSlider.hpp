#pragma once

#include "widgets/Widget.hpp"

class NumberSlider : public Widget {
    private:
        float value = 50;

        float maxValue = 100;
        float minValue = 0;

        int text_max_w;

        bool visibleNum = true;
        int decimalPlacesNum = 2;

        int8_t color = PICO_BLACK;

        void updateTextW();

    public:
        using Widget::onPressMove;
        using Widget::W;
        using Widget::H;

        NumberSlider(int16_t x, int16_t y, int16_t w){
            this->rect = {x, y, w, 21};
            this->updateTextW();
        }

        void onPressMove() override;

        void render() override;

        void Value(float value){
            this->value = min(max(value, minValue), maxValue);
            this->needsRender();
        }
        float Value() { return this->value; }

        void MinValue(float value){
            if(value > this->maxValue) return;
            this->minValue = value;
            this->updateTextW();
            this->needsRender();
        }
        float MinValue() { return this->minValue; }

        void MaxValue(float value){
            if(value < this->minValue) return;
            this->maxValue = value;
            this->updateTextW();
            this->needsRender();
        }
        float MaxValue() { return this->maxValue; }

        void Color(int8_t color){
            this->color = color;
        }
        int8_t Color(){
            return this->color;
        }

        void W(int16_t w){
            this->rect.w = w;
            this->needsRender();
        }
        void H(int16_t h){
            this->rect.h = h;
            this->needsRender();
        }

        void VisibleNum(bool visible){
            this->visibleNum = visible;
            this->needsRender();
        }
        bool VisibleNum(){
            return this->visibleNum;
        }

        void DecimalPlacesNum(int num){
            this->decimalPlacesNum = num;
        }
        int DecimalPlacesNum(){
            return this->decimalPlacesNum;
        }
};