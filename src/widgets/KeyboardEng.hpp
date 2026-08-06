#pragma once

#include "widgets/Widget.hpp"
#include "widgets/Label.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/UTF8_Functions.hpp"
#include "OS_Data.hpp"

struct Key {
    String str;
    String str_upper;
    int w;
    char str_size; //Small(S) or Normal(N) or Zero(Z)
};
struct KeyStrSize {
    int w;
    int h;
};

class KeyboardEng : public Widget {

    private:

        const static int key_h = 24;
        const static int key_w = 240 / (10 * 2);

        const static int keys_size = 37;

        const Key keys_num[keys_size] = {
            { "1", "[", 2, 'N' },
            { "2", "]", 2, 'N' },
            { "3", "{", 2, 'N' },
            { "4", "}", 2, 'N' },
            { "5", "#", 2, 'N' },
            { "6", "%", 2, 'N' },
            { "7", "^", 2, 'N' },
            { "8", "*", 2, 'N' },
            { "9", "+", 2, 'N' },
            { "0", "=", 2, 'N' },
            //10 * 2 = 20spaces

            { "\n", "\n", 0, 'Z' },

            { "-", "_", 2, 'N' },
            { "/", "\\", 2, 'N' },
            { ":", "|", 2, 'N' },
            { ";", "~", 2, 'N' },
            { "(", "<", 2, 'N' },
            { ")", ">", 2, 'N' },
            { "¥", "$", 2, 'N' },
            { "&", "€", 2, 'N' },
            { "@", "£", 2, 'N' },
            { "\"", "・", 2, 'N' },
            //10 * 2 = 20spaces

            { "\n", "\n", 0, 'Z' },

            { "#+=", "123", 3, 'N' },
            { ".", ".", 3, 'N' },
            { ",", ",", 3, 'N' },
            { "?", "?", 3, 'N' },
            { "!", "!", 3, 'N' },
            { "'", "'", 2, 'N' },
            { "X", "X", 3, 'N' },
            //3 * 6 + 2 = 20spaces

            { "\n", "\n", 0, 'Z' },

            { "ABC", "ABC", 3, 'S' },
            { "あいう", "あいう", 4, 'S' },
            { "space", "Space", 8, 'N' },
            { "return", "Return", 5, 'N'}
            //3 + 4 + 8 + 5 = 20spaces
        };

        const Key keys[keys_size] = {
            { "q", "Q", 2, 'N' },
            { "w", "W", 2, 'N' },
            { "e", "E", 2, 'N' },
            { "r", "R", 2, 'N' },
            { "t", "T", 2, 'N' },
            { "y", "Y", 2, 'N' },
            { "u", "U", 2, 'N' },
            { "i", "I", 2, 'N' },
            { "o", "O", 2, 'N' },
            { "p", "P", 2, 'N' },
            //10 * 2 = 20spaces

            { "\n", "\n", 0, 'Z' },

            { "\t", "\t", 1, 'Z' },
            { "a", "A", 2, 'N' },
            { "s", "S", 2, 'N' },
            { "d", "D", 2, 'N' },
            { "f", "F", 2, 'N' },
            { "g", "G", 2, 'N' },
            { "h", "H", 2, 'N' },
            { "j", "J", 2, 'N' },
            { "k", "K", 2, 'N' },
            { "l", "L", 2, 'N' },
            { "\t", "\t", 1, 'Z' },
            //9 * 2 + 2 * 1 = 20spaces

            { "\n", "\n", 0, 'Z' },

            { "↑", "↓", 3, 'N' },
            { "z", "Z", 2, 'N' },
            { "x", "X", 2, 'N' },
            { "c", "C", 2, 'N' },
            { "v", "V", 2, 'N' },
            { "b", "B", 2, 'N' },
            { "n", "N", 2, 'N' },
            { "m", "M", 2, 'N' },
            { "X", "X", 3, 'N' },
            //7 * 2 + 3 * 2 = 20spaces

            { "\n", "\n", 0, 'Z' },

            { "123", "123", 3, 'S' },
            { "あいう", "あいう", 4, 'S' },
            { "space", "Space", 8, 'N' },
            { "return", "Return", 5, 'N'}
            //3 + 4 + 8 + 5 = 20spaces
        };

        String inputs = "";
        void addInput(String str){
            inputs += str;
            input_label->Text(inputs);
        }
        void removeInput(){
            if(inputs.length() == 0) return;

            inputs = UTF8_Functions::removeLastChar(inputs);
            input_label->Text(inputs);
        }
        Key keyEnv(int index){
            if(isNumMode){ //123モード
                return keys_num[index];
            }else{ //ABCモード
                return keys[index];
            }
        }
        bool isUpperCase = false;
        bool isNumMode = false;

    public:

        Label* input_label;

        KeyboardEng(){
            this->w = SCREEN_WIDTH;
            this->h = key_h * 4;

            this->x = 0;
            this->y = SCREEN_HEIGHT - this->h;

            input_label = new Label("");
        }

        void onPressStart() override {
            if(on_press_start) on_press_start();

            int key_y = this->y;
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
                        }else{
                            addInput(isUpperCase ? key.str_upper : key.str);
                            if(!isNumMode && isUpperCase){
                                isUpperCase = false;
                                this->needs_redraw = true;
                            }
                        }
                        break;
                    }
                }

                key_x += key.w * key_w;
            }

        }

        void render() override {
            if(!this->needs_redraw) return;

            PICO_GFX::markDirty(this->x, this->y, this->w, this->h);
            OSData::frame->fillRect(this->x, this->y, this->w, this->h, PICO_BACKGROUND);
            int key_y = this->y;
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

};