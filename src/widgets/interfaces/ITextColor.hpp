#pragma once

#include "Arduino.h"
#include "consts.hpp"

class ITextColor {
    protected:
        int8_t text_color = PICO_FORECOLOR;

        void textColorApply();
        void textColorDefault();

    public:
        int8_t GetTextColor(){
            return this->text_color;
        }
        virtual void SetTextColor(int8_t palette_color);
};