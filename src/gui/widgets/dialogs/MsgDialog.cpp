#include "gui/widgets/dialogs/MsgDialog.hpp"
#include "OS_Data.hpp"
#include "functions/GFX_Functions.hpp"

void MsgDialog::render(){
    if(!needs_redraw) return;
    if(!visible) return;

    markdirty(this->getScreenRect());

    PICO_GFX::DrawDialogBackground();
    
    OSData::frame->fillRect(BASE_X, BASE_Y, DIALOG_WIDTH, DIALOG_HEIGHT, this->background_color);
    OSData::frame->drawRect(BASE_X, BASE_Y, DIALOG_WIDTH, DIALOG_HEIGHT, PICO_BLACK);

    needs_redraw = false;
}