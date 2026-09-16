#pragma once

#include "gui/widgets/Widget.hpp"
#include "gui/widgets/Label.hpp"
#include "functions/Font_Functions.hpp"
#include "gui/widgets/interfaces/ITextInputTarget.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

struct Key {
    const char* str;
    const char* str_upper;
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

        const static int keys_size = 47;

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
            { "かな", "かな", 3, 'N' },
            { "←", "←", 2, 'N' },

            { "space", "space", 5, 'A' },
            { "→", "→", 2, 'A' },
            { "enter", "enter", 5, 'A'},

            { "space", "space", 5, 'B'},
            { "→", "→", 2, 'B'},
            { "submit", "submit", 5, 'B'},

            { "space", "space", 4, 'C'},
            { "→", "→", 2, 'C'},
            { "enter", "enter", 4, 'C'},
            { "go", "go", 2, 'C'},
            //A/B: 3 + 3 + 2 + 5 + 2 + 5 = 20spaces
            //C  : 3 + 3 + 2 + 4 + 2 + 4 + 2 = 20spaces
            //
            //最下段は20セルを使い切っていて幅の余りが無い。1セル=12pxに対し
            //文字の実寸(16pxフォント)は 123=28px / かな=32px / ←→=16px /
            //space=42px / enter=39px / go=18px で、セルを1つ削るとすぐ枠線に重なる。
            //ラベルや幅を変えるときはPCビルドの--shotで実際の描画を見て確かめること
            //(ホストテストはフォントがスタブなので幅を検証できない)。
            //「return」(46px)はCモードの4セル=48pxで枠線に接していたため「enter」にした。
            //シフト中も表記を変えないのは、大文字にすると幅が増えて収まらなくなるため

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
            { "かな", "かな", 3, 'N' },
            { "←", "←", 2, 'N' },

            { "space", "space", 5, 'A' },
            { "→", "→", 2, 'A' },
            { "enter", "enter", 5, 'A'},

            { "space", "space", 5, 'B'},
            { "→", "→", 2, 'B'},
            { "submit", "submit", 5, 'B'},

            { "space", "space", 4, 'C'},
            { "→", "→", 2, 'C'},
            { "enter", "enter", 4, 'C'},
            { "go", "go", 2, 'C'},
            //A/B: 3 + 3 + 2 + 5 + 2 + 5 = 20spaces
            //C  : 3 + 3 + 2 + 4 + 2 + 4 + 2 = 20spaces
            //
            //最下段は20セルを使い切っていて幅の余りが無い。1セル=12pxに対し
            //文字の実寸(16pxフォント)は 123=28px / かな=32px / ←→=16px /
            //space=42px / enter=39px / go=18px で、セルを1つ削るとすぐ枠線に重なる。
            //ラベルや幅を変えるときはPCビルドの--shotで実際の描画を見て確かめること
            //(ホストテストはフォントがスタブなので幅を検証できない)。
            //「return」(46px)はCモードの4セル=48pxで枠線に接していたため「enter」にした。
            //シフト中も表記を変えないのは、大文字にすると幅が増えて収まらなくなるため

            { "\0", "\0", 0, 'Z'}
            //end
        };

        FixedString<PICO_STR_LL> inputs;

        // inputs内の挿入位置(UTF-8文字単位)。
        // 位置の真はこちらが持ち、input_label側へはバイト位置に直して渡す。
        // Labelのカーソルスロットはマークアップ記号(**や~)のぶんだけ
        // 文字数とずれるため、スロット番号をそのまま位置として使えない
        int cursor_char = 0;

        //inputsとカーソル位置をinput_labelへ反映する
        void syncInputLabel(){
            input_label->setText(inputs);
            input_label->setCursorToByteOffset((size_t)inputs.byteOffsetOfChar(cursor_char));
        }
        void addInput(const char* str){
            //半端に入ると壊れた文字が残るので、入り切らないときは何もしない
            if(inputs.length() + strlen(str) > FixedString<PICO_STR_LL>::capacity()) return;

            inputs.insertAtChar(cursor_char, str);
            cursor_char += FixedString<PICO_STR_LL>::charCount(str);

            syncInputLabel();
            if(this->target) this->target->onTextChanged(this);
        }
        void removeInput(){
            if(cursor_char <= 0) return; //カーソルより前に文字が無い

            inputs.removeCharAt(cursor_char - 1);
            cursor_char--;

            syncInputLabel();
            if(this->target) this->target->onTextChanged(this);
        }
        //カーソルをdelta文字ぶん動かす(テキストは変えないのでonTextChangedは飛ばさない)
        void moveCursor(int delta){
            int next = cursor_char + delta;
            int last = inputs.charCount();
            if(next < 0) next = 0;
            if(next > last) next = last;
            if(next == cursor_char) return;

            cursor_char = next;
            input_label->setCursorToByteOffset((size_t)inputs.byteOffsetOfChar(cursor_char));
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

        Label<PICO_STR_LL>* input_label;

        void setVisible(bool visible) override;

        KeyboardEng(Label<PICO_STR_LL>* input_label){
            this->l_rect = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};

            this->input_label = input_label;
            this->input_label->setVisible(false);
            this->visible = false;

            children_.push_back(input_label);
        }

        void causeOnPressStart() override;
        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::KeyboardEng; }

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

        void setText(const FixedString<PICO_STR_LL>& text) override {
            this->inputs = text;
            this->cursor_char = this->inputs.charCount(); //受け取った直後は末尾から書き足せるようにする
            syncInputLabel();
        }
        FixedString<PICO_STR_LL> getText() override {
            return this->inputs;
        }
};