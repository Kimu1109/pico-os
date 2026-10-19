#pragma once

#include "LovyanGFX.hpp"

namespace FontFn {

    enum FontSize {
        Small,
        Normal,
        Big,
        Bigger
    };

    //16px
    void SetSmall();

    //24px
    void SetNormal();

    //32px(16px * 2)
    void SetBig();

    //48px(24px * 2)
    void SetBigger();

    //24px
    void SetDefault();

    const lgfx::v1::U8g2font* GetSmall();
    const lgfx::v1::U8g2font* GetNormal();

    void SetFontSize(FontSize size);
    int GetFontSize(FontSize size);
}