#include "gui/widgets/keyboards/KeyboardEng.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Keyboard_Functions.hpp"
#include "OS_Data.hpp"

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
                    this->submit();
                    return;
                }else if(strcmp(key.str, "X") == 0){
                    removeInput();
                }else if(strcmp(key.str, "←") == 0){
                    moveCursor(-1);
                }else if(strcmp(key.str, "→") == 0){
                    moveCursor(1);
                }else if(strcmp(key.str, "↑") == 0 || strcmp(key.str, "#+=") == 0) {
                    isUpperCase = !isUpperCase;
                    this->needsRender();
                }else if(strcmp(key.str, "abc") == 0){
                    isNumMode = false;
                    isUpperCase = false;
                    this->needsRender();
                }else if(strcmp(key.str, "123") == 0){
                    isNumMode = true;
                    isUpperCase = false;
                    this->needsRender();
                }else if(strcmp(key.str, "かな") == 0){
                    KeyboardFunctions::SwitchPanel(this, static_cast<KeyboardPanel*>(OSData::keyboard_jpn));
                    return;
                }else{
                    addInput(isUpperCase ? key.str_upper : key.str);
                    if(!isNumMode && isUpperCase){
                        isUpperCase = false;
                        this->needsRender();
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