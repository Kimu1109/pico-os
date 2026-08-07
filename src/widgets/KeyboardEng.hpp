#pragma once

#include "widgets/Widget.hpp"
#include "widgets/Label.hpp"
#include "functions/UTF8_Functions.hpp"

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
    protected:
        std::vector<Widget*> children_;

    private:

        const static int key_h = 28;
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

        using Widget::onPressStart;
        using Widget::onPressEnd;
        using Widget::Visible;

        Label* input_label;

        void Visible(bool visible) override;

        KeyboardEng(){
            this->rect.w = SCREEN_WIDTH;
            this->rect.h = key_h * 4;

            this->rect.x = 0;
            this->rect.y = SCREEN_HEIGHT - this->rect.h;

            input_label = new Label("");
            input_label->MaxWidth(SCREEN_WIDTH);
            input_label->Visible(false);
            this->visible = false;

            children_.push_back(input_label);
        }

        void onPressStart() override;
        void render() override;

        const std::vector<Widget*>& getChildren() const override {
            return children_;
        }
};