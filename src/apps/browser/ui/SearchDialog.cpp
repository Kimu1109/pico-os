#include "apps/browser/ui/SearchDialog.hpp"
#include "gui/core/GFX_Functions.hpp"
#include "gui/core/Focus_Functions.hpp"
#include "OS_Data.hpp"

bool SearchDialog::onFocusKey(FocusKey key){
    if(key == FocusKey::Back) return FocusFunctions::Press(this->close_button);
    return false;
}

void SearchDialog::render(){
    if(!needs_redraw) return;
    if(!visible) return;

    markdirty(this->getScreenRect());
    PICO_GFX::DrawDialogBackground();

    OSData::frame->fillRect(BASE_X, BASE_Y, DIALOG_WIDTH, DIALOG_HEIGHT, this->background_color);
    OSData::frame->drawRect(BASE_X, BASE_Y, DIALOG_WIDTH, DIALOG_HEIGHT, PICO_BLACK);

    needs_redraw = false;
}
