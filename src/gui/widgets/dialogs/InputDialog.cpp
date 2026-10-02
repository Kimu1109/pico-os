#include "gui/widgets/dialogs/InputDialog.hpp"
#include "functions/GFX_Functions.hpp"
#include "OS_Data.hpp"

void InputDialog::render(){
    if(!needs_redraw) return;
    if(!visible) return;

    markdirty(this->getScreenRect());
    PICO_GFX::DrawDialogBackground();

    OSData::frame->fillRect(dlg.x, dlg.y, dlg.w, dlg.h, this->background_color);
    OSData::frame->drawRect(dlg.x, dlg.y, dlg.w, dlg.h, PICO_BLACK);

    needs_redraw = false;
}