#include "widgets/Button.hpp"

#include "functions/GFX_Functions.hpp"
#include "OS_Data.hpp"

void Button::needsRender(){
    this->needs_redraw = true;
    PICO_GFX::markDirty(this->getRect());
}

void Button::calcTextSize(String text){
    this->rect.w = OSData::frame->textWidth(text);
    this->rect.h = OSData::frame->fontHeight();
}

void Button::render() {
    if(!this->needs_redraw) return;
    if(!this->visible) return;

    //前回の描画内容の変更(削除)
    PICO_GFX::markDirty(this->prev_rect);

    //!TODO 重なってるときウィジェットの再描画

    //描画で必要な定数共
    int BOX_W = this->rect.w + TEXT_SPACING; //ボタンのボックスの横幅
    int BOX_H = this->rect.h + TEXT_SPACING; //ボタンのボックスの縦幅

    int BOX_L_X = this->rect.x; //ボタンボックスの左端のX
    int BOX_R_X = this->rect.x + this->rect.w + TEXT_SPACING; //ボタンボックスの右端のX

    int BOX_D_Y = this->rect.y + this->rect.h + TEXT_SPACING; //ボタンボックスの下のY

    //新しく描画
    PICO_GFX::markDirty(this->getRect());

    if(this->is_pressing){
        OSData::frame->drawRect(this->rect.x + _3D_PIX_LEN, this->rect.y + _3D_PIX_LEN, BOX_W, BOX_H, PICO_BLACK);
        OSData::frame->setCursor(this->rect.x + _3D_PIX_LEN + TEXT_SPACING * 0.5, this->rect.y + _3D_PIX_LEN + TEXT_SPACING * 0.5);
        OSData::frame->print(this->text);
    }else{
        //ボタンの周り
        OSData::frame->drawRect(this->rect.x, this->rect.y, BOX_W, BOX_H, PICO_BLACK);

        //ボタン立体
        OSData::frame->drawLine(BOX_L_X, BOX_D_Y, BOX_L_X + _3D_PIX_LEN, BOX_D_Y + _3D_PIX_LEN, PICO_BLACK); //左下
        OSData::frame->drawLine(BOX_R_X, BOX_D_Y, BOX_R_X + _3D_PIX_LEN, BOX_D_Y + _3D_PIX_LEN, PICO_BLACK); //右下
        OSData::frame->drawLine(BOX_R_X, this->rect.y, BOX_R_X + _3D_PIX_LEN, this->rect.y + _3D_PIX_LEN, PICO_BLACK); //右上

        //ボタン後ろ
        OSData::frame->drawFastHLine(BOX_L_X + _3D_PIX_LEN, BOX_D_Y + _3D_PIX_LEN, BOX_W, PICO_BLACK);
        OSData::frame->drawFastVLine(BOX_R_X + _3D_PIX_LEN, this->rect.y + _3D_PIX_LEN, BOX_H, PICO_BLACK);

        //テキスト
        OSData::frame->setCursor(this->rect.x + TEXT_SPACING * 0.5, this->rect.y + TEXT_SPACING * 0.5);
        OSData::frame->print(this->text);
    }

    this->prev_rect.copy(this->getRect());
    this->needs_redraw = false;
}

void Button::Visible(bool visible){
    this->visible = visible;
    this->needsRender();
}
