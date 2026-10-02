#pragma once

#include <algorithm>
#include "consts.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/ScrollContainer.hpp"

// ダイアログの本文とボタンの配置を揃えるための共通部品(MsgDialog / InputDialog)。
//
// - 本文は ScrollContainer で包んだ Label。収まらないときは
//   ①小さい文字(16px)にする → ②ダイアログ自体を大きくする → ③スクロールさせる
//   の順で収める(FitDialog)。
// - ボタンは縦に積む。文字が空のボタンは作らない(nullptr)ので、その分だけ詰める(ButtonAreaHeight)。
namespace DialogLayout {
    constexpr int kMargin = 5;
    constexpr int kButtonHeight = 30;
    // スクロールするときだけ枠を出すので、その内側の余白
    constexpr int kScrollPadding = 2;

    // 大きくするときの上限。横は画面の端から少し離し、縦はステータスバーの下から画面の下端の手前まで
    constexpr int kScreenGap = 4;
    constexpr int kMaxWidth = SCREEN_WIDTH - kScreenGap * 2;
    constexpr int kTop = STATUSBAR_HEIGHT + kScreenGap;
    constexpr int kMaxHeight = SCREEN_HEIGHT - kScreenGap - kTop;

    // ボタンの文字が空(またはnullptr)なら、そのボタンは出さない
    inline bool HasText(const char* text){ return text && *text; }

    // 縦に積んだボタンの領域の高さ(上下と間の余白込み)。0個なら0
    inline int ButtonAreaHeight(int count){
        return count > 0 ? count * kButtonHeight + (count + 1) * kMargin : 0;
    }

    // 大きさ(w, h)のダイアログの置き場所。画面の中央に置くが、ステータスバーには重ねない
    // (既定の大きさのダイアログは、これまでどおり画面の中央になる)
    inline Rect Place(int w, int h){
        int y = (SCREEN_HEIGHT - h) / 2;
        if(y < kTop) y = kTop;
        return { (int16_t)((SCREEN_WIDTH - w) / 2), (int16_t)y, (int16_t)w, (int16_t)h };
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

    struct Fit {
        Rect dialog;  // ダイアログの枠(画面座標)
        int text_h;   // 本文の枠の高さ
    };

    // 本文を収めるダイアログの大きさを決め、本文の枠(box)とLabelを整える。
    //   base_w / base_h : 既定のダイアログの大きさ
    //   overhead_h      : ダイアログの高さのうち本文以外(アイコン・入力欄・ボタン・余白)が使う分
    // 本文の枠の幅はダイアログの幅 - 左右の余白。順番は
    //   1. 既定の大きさ・大きい文字(Normal)で収まればそのまま
    //   2. 既定の大きさ・小さい文字(Small)で収まればそれ
    //   3. 小さい文字のまま、ダイアログを広げる(幅を上限まで→高さを本文に合わせて上限まで)
    //   4. それでも収まらなければ上限の大きさで、本文の枠をスクロールさせる
    // 収まる間は枠もスクロールバーも出さず、本文の高さぶんだけ使う。
    // boxの位置(setX/setY)は、戻り値のダイアログの枠から呼び出し側が決める
    template<size_t N>
    Fit FitDialog(ScrollContainer* box, Label<N>* label, int base_w, int base_h, int overhead_h){
        label->setMaxHeight(0);
        label->setX(0);
        label->setY(0);

        auto measure = [label](FontFn::FontSize size, int text_w){
            label->setFontSize(size);
            label->setMaxWidth(text_w);
            return label->getH();
        };

        int dlg_w = base_w;
        int dlg_h = base_h;
        int need = -1;

        const int base_text_w = base_w - kMargin * 2;
        const int base_avail = base_h - overhead_h;
        int h = measure(FontFn::Normal, base_text_w);
        if(h <= base_avail){
            need = h;
        }else if((h = measure(FontFn::Small, base_text_w)) <= base_avail){
            need = h;
        }else{
            dlg_w = kMaxWidth;
            h = measure(FontFn::Small, kMaxWidth - kMargin * 2);
            if(overhead_h + h <= kMaxHeight){
                need = h;
                dlg_h = std::max(base_h, overhead_h + h);
            }
        }

        const int text_w = dlg_w - kMargin * 2;
        if(need >= 0){
            box->setScrollAxes(false, false);
            box->setBorderColor(box->getBackgroundColor()); //枠は見せない
            box->setSize(text_w, need);
        }else{
            dlg_h = kMaxHeight;
            need = kMaxHeight - overhead_h;
            label->setX(kScrollPadding);
            label->setY(kScrollPadding);
            label->setMaxWidth(text_w - ScrollContainer::kScrollBarWidth - kScrollPadding * 2);
            box->setScrollAxes(false, true);
            box->setBorderColor(PICO_BLACK);
            box->setSize(text_w, need);
        }
        box->refreshContentBounds();
        box->scrollToTop();
        return { Place(dlg_w, dlg_h), need };
    }
}
