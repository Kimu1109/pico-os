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

        const char* const keys_jpn[4 * 5] = {
            "123", "あ", "か", "さ", "X",
            "ABC", "た", "な", "は", "空白",
            "カナ", "ま", "や", "ら", "改",
            "送り", "゛゜", "わ", "､｡?!", "行"
        };
        const char* const keys_num[4 * 5] = {
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
        const char* const swipe_jpn[12 * 5] = {
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
        const char* const swipe_num[12 * 5] = {
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
        const char* const hira_list[HIRA_LIST_SIZE] = {
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
        const char* const hira_back_2 = "AA";
        const char* const hira_back_3 = "BB";

        bool is_inputs_empty = true;
        FixedString<PICO_STR_LL> inputs;
        FixedString<PICO_STR_LL> inputs_done;
        FixedString<5> okuri_hira;

        // 確定済みテキスト(inputs_done)上の挿入位置(UTF-8文字単位)。
        // 変換中の読み(inputs)は常にこの位置へ挟まる形で表示・確定される。
        //
        // 位置の真はこちらが持ち、input_labelへはバイト位置に直して渡す。
        // Labelのカーソルスロットは読みを囲む`~`(波線のマークアップ)のぶんだけ
        // 文字数とずれるため、スロット番号をそのまま位置として使えない
        int done_cursor = 0;

        int candidates_scroll_index = 0;
        int candidates_width[IME_Functions::candidates_size];

        bool keyboard_mode = false; //false -> jpn, true -> num
        
        //カナ/送りは変換中にしか働かないので、働かない場面ではカーソル移動キーとして使う
        bool isCursorKeyCell(int key_index) const {
            if(key_index != 2 * 5 + 0 && key_index != 3 * 5 + 0) return false;
            return keyboard_mode || is_inputs_empty;
        }

        char keysFontStyleEnv(int key_index){
            if(isCursorKeyCell(key_index)) return 'M'; //矢印は1文字なので中サイズで出す
            if(key_index == 3 * 5 - 1 || key_index == 4 * 5 - 1){ //改行
                if(is_inputs_empty)
                    if(this->target && !this->target->getIsSingleLine())
                        return 'S'; //決定or改行
            }
            return keys_font_style[key_index];
        }
        const char* keysEnv(int key_index){
            if(isCursorKeyCell(key_index)){ //カナ/送り → カーソル左/右
                return (key_index == 2 * 5 + 0) ? "←" : "→";
            }
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
        const char* swipeEnv(int swipe_index){
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

            //確定済みテキストをカーソル位置で割り、そこへ変換中の読みを挟んで表示する。
            //読みを囲む`~`は波線のマークアップで、記号自体は描画されない。
            //変換中でないときに囲みを出さないのは、空の`~~`が取り消し線の開始として
            //解釈され、カーソル以降の確定済みテキストに線が入ってしまうため
            const size_t split = (size_t)inputs_done.byteOffsetOfChar(done_cursor);

            FixedString<PICO_STR_LL> display;
            display.assign(inputs_done.c_str(), split);
            if(!is_inputs_empty_now){
                display.append("~");
                display.append(inputs);
                display.append("~");
            }
            display.append(inputs_done.c_str() + split);

            input_label->setText(display);
            //変換中は読みの直後、そうでなければカーソル位置そのものへ置く
            input_label->setCursorToByteOffset(
                is_inputs_empty_now ? split : split + 1 + inputs.length()
            );

            if(!notToCauseEvent){
                if(this->target) this->target->onTextChanged(this);
            }
        }

        void addInput(const char* input) {
            inputs.append(input);
            updateImeCandidates();
            updateInputs(false);
        }

        void removeInput() {
            if(is_inputs_empty){
                //確定済みテキストからカーソルの直前の1文字を消す
                if(done_cursor <= 0) return;

                inputs_done.removeCharAt(done_cursor - 1);
                done_cursor--;
                updateInputs(false);
                return;
            }

            if(okuri_hira.length() != 0){
                okuri_hira.clear();
            }

            inputs.removeLastChar();
            updateImeCandidates();
            updateInputs(false);
        }

        void commitAndClear() {
            if(inputs.length() != 0){
                //入り切らないときは確定せず読みのまま残す(半端に挿すと壊れた文字が残るため)
                if(inputs_done.length() + inputs.length() > FixedString<PICO_STR_LL>::capacity()) return;

                inputs_done.insertAtChar(done_cursor, inputs);
                done_cursor += inputs.charCount();
            }
            inputs.clear();
            okuri_hira.clear();

            updateInputs(false);
        }

        //カーソルをdelta文字ぶん動かす。変換中の読みがあれば先に確定させる
        void moveCursor(int delta) {
            if(!is_inputs_empty) commitAndClear();

            int next = done_cursor + delta;
            int last = inputs_done.charCount();
            if(next < 0) next = 0;
            if(next > last) next = last;
            if(next == done_cursor) return;

            done_cursor = next;
            updateInputs(true); //テキストは変えていないのでonTextChangedは飛ばさない
        }

        void switchDakuten(){
            FixedString<5> ch = inputs.lastChar();

            FixedString<5> switched_char;
            for(int i = 0; i < HIRA_LIST_SIZE; i++){
                if(strcmp(hira_list[i], ch.c_str()) == 0){
                    if(strcmp(hira_list[i + 1], hira_back_2) == 0){
                        switched_char.assign(hira_list[i - 1]);
                        break;
                    }else if(strcmp(hira_list[i + 1], hira_back_3) == 0){
                        switched_char.assign(hira_list[i - 2]);
                        break;
                    }else{
                        switched_char.assign(hira_list[i + 1]);
                        break;
                    }
                }
            }
            if(switched_char.length() == 0) return;

            inputs.replaceLastChar(switched_char);
            updateImeCandidates();
            updateInputs(false);
        }

        void switch_font_style(char style);
        void updateImeCandidates();
        void drawCandidates();

    public:

        Label<PICO_STR_LL>* input_label;

        Keyboard(Label<PICO_STR_LL>* input_label){
            this->l_rect = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
            
            this->visible = false;

            this->input_label = input_label;
            children_.push_back(input_label);
        }

        void setVisible(bool visible) override;

        void causeOnPressStart() override;
        void causeOnPressEnd() override;
        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::Keyboard; }

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

        void setText(const FixedString<PICO_STR_LL>& text) override {
            this->inputs_done = text;
            this->inputs.clear();
            this->done_cursor = this->inputs_done.charCount(); //受け取った直後は末尾から書き足せるようにする
            this->updateInputs(true);
        }
        FixedString<PICO_STR_LL> getText() override {
            //表示と同じく、変換中の読みはカーソル位置へ挟んで返す
            const size_t split = (size_t)inputs_done.byteOffsetOfChar(done_cursor);

            FixedString<PICO_STR_LL> result;
            result.assign(inputs_done.c_str(), split);
            result.append(inputs);
            result.append(inputs_done.c_str() + split);
            return result;
        }
};