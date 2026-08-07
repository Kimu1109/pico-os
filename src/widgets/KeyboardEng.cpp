#include "widgets/KeyboardEng.hpp"
#include "functions/GFX_Functions.hpp"
#include "OS_Data.hpp"

void KeyboardEng::Visible(bool visible) {
    this->visible = visible;
    this->input_label->Visible(visible);

    this->needs_redraw = true;
    PICO_GFX::markDirty(this->rect);
}


void KeyboardEng::onPressStart() {
    if(on_press_start) on_press_start();

    int key_y = this->rect.y;
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

        if(OSData::touchX >= key_x && OSData::touchX <= key_x + key.w * key_w){
            if(OSData::touchY >= key_y && OSData::touchY <= key_y + key_h){
                if(key.str == "space"){
                    addInput(" ");
                }else if(key.str == "return"){
                    addInput("\n");
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
                        PICO_GFX::markDirty(this->rect);
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

    PICO_GFX::fillBackground(this->rect);
    int key_y = this->rect.y;
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
        switch(key.str_size){
            case 'S':
            case 'N':
                OSData::frame->setFont(&lgfxJapanGothicP_16);
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
    OSData::frame->setFont(&lgfxJapanGothicP_24);

    this->needs_redraw = false;
}