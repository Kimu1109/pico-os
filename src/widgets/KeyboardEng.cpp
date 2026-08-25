#include "widgets/KeyboardEng.hpp"
#include "functions/GFX_Functions.hpp"
#include "OS_Data.hpp"

void KeyboardEng::Visible(bool visible) {
    this->visible = visible;
    this->input_label->Visible(visible);
    this->input_label->MaxHeight(SCREEN_HEIGHT - 10 * 2 - this->l_rect.h);

    if(visible){
        this->inputs = this->input_label->Text();
        if(this->target) this->target->onShow(this);
    }else{
        if(this->target) this->target->onHide(this);
    }

    this->needs_redraw = true;
    markdirty(this->getScreenRect());
}


void KeyboardEng::onPressStart() {
    if(on_press_start) on_press_start();

    int key_y = SCREEN_HEIGHT - key_h * 4;
    int key_x = 0;

    char mode = this->getMode();

    for(int i = 0; i < keys_size; i++){
        Key key = keyEnv(i);

        if(key.str == "\n"){
            key_x = 0;
            key_y += key_h;
            continue;
        }
        if(key.str == "\t"){
            key_x += key.w * key_w;
            continue;
        }
        if(key.str == "\0"){
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
                if(key.str == "space"){
                    addInput(" ");
                }else if(key.str == "return"){
                    addInput("\n");
                }else if(key.str == "go" || key.str == "submit"){
                    this->target->onHide(this);
                    this->Visible(false);
                }else if(key.str == "X"){
                    removeInput();
                }else if(key.str == "↑" || key.str == "#+=") {
                    isUpperCase = !isUpperCase;
                    this->needs_redraw = true;
                }else if(key.str == "ABC"){
                    isNumMode = false;
                    isUpperCase = false;
                    this->needs_redraw = true;
                }else if(key.str == "123"){
                    isNumMode = true;
                    isUpperCase = false;
                    this->needs_redraw = true;
                }else if(key.str == "あいう"){
                    this->Visible(false);
                    OSData::keyboard_jpn->Visible(true);
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
    PICO_GFX::drawDialogBackground();
    OSData::frame->fillRect(0, SCREEN_HEIGHT - key_h * 4, SCREEN_WIDTH, key_h * 4, this->background_color);

    char mode = this->getMode();

    int key_y = SCREEN_HEIGHT - key_h * 4;
    int key_x = 0;
    for(int i = 0; i < keys_size; i++){
        Key key = keyEnv(i);

        if(key.str == "\n"){
            key_x = 0;
            key_y += key_h;
            continue;
        }
        if(key.str == "\t"){
            key_x += key.w * key_w;
            continue;
        }
        if(key.str == "\0"){
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