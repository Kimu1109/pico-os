#include "gui/widgets/dialogs/FileSaveDialog.hpp"
#include "functions/GFX_Functions.hpp"
#include "OS_Data.hpp"
#include "storage/SD_IO.hpp"

void FileSaveDialog::render(){
    if(!this->visible) return;
    if(!this->needs_redraw) return;

    this->markdirty(this->getScreenRect());
    PICO_GFX::DrawDialogBackground();

    OSData::frame->fillRect(BASE_X, BASE_Y, DIALOG_W, DIALOG_H, this->background_color);
    OSData::frame->drawRect(BASE_X, BASE_Y, DIALOG_W, DIALOG_H, PICO_BLACK);

    this->needs_redraw = false;
}

const char* FileSaveDialog::getSavePath(){
    static char path[256];
    PICO_IO::join(path, this->explorer->getCurrentFolderPath(), this->textbox_filename->getText()->c_str());
    return path;
}