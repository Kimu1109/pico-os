#include "widgets/Button.hpp"

#include "functions/GFX_Functions.hpp"
#include "OS_Data.hpp"

void Button::calcTextSize(String text){
    this->w = OSData::frame->textWidth(text);
    this->h = OSData::frame->fontHeight();
}

void Button::render() {
    if(!this->needs_redraw) return;
    if(!this->visible) return;

    //前回の描画内容の消去
    OSData::frame->fillRect(this->prev_x, this->prev_y, this->prev_w, this->prev_h, PICO_BACKGROUND);
    PICO_GFX::markDirty(this->prev_x, this->prev_y, this->prev_w, this->prev_h);
    //!TODO 重なってるときウィジェットの再描画

    //描画で必要な定数共
    int BOX_W = this->w + TEXT_SPACING; //ボタンのボックスの横幅
    int BOX_H = this->h + TEXT_SPACING; //ボタンのボックスの縦幅

    int BOX_L_X = this->x; //ボタンボックスの左端のX
    int BOX_R_X = this->x + this->w + TEXT_SPACING; //ボタンボックスの右端のX

    int BOX_D_Y = this->y + this->h + TEXT_SPACING; //ボタンボックスの下のY

    //新しく描画
    OSData::frame->fillRect(this->x, this->y, BOX_W + _3D_PIX_LEN, BOX_H + _3D_PIX_LEN, PICO_BACKGROUND);
    PICO_GFX::markDirty(this->x, this->y, BOX_W + _3D_PIX_LEN, BOX_H + _3D_PIX_LEN);
    if(this->is_pressing){
        OSData::frame->drawRect(this->x + _3D_PIX_LEN, this->y + _3D_PIX_LEN, BOX_W, BOX_H, PICO_BLACK);
        OSData::frame->setCursor(this->x + _3D_PIX_LEN + TEXT_SPACING * 0.5, this->y + _3D_PIX_LEN + TEXT_SPACING * 0.5);
        OSData::frame->print(this->text);
    }else{
        //ボタンの周り
        OSData::frame->drawRect(this->x, this->y, BOX_W, BOX_H, PICO_BLACK);

        //ボタン立体
        OSData::frame->drawLine(BOX_L_X, BOX_D_Y, BOX_L_X + _3D_PIX_LEN, BOX_D_Y + _3D_PIX_LEN, PICO_BLACK); //左下
        OSData::frame->drawLine(BOX_R_X, BOX_D_Y, BOX_R_X + _3D_PIX_LEN, BOX_D_Y + _3D_PIX_LEN, PICO_BLACK); //右下
        OSData::frame->drawLine(BOX_R_X, this->y, BOX_R_X + _3D_PIX_LEN, this->y + _3D_PIX_LEN, PICO_BLACK); //右上

        //ボタン後ろ
        OSData::frame->drawFastHLine(BOX_L_X + _3D_PIX_LEN, BOX_D_Y + _3D_PIX_LEN, BOX_W, PICO_BLACK);
        OSData::frame->drawFastVLine(BOX_R_X + _3D_PIX_LEN, this->y + _3D_PIX_LEN, BOX_H, PICO_BLACK);

        //テキスト
        OSData::frame->setCursor(this->x + TEXT_SPACING * 0.5, this->y + TEXT_SPACING * 0.5);
        OSData::frame->print(this->text);
    }

    this->needs_redraw = false;
}

void Button::Visible(bool visible){
    this->visible = visible;
    if(visible){
        this->needs_redraw = true;
    }else{
        OSData::frame->fillRect(this->x, this->y, this->w, this->h, PICO_BACKGROUND);
        PICO_GFX::markDirty(this->x, this->y, this->w, this->h);
    }
}