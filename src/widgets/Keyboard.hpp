#pragma once

#include "widgets/Widget.hpp"
#include "widgets/Label.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/HitBox_Functions.hpp"
#include "functions/UTF8_Functions.hpp"
#include "functions/IME_Functions.hpp"
#include "OS_Data.hpp"

class Keyboard : public Widget {
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
        int keys_w[4 * 5];
        int keys_h[4 * 5];

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

        void switch_font_style(char style){
            switch(style) {
                case 'S': //small
                    OSData::frame->setFont(&lgfxJapanGothicP_16);
                    break;
                case 'M': //medium
                    OSData::frame->setFont(&lgfxJapanGothicP_24);
                    break;
            }
        }

        bool is_inputs_empty = true;
        String inputs = "";
        String inputs_done = "";
        String okuri_hira = "";
        int candidates_scroll_index = 0;

        bool keyboard_mode = false; //false -> jpn, true -> num

        void updateImeCandidates() {
            int candidates_counts = IME_Functions::ime_lookup(inputs.c_str());
            if(okuri_hira.length() != 0){ //送りありの場合、送りをつけてあげる
                IME_Functions::okuri_attach(okuri_hira.c_str());
            }

            candidates_scroll_index = 0;
            drawCandidates();
        }
        void drawCandidates(){
            OSData::frame->fillRect(0, START_CANDIDATES_Y + 1, SCREEN_WIDTH, CANDIDATES_H - 1, PICO_WHITE);
            OSData::frame->drawFastHLine(0, START_CANDIDATES_Y, SCREEN_WIDTH, PICO_BLACK);
            OSData::frame->drawFastHLine(0, START_CANDIDATES_Y + CANDIDATES_H, SCREEN_WIDTH, PICO_BLACK);
            PICO_GFX::markDirty(0, START_CANDIDATES_Y + 1, SCREEN_WIDTH, CANDIDATES_H - 1);

            OSData::frame->setFont(&lgfxJapanGothicP_16);

            int cands_x = 0;
            int cands_y = START_CANDIDATES_Y + (CANDIDATES_H - OSData::frame->fontHeight()) / 2;

            //候補を左へスクロール
            OSData::frame->setCursor(200 + CANDIDATES_MARGIN, cands_y);
            OSData::frame->print("←");
            //候補を右へスクロール
            OSData::frame->setCursor(200 + OSData::frame->textWidth("←") + CANDIDATES_MARGIN * 2, cands_y);
            OSData::frame->print("→");

            int n = IME_Functions::candidatesCount;
            if(n == 0) { //候補がなかったら描画しない
                OSData::frame->setFont(&lgfxJapanGothicP_24);
                return;
            }

            for(int i = candidates_scroll_index; i < n; i++){
                int w = IME_Functions::candidates_width[i];
                if(cands_x + w + CANDIDATES_MARGIN > 200) break;

                OSData::frame->setCursor(cands_x, cands_y);
                OSData::frame->print(IME_Functions::candidates[i]);

                OSData::frame->drawFastVLine(cands_x + w + 2, START_CANDIDATES_Y, CANDIDATES_H, PICO_BLACK);

                cands_x += w + CANDIDATES_MARGIN;
            }

            OSData::frame->setFont(&lgfxJapanGothicP_24);
        }
        String keysEnv(int key_index){
            if(key_index == 3 * 5 - 1){ //改
                return (
                    is_inputs_empty ?
                        (!keyboard_mode ? keys_jpn[key_index] : keys_num[key_index]) :
                        "確"
                );
            }
            if(key_index == 4 * 5 - 1){ //行
                return (
                    is_inputs_empty ?
                        (!keyboard_mode ? keys_jpn[key_index] : keys_num[key_index]) : 
                        "定"
                );
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
        void calcKeySize(){
            //キーの横幅と高さを計算
            for(int i = 0; i < 4 * 5; i++){
                switch_font_style(keys_font_style[i]);
                keys_w[i] = OSData::frame->textWidth(keysEnv(i));
                keys_h[i] = OSData::frame->fontHeight();
            }
        }
        void drawKey(int x, int y, bool is_redraw){
            int draw_x = x * SQUARE_W;
            int draw_y = START_KEY_Y + y * SQUARE_H;

            if(x < 0 || x > 4 || y < 0 || y > 3) {
                if(is_redraw) {
                    PICO_GFX::markDirty(draw_x, draw_y, SQUARE_W, SQUARE_H);
                    OSData::frame->fillRect(draw_x, draw_y, SQUARE_W + 1, SQUARE_H + 1, PICO_BACKGROUND);
                }
                return;
            }

            PICO_GFX::markDirty(draw_x, draw_y, SQUARE_W, SQUARE_H);

            if(is_redraw){
                OSData::frame->fillRect(draw_x, draw_y, SQUARE_W + 1, SQUARE_H + 1, PICO_BACKGROUND);
                OSData::frame->drawRect(draw_x, draw_y, SQUARE_W + 1, SQUARE_H + 1, PICO_BLACK);

                //改行に線が入るのを対策
                if(x == 4 && y == 2){
                    OSData::frame->drawFastHLine(draw_x + 1, draw_y + SQUARE_H, SQUARE_W, PICO_BACKGROUND);
                }
                if(x == 4 && y == 3){
                    OSData::frame->drawFastHLine(draw_x + 1, draw_y, SQUARE_W, PICO_BACKGROUND);
                }
            }

            switch_font_style(keys_font_style[x + y * 5]);
            OSData::frame->setCursor(
                draw_x + (SQUARE_W - keys_w[x + y * 5]) / 2,
                draw_y + (SQUARE_H - keys_h[x + y * 5]) / 2
            );
            OSData::frame->print(keysEnv(x + y * 5));
        }

    public:

        Label* dev_label; //!TODO REMOVE

        Keyboard(){
            this->x = 0;
            this->y = START_CANDIDATES_Y;
            this->w = SCREEN_WIDTH;
            this->h = SCREEN_HEIGHT - this->y;

            calcKeySize();
            OSData::frame->setFont(&lgfxJapanGothicP_24);
            
            this->needs_redraw = true;

            dev_label = new Label(""); //!TODO REMOVE
            dev_label->MaxWidth(SCREEN_WIDTH);
        }

        void updateInputs(){
            bool is_inputs_empty_now = inputs.length() == 0;
            
            if(is_inputs_empty != is_inputs_empty_now){
                is_inputs_empty = is_inputs_empty_now;
                drawKey(4, 2, true); //改 | 確
                drawKey(4, 3, true); //行 | 定
            }

            is_inputs_empty = is_inputs_empty_now;
            dev_label->Text(inputs_done + "~" + inputs + "~");
        }

        void addInput(String input) {
            inputs += input;
            updateImeCandidates();
            updateInputs();
        }

        void removeInput() {
            if(is_inputs_empty){
                inputs_done = UTF8_Functions::removeLastChar(inputs_done);
                updateInputs();
                return;
            }

            if(okuri_hira.length() != 0){
                okuri_hira = "";
            }

            inputs = UTF8_Functions::removeLastChar(inputs);
            updateImeCandidates();
            updateInputs();
        }

        void commitAndClear() {
            inputs_done += inputs;
            inputs = "";
            okuri_hira = "";

            updateInputs();
        }

        void switchDakuten(){
            String ch = UTF8_Functions::getLastChar(inputs);

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

            inputs = UTF8_Functions::replaceLastChar(inputs, switched_char);
            updateImeCandidates();
            updateInputs();
        }

        void onPressStart() override {
            if(on_press_start) on_press_start();

            if(OSData::touchY < START_KEY_Y){ //候補のタップ
                if(OSData::touchX > 200){ //候補のスクロール
                    if(OSData::touchX < 200 + OSData::frame->textWidth("←") + CANDIDATES_MARGIN){
                        candidates_scroll_index -= 1; //左へ
                        if(candidates_scroll_index < 0) candidates_scroll_index = 0;
                    }else{
                        candidates_scroll_index += 1; //右へ
                        if(candidates_scroll_index == IME_Functions::candidatesCount) candidates_scroll_index -= 1;
                    }
                    drawCandidates();
                    return;
                }

                int cands_x = 0;
                for(int i = candidates_scroll_index; i < IME_Functions::candidatesCount; i++){
                    int w = IME_Functions::candidates_width[i];
                    if(cands_x + w + CANDIDATES_MARGIN > 200) break;

                    if(OSData::touchX - 1 >= cands_x && OSData::touchX <= cands_x + w + 2){
                        inputs = IME_Functions::candidates[i];
                        commitAndClear();
                        return;
                    }

                    cands_x += w + CANDIDATES_MARGIN;
                }
                return;
            }

            //インデックス
            swipe_x_index = floor((float)OSData::touchX / (float)SQUARE_W);
            swipe_y_index = floor((float)(OSData::touchY - START_KEY_Y) / (float)SQUARE_H);

            //数字モードへ or ひらがなモードへ
            if(swipe_x_index == 0 && swipe_y_index == 0){
                keyboard_mode = !keyboard_mode;
                calcKeySize();
                this->needs_redraw = true;
            }

            //1文字削除
            if(swipe_x_index == 4 && swipe_y_index == 0){
                removeInput();
            }
            //空白
            if(swipe_x_index == 4 && swipe_y_index == 1){
                addInput(" ");
            }
            //改行/確定
            if(swipe_x_index == 4 && swipe_y_index >= 2){
                if(is_inputs_empty){
                    inputs = "\n";
                }
                commitAndClear();
            }
            if(!keyboard_mode){ //ひらがなのときだけ
                //濁点、半濁点
                if(swipe_x_index == 1 && swipe_y_index == 3){
                    switchDakuten();
                    return;
                }
                //カタカナへ
                if(swipe_x_index == 0 && swipe_y_index == 2 && !is_inputs_empty){
                    inputs = UTF8_Functions::hiraganaToKatakana(inputs);
                    commitAndClear();
                }
                //送りへ
                if(swipe_x_index == 0 && swipe_y_index == 3 && !is_inputs_empty){
                    if(okuri_hira.length() == 0){ //送り開始
                        okuri_hira = UTF8_Functions::getLastChar(inputs);
                        inputs = IME_Functions::buildOkuriKey(inputs, okuri_hira.c_str());

                    }else if(okuri_hira == "い" && UTF8_Functions::getLastChar(inputs) == "w") { //形容詞に配慮
                        inputs = UTF8_Functions::removeLastChar(inputs);
                        inputs += "i";
                    }else { //送り解除
                        inputs = UTF8_Functions::removeLastChar(inputs);
                        inputs += okuri_hira;
                        okuri_hira = "";
                    }
                    updateInputs();
                    updateImeCandidates();
                }
            }
            
            is_swiping = false;
            if(swipe_x_index >= 1 && swipe_x_index <= 3){ //スワイプ範囲内
                swipe_index = (swipe_x_index - 1) + swipe_y_index * 3;

                is_swiping = true;
                int FONT_H = OSData::frame->fontHeight();
                for(int i = 1; i < 5; i++){
                    if(swipeEnv(swipe_index * 5 + i) == "NO") continue;

                    //座標系
                    int FONT_W = OSData::frame->textWidth(swipeEnv(swipe_index * 5 + i));

                    int BOX_X = SQUARE_W * (swipe_x_index + swipe_directions[i * 2]);
                    int BOX_Y = START_KEY_Y + SQUARE_H * (swipe_y_index + swipe_directions[i * 2 + 1]);

                    //塗りつぶし&矩形
                    OSData::frame->fillRect(BOX_X, BOX_Y, SQUARE_W, SQUARE_H, PICO_BACKGROUND);
                    OSData::frame->drawRect(BOX_X, BOX_Y, SQUARE_W, SQUARE_H, PICO_BLACK);

                    //スワイプ用のテキストを表示
                    OSData::frame->setCursor(BOX_X + (SQUARE_W - FONT_W) / 2, BOX_Y + (SQUARE_H - FONT_H) / 2);
                    OSData::frame->print(swipeEnv(swipe_index * 5 + i));

                    PICO_GFX::markDirty(BOX_X, BOX_Y, SQUARE_W, SQUARE_H);
                }                
            }
        }
        void onPressMove() override {
            if(on_press_move) on_press_move();
        }
        void onPressEnd() override {
            if(on_press_end) on_press_end();

            if(is_swiping){

                //準備
                struct HitBoxFunctions::Point point;
                point.x = OSData::touchX;
                point.y = OSData::touchY;

                struct HitBoxFunctions::Rect rect;
                rect.minX = SQUARE_W * swipe_x_index;
                rect.minY = START_KEY_Y + SQUARE_H * swipe_y_index;
                rect.maxX = SQUARE_W * (swipe_x_index + 1);
                rect.maxY = START_KEY_Y + SQUARE_H * (swipe_y_index + 1);

                int input_relative_index = 0;
                switch (HitBoxFunctions::classifyPoint(point, rect)){
                    case HitBoxFunctions::Region::Inside:
                        input_relative_index = 0;
                        break;
                    case HitBoxFunctions::Region::Left:
                        input_relative_index = 1;
                        break;
                    case HitBoxFunctions::Region::Top:
                        input_relative_index = 2;
                        break;
                    case HitBoxFunctions::Region::Right:
                        input_relative_index = 3;
                        break;
                    case HitBoxFunctions::Region::Bottom:
                        input_relative_index = 4;
                        break;
                }

                //入力の確定
                int input_swipe_index = ((swipe_x_index - 1) + (swipe_y_index * 3)) * 5 + input_relative_index;
                if(swipeEnv(input_swipe_index) != "NO")
                    addInput(swipeEnv(input_swipe_index));

                for(int i = 1; i < 5; i++){
                    if(swipeEnv(swipe_index * 5 + i) == "NO") continue;

                    //インデックス&座標
                    int x_index = swipe_x_index + swipe_directions[i * 2];
                    int y_index = swipe_y_index + swipe_directions[i * 2 + 1];

                    int BOX_X = SQUARE_W * x_index;
                    int BOX_Y = START_KEY_Y + SQUARE_H * y_index;

                    int keys_index = x_index + y_index * 5;

                    //キーの再描画
                    drawKey(x_index, y_index, true);
                }

                OSData::frame->setFont(&lgfxJapanGothicP_24);

                if(swipe_y_index == 0) drawCandidates(); //候補を再描画
            }
            is_swiping = false;
        }

        void render() override {
            if(!this->needs_redraw) return;

            PICO_GFX::markDirty(0, START_CANDIDATES_Y, SCREEN_WIDTH, SCREEN_HEIGHT - START_CANDIDATES_Y);
            OSData::frame->fillRect(0, START_CANDIDATES_Y, SCREEN_WIDTH, SCREEN_HEIGHT - START_CANDIDATES_Y, PICO_BACKGROUND);

            //候補
            OSData::frame->drawFastHLine(0, START_CANDIDATES_Y, SCREEN_WIDTH, PICO_BLACK);

            //キー
            for(int i = 1; i < 5; i++){
                OSData::frame->drawFastVLine(i * SQUARE_W, START_KEY_Y, SQUARE_H * 4, PICO_BLACK); //縦線
            }
            for(int i = 0; i < 4; i++){
                int LINE_WIDTH = i == 3 ? (SCREEN_WIDTH - SQUARE_W) : SCREEN_WIDTH;
                OSData::frame->drawFastHLine(0, START_KEY_Y + SQUARE_H * i, LINE_WIDTH, PICO_BLACK); //横線
            }

            //キーの描画
            for(int x = 0; x < 5; x++){
                for(int y = 0; y < 4; y++){
                    drawKey(x, y, false);
                }
            }
            OSData::frame->setFont(&lgfxJapanGothicP_24);
            drawCandidates();

            this->needs_redraw = false;
        }
};