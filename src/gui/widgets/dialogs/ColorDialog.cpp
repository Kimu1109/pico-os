#include "gui/widgets/dialogs/ColorDialog.hpp"
#include "functions/GFX_Functions.hpp"
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

    this->needs_redraw = false;
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