#pragma once

#include "widgets/Widget.hpp"
#include "widgets/Label.hpp"
#include "functions/UTF8_Functions.hpp"
#include "functions/Font_Functions.hpp"
#include "widgets/interfaces/ITextInputTarget.hpp"

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

class KeyboardEng : public Widget, public ITextInputWidget {
    protected:
        std::vector<Widget*> children_;
        ITextInputTarget* target = nullptr;

    private:

        const static int key_h = 28;
        const static int key_w = 240 / (10 * 2);

        const static int keys_size = 43;

        //N→Normal
        //Z→コマンド
        //A→Aモード専用
        //B→Bモード専用
        //C→Cモード専用
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

            { "ABC", "ABC", 3, 'N' },
            { "あいう", "あいう", 4, 'N' },

            { "space", "Space", 8, 'A' },
            { "return", "Return", 5, 'A'},

            { "space", "Space", 8, 'B'},
            { "submit", "Submit", 5, 'B'},

            { "space", "Space", 5, 'C'},
            { "return", "Return", 5, 'C'},
            { "go", "Go", 3, 'C'},
            //3 + 4 + 8 + 5 = 20spaces

            { "\0", "\0", 0, 'Z'}
            //end
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

            { "123", "123", 3, 'N' },
            { "あいう", "あいう", 4, 'N' },
            
            { "space", "Space", 8, 'A' },
            { "return", "Return", 5, 'A'},

            { "space", "Space", 8, 'B'},
            { "submit", "Submit", 5, 'B'},

            { "space", "Space", 5, 'C'},
            { "return", "Return", 5, 'C'},
            { "go", "Go", 3, 'C'},
            //3 + 4 + 8 + 5 = 20spaces

            { "\0", "\0", 0, 'Z'}
            //end
        };

        String inputs = "";
        void addInput(String str){
            inputs += str;
            input_label->setText(inputs);
            input_label->setCursorToEnd();
            if(this->target) this->target->onTextChanged(this);
        }
        void removeInput(){
            if(inputs.length() == 0) return;

            inputs = UTF8_Functions::RemoveLastChar(inputs);
            input_label->setText(inputs);
            if(this->target) this->target->onTextChanged(this);
        }
        Key keyEnv(int index){
            if(isNumMode){ //123モード
                return keys_num[index];
            }else{ //ABCモード
                return keys[index];
            }
        }
        char getMode(){
            if(this->target){
                if(this->target->getIsSingleLine()){
                    return 'B';
                }else{
                    return 'C';
                }
            }
            return 'A';
        }
        bool isUpperCase = false;
        bool isNumMode = false;

    public:

        Label* input_label;

        void setVisible(bool visible) override;

        KeyboardEng(Label* input_label){
            this->l_rect = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};

            this->input_label = input_label;
            this->input_label->setVisible(false);
            this->visible = false;

            children_.push_back(input_label);
        }

        void causeOnPressStart() override;
        void render() override;

        const std::vector<Widget*>& getChildren() const override {
            return children_;
        }

        void setX(int x) override {};
        void setY(int y) override {};

        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::TRANSLUCENT; }

        void setInputTarget(ITextInputTarget* target) override {
            this->target = target;
        }
        void removeInputTarget(ITextInputTarget* valid_target) override {
            if(this->target == valid_target){
                this->target = nullptr;
            }
        }
        ITextInputTarget* getInputTarget() override {
            return this->target;
        }

        void setText(String text) override {
            this->inputs = text;
            input_label->setText(inputs);
            input_label->setCursorToEnd();
        }
        String getText() override {
            return this->inputs;
        }
};