#pragma once

#include "Arduino.h"
#include "consts.hpp"

template<size_t N> class Label;

class ITextColor {
    protected:
        int8_t text_color = PICO_FORECOLOR;

        void textColorApply();
        void textColorDefault();

    public:
        int8_t getTextColor(){
            return this->text_color;
        }
        virtual void setTextColor(int8_t palette_color);

        // IFontImplementation.hppのfriend宣言と同じ理由
        // (Label<N>::DrawPlain()が別特殊化Label<PICO_STR_LL>のprotectedメンバへアクセスするため)。
        template<size_t N> friend class Label;
};