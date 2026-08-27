#pragma once

#include "gui/widgets/Widget.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/interfaces/ITextInputTarget.hpp"
#include "functions/UTF8_Functions.hpp"
#include "functions/IME_Functions.hpp"
#include "functions/Font_Functions.hpp"
#include "consts.hpp"


class Keyboard : public Widget, public ITextInputWidget {
    protected:
        std::vector<Widget*> children_;
        ITextInputTarget* target = nullptr;

    private:
        const int SQUARE_W = SCREEN_WIDTH / 5;
        const int SQUARE_H = SQUARE_W / 1.5;

        const int CANDIDATES_H = 23;
        const int CANDIDATES_MARGIN = 3;

        const int START_KEY_Y = SCREEN_HEIGHT - SQUARE_H * 4;
        const int START_CANDIDATES_Y = START_KEY_Y - CANDIDATES_H;

        const String keys_jpn[4 * 5] = {
            "123", "あ", "か", "さ", "X",
            "ABC", "た", "な", "は", "空白",
            "カナ", "ま", "や", "ら", "改",
            "送り", "゛゜", "わ", "､｡?!", "行"
        };
        const String keys_num[4 * 5] = {
            "あいう","1",  "2",  "3", "X",
            "ABC", "4", "5", "6", "空白",
            "",   "7", "8", "9", "改",
            "", "()[]", "0", ".,-/", "行"
        };

        const char keys_font_style[4 * 5] = {
            'S', 'M', 'M', 'M', 'M',
            'S', 'M', 'M', 'M', 'S',
            'S', 'M', 'M', 'M', 'M',
            'S', 'S', 'M', 'S', 'M'
        }; //M → medium, S → Small
        //int keys_w[4 * 5];
        //int keys_h[4 * 5];

        bool is_swiping = false;
        int swipe_index = 0;
        int swipe_x_index = 0;
        int swipe_y_index = 0;
        const String swipe_jpn[12 * 5] = {
            "あ",   "い",   "う",   "え",   "お",
            "か",   "き",   "く",   "け",   "こ",
            "さ",   "し",   "す",   "せ",   "そ",
            "た",   "ち",   "つ",   "て",   "と",
            "な",   "に",   "ぬ",   "ね",   "の",
            "は",   "ひ",   "ふ",   "へ",   "ほ",
            "ま",   "み",   "む",   "め",   "も",
            "や",   "「",   "ゆ",   "」",   "よ",
            "ら",   "り",   "る",   "れ",   "ろ",
            "NO",   "NO",  "NO",  "NO",   "NO",
            "わ",   "を",   "ん",   "ー",   "NO",
            "、",   "。",   "?",    "!",   "NO"
        };
        const String swipe_num[12 * 5] = {
            "1", "←", "↑", "→", "↓",
            "2", "¥", "$", "€", "NO",
            "3", "%", "゜", "#", "NO",
            "4", "○", "*", "・", "NO",
            "5", "+", "×", "÷", "NO",
            "6", "<", "=", ">", "NO",
            "7", "「", "」", ":", "NO",
            "8", "〒", "々", "〆", "NO",
            "9", "^", "|", "\\", "NO",
            "(", ")", "[", "]", "NO",
            "0", "〜", "...", "NO", "NO",
            ".", ",", "-", "/", "NO"
        };

        const int swipe_directions[5 * 2] = {
            0, 0,
            -1, 0,
            0, -1,
            1, 0,
            0, 1
        };
        const static int HIRA_LIST_SIZE = 28 * 3 + 6;
        const String hira_list[HIRA_LIST_SIZE] = {
            "あ", "ぁ", "AA",
            "い", "ぃ", "AA",
            "う", "ぅ", "AA",
            "え", "ぇ", "AA",
            "お", "ぉ", "AA",
            "か", "が", "AA",
            "き", "ぎ", "AA",
            "く", "ぐ", "AA",
            "け", "げ", "AA",
            "こ", "ご", "AA",
            "さ", "ざ", "AA",
            "し", "じ", "AA",
            "す", "ず", "AA",
            "せ", "ぜ", "AA",
            "そ", "ぞ", "AA",
            "た", "だ", "AA",
            "ち", "ぢ", "AA",
            "つ", "っ", "づ", "BB",
            "て", "で", "AA",
            "と", "ど", "AA",
            "は", "ば", "ぱ", "BB",
            "ひ", "び", "ぴ", "BB",
            "ふ", "ぶ", "ぷ", "BB",
            "へ", "べ", "ぺ", "BB",
            "ほ", "ぼ", "ぽ", "BB",
            "や", "ゃ", "AA",
            "ゆ", "ゅ", "AA",
            "よ", "ょ", "AA"
        }; //AAは2文字戻り。 BBは3文字戻りを表す
        const String hira_back_2 = "AA";
        const String hira_back_3 = "BB";

