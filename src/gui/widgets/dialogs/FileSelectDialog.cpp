#include "gui/widgets/dialogs/FileSelectDialog.hpp"
#include "gui/core/GFX_Functions.hpp"
#include "gui/core/Focus_Functions.hpp"
#include "OS_Data.hpp"
#include "storage/SD_IO.hpp"

bool FileSelectDialog::onFocusKey(FocusKey key){
    if(key == FocusKey::Back) return FocusFunctions::Press(this->button_no);
    return false;
}

void FileSelectDialog::render(){
    if(!this->visible) return;
    if(!this->needs_redraw) return;

    this->markdirty(this->getScreenRect());
    PICO_GFX::DrawDialogBackground();

    OSData::frame->fillRect(BASE_X, BASE_Y, DIALOG_W, DIALOG_H, this->background_color);
    OSData::frame->drawRect(BASE_X, BASE_Y, DIALOG_W, DIALOG_H, PICO_BLACK);

    this->needs_redraw = false;
}

const char* FileSelectDialog::getSelectedPath(){
    return this->explorer->getSelectedPath();
}