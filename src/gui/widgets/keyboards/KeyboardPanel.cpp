#include "gui/widgets/keyboards/KeyboardPanel.hpp"
#include "functions/Keyboard_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Clipboard_Functions.hpp"

#include <cstring>

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
        this->onPanelHidden();
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
        this->onPanelHidden();
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
            if(ev.ctrl() && !ev.alt()) return this->clipboardKey(ev);
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

bool KeyboardPanel::clipboardKey(const KeyInputFunctions::Event& ev){
    uint32_t c = ev.cp;
    if(c >= 'A' && c <= 'Z') c += 'a' - 'A';

    if(c == 'c' || c == 'x'){
        const FixedString<PICO_STR_LL> text = this->getText();
        ClipboardFunctions::Set(text.c_str(), text.length());
        if(c == 'x'){
            this->setText(FixedString<PICO_STR_LL>());
            this->notifyChanged(true);
        }
        return true;
    }
    if(c != 'v') return false;
    if(ClipboardFunctions::Empty()) return true;

    //入り切る分だけ(UTF-8の文字の途中では切らない)。1行の入力欄では改行を空白にする
    const size_t room_total = FixedString<PICO_STR_LL>::capacity();
    const size_t used = this->getText().length();
    if(used >= room_total) return true;
    size_t n = ClipboardFunctions::Length();
    if(n > room_total - used){
        n = room_total - used;
        while(n > 0 && ((unsigned char)ClipboardFunctions::Get()[n] & 0xC0) == 0x80) n--;
    }
    if(n == 0) return true;
    char buf[PICO_STR_LL];
    memcpy(buf, ClipboardFunctions::Get(), n);
    buf[n] = '\0';
    const bool single = !this->target || this->target->getIsSingleLine();
    for(size_t i = 0; i < n; i++){
        if(buf[i] == '\n' && single) buf[i] = ' ';
    }
    this->physicalPaste(buf);
    return true;
}
