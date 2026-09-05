#include "gui/widgets/dialogs/FileSelectDialog.hpp"
#include "functions/GFX_Functions.hpp"
#include "OS_Data.hpp"
#include "storage/SD_IO.hpp"

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