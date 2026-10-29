#pragma once

#include "LovyanGFX.hpp"

namespace FontFn {

    enum FontSize {
        Small,
        Normal,
        Big,
        Bigger,
        // 本文(読む所の文字)。文字の大きさの設定(SetLargeText())に従って16px/20pxになる。
        // 部品・ボタン・タブ等の文字は従わせないので、読む所だけがこれを選ぶ
        // (docs/design/gui.md「文字の大きさの設定」)。Luaからは選べない(FromInt())
        Text
    };

    //16px
    void SetSmall();

    //24px
    void SetNormal();

    //32px(16px * 2)
    void SetBig();

    //48px(24px * 2)
    void SetBigger();

    //16px または 20px(SetLargeText())
    void SetText();

    //24px
    void SetDefault();

    const lgfx::v1::U8g2font* GetSmall();
    const lgfx::v1::U8g2font* GetNormal();
    // 本文の文字(今の設定のもの)。倍率は常に1
    const lgfx::v1::U8g2font* GetText();

    void SetFontSize(FontSize size);
    int GetFontSize(FontSize size);

    // 本文(FontSize::Text)を大きくするか。display.cfg の text-size(DisplayFunctions が読む)。
    // 次に組み直す(シーンに入り直す・文書を開き直す)ときから効く
    void SetLargeText(bool large);
    bool IsLargeText();

    // 外(Lua等)から来た整数を FontSize にする。Small〜Bigger 以外は Small。
    // Luaのアプリはピクセルで配置しているので、本文の設定(Text)には従わせない
    FontSize FromInt(int value);
}
