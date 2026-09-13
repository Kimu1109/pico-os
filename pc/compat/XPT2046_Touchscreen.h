// PCビルド用のタッチICの代替(型を満たすためだけの殻)。
//
// 実際のタッチ入力はSDLのマウスから取るので、この殻は使われない。
// PC版の pc/compat/functions/Touch_Functions.hpp がタッチ取得ごと差し替えている。
#pragma once

#include <cstdint>

struct TS_Point {
    int16_t x = 0, y = 0, z = 0;
};

class XPT2046_Touchscreen {
public:
    XPT2046_Touchscreen(int, int = -1){}
    template<typename T> bool begin(T&){ return true; }
    bool begin(){ return true; }
    void setRotation(uint8_t){}
    bool touched(){ return false; }
    TS_Point getPoint(){ return TS_Point(); }
};
