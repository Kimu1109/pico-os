#include "gui/widgets/dialogs/ColorDialog.hpp"
#include "gui/core/GFX_Functions.hpp"
#include "gui/core/Focus_Functions.hpp"
#include "OS_Data.hpp"
#include "gui/icons/icon_render.h"

void ColorDialog::render(){
    if(!this->visible) return;
    if(!this->needs_redraw) return;

    this->markdirty(this->getScreenRect());
    PICO_GFX::DrawDialogBackground();

    OSData::frame->fillRect(BASE_X, BASE_Y, DIALOG_W, DIALOG_H, this->background_color);
    OSData::frame->drawRect(BASE_X, BASE_Y, DIALOG_W, DIALOG_H, PICO_BLACK);

    for(int x = 0; x < 4; x++){
        for(int y = 0; y < 4; y++){
            const int draw_x = BASE_X + x * COLOR_W;
            const int draw_y = BASE_Y + TITLE_H + y * COLOR_H;
            const int color = getIndexToColor(x, y);
            
            OSData::frame->fillRect(draw_x, draw_y, COLOR_W, COLOR_H, getIndexToColor(x, y));
            OSData::frame->drawRect(draw_x, draw_y, COLOR_W, COLOR_H, PICO_BLACK);

            if(color == selected_color){
                IconRender::DrawIcon(IconID::CheckboxOn, IconSize::Px24, draw_x + (COLOR_W - 24) / 2, draw_y + (COLOR_H - 24) / 2, 15 - color);
            }
        }
    }

    // フォーカスの枠(キー/コントローラーで選んでいるマス)
    if(FocusFunctions::ring_visible && FocusFunctions::focused == this){
        const Rect r = this->focusRect();
        for(int k = 0; k < FocusFunctions::kRingWidth; k++){
            OSData::frame->drawRect(r.x + k, r.y + k, r.w - 2 * k, r.h - 2 * k, FocusFunctions::kRingColor);
        }
    }

    this->needs_redraw = false;
}

Rect ColorDialog::focusRect() const {
    const int c = this->selected_color < 0 ? 0 : this->selected_color;
    return {(int16_t)(BASE_X + (c % 4) * COLOR_W), (int16_t)(BASE_Y + TITLE_H + (c / 4) * COLOR_H), (int16_t)COLOR_W, (int16_t)COLOR_H};
}

bool ColorDialog::onFocusKey(FocusKey key){
    if(key == FocusKey::Activate){
        //枠のあるマス(まだ選んでいなければ最初の色)で決める
        if(this->selected_color < 0) this->selected_color = 0;
        return FocusFunctions::Press(this->button_ok);
    }
    if(key == FocusKey::Back) return FocusFunctions::Press(this->button_no);
    int x = this->selected_color < 0 ? 0 : this->selected_color % 4;
    int y = this->selected_color < 0 ? 0 : this->selected_color / 4;
    switch(key){
        case FocusKey::Left:  x--; break;
        case FocusKey::Right: x++; break;
        case FocusKey::Up:    y--; break;
        case FocusKey::Down:  y++; break;
        default: return false;
    }
    //格子の外へ出る向きは、隣(下のボタン)へフォーカスを移す
    if(this->selected_color >= 0 && (x < 0 || x >= 4 || y < 0 || y >= 4)) return false;
    if(x < 0) x = 0;
    if(x > 3) x = 3;
    if(y < 0) y = 0;
    if(y > 3) y = 3;
    this->selected_color = getIndexToColor(x, y);
    this->needsRender();
    return true;
}

void ColorDialog::causeOnPressStart(){
    Widget::causeOnPressStart();

    for(int x = 0; x < 4; x++){
        for(int y = 0; y < 4; y++){
            const int draw_x = BASE_X + x * COLOR_W;
            const int draw_y = BASE_Y + TITLE_H + y * COLOR_H;

            if(OSData::touchX >= draw_x && OSData::touchX <= draw_x + COLOR_W){
                if(OSData::touchY >= draw_y && OSData::touchY <= draw_y + COLOR_H){
                    selected_color = getIndexToColor(x, y);
                }
            }
        }
    }

    needsRender();
}