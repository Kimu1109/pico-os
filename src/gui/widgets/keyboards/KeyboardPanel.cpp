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

void KeyboardPanel::physicalEnter(){
    if(!this->target || this->target->getIsSingleLine()){
        this->submit();
        return;
    }
    this->physicalInsert("\n");
}

bool KeyboardPanel::onPhysicalKey(const KeyInputFunctions::Event& ev){
    using KeyInputFunctions::Key;
    if(!this->visible) return false;

    switch(ev.key){
        case Key::Char: {
            if(!ev.isPlainChar()) return false;
            if(!this->acceptsPhysicalChar(ev.cp)) return true; //断った打鍵は他へ回さない
            char buf[5];
            if(KeyInputFunctions::EncodeUtf8(ev.cp, buf) == 0) return true;
            this->physicalInsert(buf);
            return true;
        }
        case Key::Enter:
            if(ev.ctrl()) this->submit();
            else this->physicalEnter();
            return true;
        case Key::Escape:
            this->submit();
            return true;
        case Key::Backspace:
            this->physicalBackspace();
            return true;
        case Key::Delete: {
            const size_t len = this->getText().length();
            if(this->getCursorByteOffset() >= len){
                if(this->target) this->target->onDeleteAtEnd(this);
                return true;
            }
            //末尾でなければ必ず1文字右へ動けるので、動いてから前の1文字を消す
            this->physicalMove(1);
            this->physicalBackspace();
            return true;
        }
        case Key::Left:
            this->physicalMove(-1);
            return true;
        case Key::Right:
            this->physicalMove(1);
            return true;
        case Key::Home:
            this->setCursorByteOffset(0);
            return true;
        case Key::End:
            this->setCursorByteOffset(this->getText().length());
            return true;
        default:
            return false;
    }
}
