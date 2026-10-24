#include "gui/widgets/dialogs/MsgDialog.hpp"
#include "gui/core/Focus_Functions.hpp"
#include "OS_Data.hpp"

bool MsgDialog::onFocusKey(FocusKey key){
    if(key != FocusKey::Back) return false;
    return FocusFunctions::Press(this->cancel_button ? this->cancel_button : this->ok_button);
}
#include "gui/core/GFX_Functions.hpp"

void MsgDialog::render(){
    if(!needs_redraw) return;
    if(!visible) return;

    markdirty(this->getScreenRect());

    PICO_GFX::DrawDialogBackground();
    
    OSData::frame->fillRect(dlg.x, dlg.y, dlg.w, dlg.h, this->background_color);
    OSData::frame->drawRect(dlg.x, dlg.y, dlg.w, dlg.h, PICO_BLACK);

    needs_redraw = false;
}