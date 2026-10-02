#pragma once

#include "consts.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/ScrollContainer.hpp"

// ダイアログの本文とボタンの配置を揃えるための共通部品(MsgDialog / InputDialog)。
//
// - 本文は ScrollContainer で包んだ Label。まず大きい文字(24px)で置き、枠に収まらなければ
//   小さい文字(16px)にし、それでも収まらなければスクロールできるようにする(FitText)。
// - ボタンは縦に積む。文字が空のボタンは作らない(nullptr)ので、その分だけ詰める(ButtonAreaHeight)。
namespace DialogLayout {
    constexpr int kMargin = 5;
    constexpr int kButtonHeight = 30;
    // スクロールするときだけ枠を出すので、その内側の余白
    constexpr int kScrollPadding = 2;

    // ボタンの文字が空(またはnullptr)なら、そのボタンは出さない
    inline bool HasText(const char* text){ return text && *text; }

    // 縦に積んだボタンの領域の高さ(上下と間の余白込み)。0個なら0
    inline int ButtonAreaHeight(int count){
        return count > 0 ? count * kButtonHeight + (count + 1) * kMargin : 0;
    }

    // buttons(nullptrは飛ばす)を、ダイアログの下端(bottom)から上へ詰めて積む。
    // 並びは配列の先頭が上
    inline void StackButtons(Button* const* buttons, int n, int x, int w, int bottom){
        int count = 0;
        for(int i = 0; i < n; i++) if(buttons[i]) count++;
        int y = bottom - ButtonAreaHeight(count) + kMargin;
        for(int i = 0; i < n; i++){
            Button* b = buttons[i];
            if(!b) continue;
            //Buttonの箱は立体ぶん右下へ出るので、これまでのダイアログと同じく2px左へ寄せる
            b->setX(x - 2);
            b->setY(y);
            b->setW(w);
            b->setAllowTextSpacing(false);
            y += kButtonHeight + kMargin;
        }
    }

    // 本文を(x, y, w, max_h)の枠へ収める。戻り値は実際に使った高さ。
    //   1. 大きい文字(Normal)で収まればそのまま
    //   2. 小さい文字(Small)で収まればそれ
    //   3. それでも収まらなければ max_h いっぱいの枠にしてスクロールさせる
    // 収まる間は枠もスクロールバーも出さず、本文の高さぶんだけ使う
    template<size_t N>
    int FitText(ScrollContainer* box, Label<N>* label, int x, int y, int w, int max_h){
        box->setX(x);
        box->setY(y);
        label->setMaxHeight(0);
        label->setX(0);
        label->setY(0);
        label->setMaxWidth(w);

        int used = -1;
        label->setFontSize(FontFn::Normal);
        if(label->getH() <= max_h){
            used = label->getH();
        }else{
            label->setFontSize(FontFn::Small);
            if(label->getH() <= max_h) used = label->getH();
        }

        if(used >= 0){
            box->setScrollAxes(false, false);
            box->setBorderColor(box->getBackgroundColor()); //枠は見せない
            box->setSize(w, used);
        }else{
            label->setX(kScrollPadding);
            label->setY(kScrollPadding);
            label->setMaxWidth(w - ScrollContainer::kScrollBarWidth - kScrollPadding * 2);
            box->setScrollAxes(false, true);
            box->setBorderColor(PICO_BLACK);
            box->setSize(w, max_h);
            used = max_h;
        }
        box->refreshContentBounds();
        box->scrollToTop();
        return used;
    }
}
