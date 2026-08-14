#pragma once

#include "Arduino.h"
#include "consts.hpp"

class IBorderColor {
    protected:
        int8_t border_color = PICO_BLACK;

    public:
        int8_t GetBorderColor(){
            return this->border_color;
        }
        virtual void SetBorderColor(int8_t palette_color);
};