#include "gui/widgets/keyboards/KeyboardPanel.hpp"
#include "functions/Keyboard_Functions.hpp"
#include "functions/GFX_Functions.hpp"

void KeyboardPanel::notifyChanged(bool text_changed){
    KeyboardFunctions::OnPanelChanged(this, text_changed);
}

void KeyboardPanel::setPanelHeight(int h){
    if(h == this->l_rect.h) return;
    if(this->visible) PICO_GFX::MarkDirty(this->getScreenRect()); //縮むとき、はみ出していた部分を描き直す
    this->l_rect = {0, (int16_t)(SCREEN_HEIGHT - h), SCREEN_WIDTH, (int16_t)h};
    if(this->visible){
        this->needsRender();
        KeyboardFunctions::OnPanelResized(this);
    }
}

void KeyboardPanel::setShownSilently(bool visible){
    if(this->visible == visible) return;
    this->visible = visible;
    if(visible){
        this->resetTransientState();
        this->needsRender();
    }else{
        PICO_GFX::MarkDirty(this->getScreenRect());
    }
}

void KeyboardPanel::setVisible(bool visible){
    if(this->visible == visible) return;

    if(visible){
        this->visible = true;
        this->resetTransientState();
        this->needsRender();
        KeyboardFunctions::OnPanelShown(this);
        //targetがいればsetText()で編集対象が渡される
        if(this->target) this->target->onShow(this);
        this->notifyChanged(false);
    }else{
        this->visible = false;
        PICO_GFX::MarkDirty(this->getScreenRect());
        if(this->target) this->target->onHide(this);
        KeyboardFunctions::OnPanelHidden(this);
    }
}
