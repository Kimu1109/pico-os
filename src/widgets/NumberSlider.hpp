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

        NumberSlider(int16_t x, int16_t y, int16_t w){
            this->l_rect = {x, y, w, 21};
            this->updateTextW();
        }

        void causeOnPressMove() override;

        void render() override;

        void setValue(float value){
            this->value = min(max(value, minValue), maxValue);
            this->needsRender();
        }
        float getValue() { return this->value; }

        void setMinValue(float value){
            if(value > this->maxValue) return;
            this->minValue = value;
            this->updateTextW();
            this->needsRender();
        }
        float getMinValue() { return this->minValue; }

        void setMaxValue(float value){
            if(value < this->minValue) return;
            this->maxValue = value;
            this->updateTextW();
            this->needsRender();
        }
        float getMaxValue() { return this->maxValue; }

        void setColor(int8_t color){
            this->color = color;
        }
        int8_t getColor(){
            return this->color;
        }

        void setW(int16_t w){
            this->l_rect.w = w;
            this->needsRender();
        }
        void setH(int16_t h){
            this->l_rect.h = h;
            this->needsRender();
        }

        void setVisibleNum(bool visible){
            this->visibleNum = visible;
            this->needsRender();
        }
        bool getVisibleNum(){
            return this->visibleNum;
        }

        void setDecimalPlacesNum(int num){
            this->decimalPlacesNum = num;
        }
        int getDecimalPlacesNum(){
            return this->decimalPlacesNum;
        }
};