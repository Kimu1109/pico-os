#pragma once

#include "gui/widgets/keyboards/KeyboardPanel.hpp"
#include "functions/Font_Functions.hpp"
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

// 英字・記号(QWERTY)のキー盤
class KeyboardEng : public KeyboardPanel {
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

            { "abc", "abc", 3, 'N' },
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
            //文字の実寸(16pxフォント)は 123/abc=28px / かな=30px / ←→=16px /
            //space=42px / enter=40px / go=18px で、どのセルも1つ削ると枠線に文字が重なる。
            //ラベルや幅を変えるときはPCビルドの--shotで実際の描画を見て確かめること
            //(ホストテストはフォントがスタブなので幅を検証できない)。
            //幅に合わせて短くしたラベルが3つある:
            //  「Return」46px → 「enter」    (Cモードの4セル=48pxで枠線に接していた)
            //  「ABC」  34px → 「abc」       (3セル=36pxで左の枠線に接していた)
            //  シフト中の大文字表記をやめた   (Space/Return/Submitは小文字より数px太い)

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
            //文字の実寸(16pxフォント)は 123/abc=28px / かな=30px / ←→=16px /
            //space=42px / enter=40px / go=18px で、どのセルも1つ削ると枠線に文字が重なる。
            //ラベルや幅を変えるときはPCビルドの--shotで実際の描画を見て確かめること
            //(ホストテストはフォントがスタブなので幅を検証できない)。
            //幅に合わせて短くしたラベルが3つある:
            //  「Return」46px → 「enter」    (Cモードの4セル=48pxで枠線に接していた)
            //  「ABC」  34px → 「abc」       (3セル=36pxで左の枠線に接していた)
            //  シフト中の大文字表記をやめた   (Space/Return/Submitは小文字より数px太い)

            { "\0", "\0", 0, 'Z'}
            //end
        };

        FixedString<PICO_STR_LL> inputs;

        // inputs内の挿入位置(UTF-8文字単位)。
        // 位置の真はこちらが持ち、表示側へはバイト位置に直して渡す。
        // Labelのカーソルスロットはマークアップ記号(**や~)のぶんだけ
        // 文字数とずれるため、スロット番号をそのまま位置として使えない
        int cursor_char = 0;

        void addInput(const char* str){
            //半端に入ると壊れた文字が残るので、入り切らないときは何もしない
            if(inputs.length() + strlen(str) > FixedString<PICO_STR_LL>::capacity()) return;

            inputs.insertAtChar(cursor_char, str);
            cursor_char += FixedString<PICO_STR_LL>::charCount(str);

            this->notifyChanged(true);
        }
        void removeInput(){
            if(cursor_char <= 0){ //カーソルより前に文字が無い
                if(this->target) this->target->onBackspaceAtStart(this);
                return;
            }

            inputs.removeCharAt(cursor_char - 1);
            cursor_char--;

            this->notifyChanged(true);
        }
        //カーソルをdelta文字ぶん動かす(テキストは変えないのでonTextChangedは飛ばさない)
        void moveCursor(int delta){
            int next = cursor_char + delta;
            int last = inputs.charCount();
            if(next < 0) next = 0;
            if(next > last) next = last;
            if(next == cursor_char){
                if(delta != 0 && this->target) this->target->onCursorAtEdge(this, delta < 0 ? -1 : 1);
                return;
            }

            cursor_char = next;
            this->notifyChanged(false);
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
        static constexpr int PANEL_H = key_h * 4;

        KeyboardEng() : KeyboardPanel(PANEL_H) {}

        void causeOnPressStart() override;
        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::KeyboardEng; }

        void setText(const FixedString<PICO_STR_LL>& text) override {
            this->inputs = text;
            this->cursor_char = this->inputs.charCount(); //受け取った直後は末尾から書き足せるようにする
            this->notifyChanged(false);
        }
        FixedString<PICO_STR_LL> getText() override {
            return this->inputs;
        }
        size_t getCursorByteOffset() override {
            return (size_t)this->inputs.byteOffsetOfChar(this->cursor_char);
        }
        void setCursorByteOffset(size_t byte_offset) override {
            int c = CharIndexOfByte(this->inputs.c_str(), byte_offset);
            const int last = this->inputs.charCount();
            this->cursor_char = (c > last) ? last : c;
            this->notifyChanged(false);
        }
};
