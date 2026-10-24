#include "gui/widgets/dialogs/InputDialog.hpp"
#include "gui/core/GFX_Functions.hpp"
#include "gui/core/Focus_Functions.hpp"
#include "OS_Data.hpp"

bool InputDialog::onFocusKey(FocusKey key){
    if(key == FocusKey::Submit) return FocusFunctions::Press(this->submit_button);
    if(key == FocusKey::Back) return FocusFunctions::Press(this->cancel_button);
    return false;
}

void InputDialog::render(){
    if(!needs_redraw) return;
    if(!visible) return;

    markdirty(this->getScreenRect());
    PICO_GFX::DrawDialogBackground();

    OSData::frame->fillRect(dlg.x, dlg.y, dlg.w, dlg.h, this->background_color);
    OSData::frame->drawRect(dlg.x, dlg.y, dlg.w, dlg.h, PICO_BLACK);

    needs_redraw = false;
}