#include "gui/widgets/dialogs/KeyboardEng.hpp"
#include "functions/GFX_Functions.hpp"
#include "OS_Data.hpp"

void KeyboardEng::setVisible(bool visible) {
    this->visible = visible;
    this->input_label->setVisible(visible);
    this->input_label->setMaxHeight(SCREEN_HEIGHT - 10 * 2 - this->l_rect.h);

    if(visible){
        this->inputs = *this->input_label->getText();
        this->cursor_char = this->inputs.charCount();
        if(this->target) this->target->onShow(this); //targetがいればsetText()でカーソルごと引き直される
    }else{
        if(this->target) this->target->onHide(this);
    }

    this->needs_redraw = true;
    markdirty(this->getScreenRect());
}


void KeyboardEng::causeOnPressStart() {
    Widget::causeOnPressStart();

    int key_y = SCREEN_HEIGHT - key_h * 4;
    int key_x = 0;

    char mode = this->getMode();

    for(int i = 0; i < keys_size; i++){
        Key key = keyEnv(i);

        if(strcmp(key.str, "\n") == 0){
            key_x = 0;
            key_y += key_h;
            continue;
        }
        if(strcmp(key.str, "\t") == 0){
            key_x += key.w * key_w;
            continue;
        }
        if(key.str[0] == '\0'){
            break;
        }
        switch(key.str_size){
            case 'A':
            case 'B':
            case 'C':
                if(mode != key.str_size){
                    continue;
                }
                break;
            default:
                break;
        }

        if(OSData::touchX >= key_x && OSData::touchX <= key_x + key.w * key_w){
            if(OSData::touchY >= key_y && OSData::touchY <= key_y + key_h){
                if(strcmp(key.str, "space") == 0){
                    addInput(" ");
                }else if(strcmp(key.str, "enter") == 0){
                    addInput("\n");
                }else if(strcmp(key.str, "go") == 0 || strcmp(key.str, "submit") == 0){
                    this->target->onHide(this);
                    this->setVisible(false);
                }else if(strcmp(key.str, "X") == 0){
                    removeInput();
                }else if(strcmp(key.str, "←") == 0){
                    moveCursor(-1);
                }else if(strcmp(key.str, "→") == 0){
                    moveCursor(1);
                }else if(strcmp(key.str, "↑") == 0 || strcmp(key.str, "#+=") == 0) {
                    isUpperCase = !isUpperCase;
                    this->needs_redraw = true;
                }else if(strcmp(key.str, "ABC") == 0){
                    isNumMode = false;
                    isUpperCase = false;
                    this->needs_redraw = true;
                }else if(strcmp(key.str, "123") == 0){
                    isNumMode = true;
                    isUpperCase = false;
                    this->needs_redraw = true;
                }else if(strcmp(key.str, "かな") == 0){
                    this->setVisible(false);
                    OSData::keyboard_jpn->setVisible(true);
                }else{
                    addInput(isUpperCase ? key.str_upper : key.str);
                    if(!isNumMode && isUpperCase){
                        isUpperCase = false;
                        this->needs_redraw = true;
                        markdirty(this->getScreenRect());
                    }
                }
                break;
            }
        }

        key_x += key.w * key_w;
    }

}

void KeyboardEng::render() {
    if(!this->needs_redraw) return;
    if(!this->visible) return;

    markdirty(this->getScreenRect());
    PICO_GFX::DrawDialogBackground();
    OSData::frame->fillRect(0, SCREEN_HEIGHT - key_h * 4, SCREEN_WIDTH, key_h * 4, this->background_color);

    char mode = this->getMode();

    int key_y = SCREEN_HEIGHT - key_h * 4;
    int key_x = 0;
    for(int i = 0; i < keys_size; i++){
        Key key = keyEnv(i);

        if(strcmp(key.str, "\n") == 0){
            key_x = 0;
            key_y += key_h;
            continue;
        }
        if(strcmp(key.str, "\t") == 0){
            key_x += key.w * key_w;
            continue;
        }
        if(key.str[0] == '\0'){
            break;
        }
        switch(key.str_size){
            case 'N':
                FontFn::SetSmall();
                break;
            case 'A':
            case 'B':
            case 'C':
                if(mode != key.str_size){
                    continue;
                }
                break;
            case 'Z':
            default:
                break;
        }

        int str_w = OSData::frame->textWidth(isUpperCase ? key.str_upper : key.str);
        int str_h = OSData::frame->fontHeight();
        OSData::frame->drawRect(key_x, key_y, key.w * key_w + 1, key_h + 1, PICO_BLACK);
        OSData::frame->setCursor(key_x + (key.w * key_w - str_w) / 2, key_y + (key_h - str_h) / 2);
        OSData::frame->print(isUpperCase ? key.str_upper : key.str);

        key_x += key.w * key_w;
    }
    FontFn::SetDefault();

    this->needs_redraw = false;
}