        bool is_inputs_empty = true;
        String inputs = "";
        String inputs_done = "";
        String okuri_hira = "";

        int candidates_scroll_index = 0;
        int candidates_width[IME_Functions::candidates_size];

        bool keyboard_mode = false; //false -> jpn, true -> num
        
        char keysFontStyleEnv(int key_index){
            if(key_index == 3 * 5 - 1 || key_index == 4 * 5 - 1){ //改行
                if(is_inputs_empty)
                    if(this->target && !this->target->getIsSingleLine())
                        return 'S'; //決定or改行
            }
            return keys_font_style[key_index];
        }
        String keysEnv(int key_index){
            if(key_index == 3 * 5 - 1){ //改
                if(is_inputs_empty)
                    if(this->target)
                        if(this->target->getIsSingleLine())
                            return "決";
                        else
                            return "決定";
                    else if(!keyboard_mode)
                        return keys_jpn[key_index];
                    else
                        return keys_num[key_index];
                else
                    return "確";
            }
            if(key_index == 4 * 5 - 1){ //行
                if(is_inputs_empty)
                    if(this->target)
                        if(this->target->getIsSingleLine())
                            return "定";
                        else
                            return "改行";
                    else if(!keyboard_mode)
                        return keys_jpn[key_index];
                    else
                        return keys_num[key_index];
                else
                    return "定";
            }
            return (!keyboard_mode ? keys_jpn[key_index] : keys_num[key_index]);
        }
        String swipeEnv(int swipe_index){
            if(!keyboard_mode){
                return swipe_jpn[swipe_index];
            }else{
                return swipe_num[swipe_index];
            }
        }

        void updateInputs(bool notToCauseEvent){
            bool is_inputs_empty_now = inputs.length() == 0;
            
            if(is_inputs_empty != is_inputs_empty_now){
                is_inputs_empty = is_inputs_empty_now;
                this->needsRender();
            }

            is_inputs_empty = is_inputs_empty_now;
            input_label->setText(inputs_done + "~" + inputs + "~");
            input_label->setCursorToEnd();

            if(!notToCauseEvent){
                if(this->target) this->target->onTextChanged(this);
            }
        }

        void addInput(String input) {
            inputs += input;
            updateImeCandidates();
            updateInputs(false);
        }

        void removeInput() {
            if(is_inputs_empty){
                inputs_done = UTF8_Functions::RemoveLastChar(inputs_done);
                updateInputs(false);
                return;
            }

            if(okuri_hira.length() != 0){
                okuri_hira = "";
            }

            inputs = UTF8_Functions::RemoveLastChar(inputs);
            updateImeCandidates();
            updateInputs(false);
        }

        void commitAndClear() {
            inputs_done += inputs;
            inputs = "";
            okuri_hira = "";

            updateInputs(false);
        }

        void switchDakuten(){
            String ch = UTF8_Functions::GetLastChar(inputs);

            String switched_char = "";
            for(int i = 0; i < HIRA_LIST_SIZE; i++){
                if(hira_list[i] == ch){
                    if(hira_list[i + 1] == hira_back_2){
                        switched_char = hira_list[i - 1];
                        break;
                    }else if(hira_list[i + 1] == hira_back_3){
                        switched_char = hira_list[i - 2];
                        break;
                    }else{
                        switched_char = hira_list[i + 1];
                        break;
                    }
                }
            }
            if(switched_char.length() == 0) return;

            inputs = UTF8_Functions::ReplaceLastChar(inputs, switched_char);
            updateImeCandidates();
            updateInputs(false);
        }

        void switch_font_style(char style);
        void updateImeCandidates();
        void drawCandidates();

    public:

        Label* input_label;

        Keyboard(Label* input_label){
            this->l_rect = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
            
            this->visible = false;

            this->input_label = input_label;
            children_.push_back(input_label);
        }

        void setVisible(bool visible) override;

        void causeOnPressStart() override;
        void causeOnPressEnd() override;
        void render() override;

        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::TRANSLUCENT; }

        const std::vector<Widget*>& getChildren() const override {
            return children_;
        }

        void setX(int x) override {};
        void setY(int y) override {};

        void setInputTarget(ITextInputTarget* target) override {
            this->target = target;
            this->needsRender();
        }
        void removeInputTarget(ITextInputTarget* valid_target) override{
            if(this->target == valid_target){
                this->target = nullptr;
            }
            this->needsRender();
        }
        ITextInputTarget* getInputTarget() override{
            return this->target;
        }

        void setText(String text) override {
            this->inputs_done = text;
            this->inputs = "";
            this->updateInputs(true);
        }
        String getText() override {
            return this->inputs_done + this->inputs;
        }
};