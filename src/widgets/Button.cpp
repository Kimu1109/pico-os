#include "widgets/Button.hpp"

#include "functions/GFX_Functions.hpp"
#include "OS_Data.hpp"

void Button::calcTextSize(String text){
    this->fontApply();
    this->rect.w = OSData::frame->textWidth(text);
    this->rect.h = OSData::frame->fontHeight();

    this->text_w = this->rect.w;
    this->text_h = this->rect.h;

    this->fontDefault();
}

void Button::render() {
    if(!this->needs_redraw) return;
    if(!this->visible) return;

    //前回の描画内容の変更(削除)
    if(this->prev_rect != this->rect)
        PICO_GFX::markDirty(this->prev_rect);

    //描画で必要な定数共
    const int text_spacing = this->allowTextSpacing ? TEXT_SPACING : 0;

    const int BOX_W = this->rect.w + text_spacing; //ボタンのボックスの横幅
    const int BOX_H = this->rect.h + text_spacing; //ボタンのボックスの縦幅

    const int BOX_L_X = this->rect.x; //ボタンボックスの左端のX
    const int BOX_R_X = this->rect.x + this->rect.w + text_spacing; //ボタンボックスの右端のX

    const int BOX_D_Y = this->rect.y + this->rect.h + text_spacing; //ボタンボックスの下のY

    //新しく描画
    PICO_GFX::markDirty(this->getRect());

    this->fontApply();
    this->textColorApply();
    if(this->is_pressing){
        OSData::frame->drawRect(
            this->rect.x + _3D_PIX_LEN, this->rect.y + _3D_PIX_LEN,
            BOX_W, BOX_H,
            this->border_color
        );
        OSData::frame->setCursor(
            this->rect.x + _3D_PIX_LEN + text_spacing * 0.5 + (this->rect.w - this->text_w) * 0.5,
            this->rect.y + _3D_PIX_LEN + text_spacing * 0.5 + (this->rect.h - this->text_h) * 0.5
        );
        OSData::frame->print(this->text);
    }else{
        //ボタンの周り
        OSData::frame->drawRect(this->rect.x, this->rect.y, BOX_W, BOX_H, this->border_color);

        //ボタン立体
        OSData::frame->drawLine(BOX_L_X, BOX_D_Y, BOX_L_X + _3D_PIX_LEN, BOX_D_Y + _3D_PIX_LEN, this->border_color); //左下
        OSData::frame->drawLine(BOX_R_X, BOX_D_Y, BOX_R_X + _3D_PIX_LEN, BOX_D_Y + _3D_PIX_LEN, this->border_color); //右下
        OSData::frame->drawLine(BOX_R_X, this->rect.y, BOX_R_X + _3D_PIX_LEN, this->rect.y + _3D_PIX_LEN, this->border_color); //右上

        //ボタン後ろ
        OSData::frame->drawFastHLine(BOX_L_X + _3D_PIX_LEN, BOX_D_Y + _3D_PIX_LEN, BOX_W, this->border_color);
        OSData::frame->drawFastVLine(BOX_R_X + _3D_PIX_LEN, this->rect.y + _3D_PIX_LEN, BOX_H, this->border_color);

        //テキスト
        OSData::frame->setCursor(
            this->rect.x + text_spacing * 0.5 + (this->rect.w - this->text_w) * 0.5,
            this->rect.y + text_spacing * 0.5 + (this->rect.h - this->text_h) * 0.5
        );
        OSData::frame->print(this->text);
    }
    this->textColorDefault();
    this->fontDefault();

    this->prev_rect.copy(this->getRect());
    this->needs_redraw = false;
}
