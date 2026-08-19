#include "Keyboard.hpp"

#include "functions/GFX_Functions.hpp"
#include "functions/HitBox_Functions.hpp"
#include "OS_Data.hpp"

//表示の切り替え
void Keyboard::Visible(bool visible) {
    this->visible = visible;
    this->input_label->Visible(visible);
    this->input_label->MaxHeight(SCREEN_HEIGHT - 10 * 2 - this->rect.h);

    if(!visible){
        this->input_label->Text(this->inputs_done + this->inputs);
        if(this->target) this->target->onHide(this);
    }else{
        this->inputs_done = this->input_label->Text();
        this->inputs = "";
        if(this->target) this->target->onShow(this);
        this->updateInputs(false);
    }

    this->needsRender();
}

//フォントのスタイルの切り替え(主にキー用)
void Keyboard::switch_font_style(char style){
    switch(style) {
        case 'S': //small
            FontFn::SetSmall();
            break;
        case 'M': //medium
            FontFn::SetNormal();
            break;
    }
}

//候補を更新
void Keyboard::updateImeCandidates() {
    int candidates_counts = IME_Functions::ime_lookup(inputs.c_str());
    for(int i = 0; i < candidates_counts; i++){
        if(okuri_hira.length() != 0){
            strcat(IME_Functions::candidates[i], okuri_hira.c_str());
        }
        candidates_width[i] = OSData::frame->textWidth(IME_Functions::candidates[i], FontFn::GetSmall()); //横幅の更新
    }

    candidates_scroll_index = 0;
    
    this->needsRender(); //候補が更新されたため
}

//候補を描画
void Keyboard::drawCandidates(){

    //区画を確保
    OSData::frame->drawFastHLine(0, START_CANDIDATES_Y, SCREEN_WIDTH, PICO_BLACK);
    OSData::frame->drawFastHLine(0, START_CANDIDATES_Y + CANDIDATES_H, SCREEN_WIDTH, PICO_BLACK);


    FontFn::SetSmall();

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
        FontFn::SetDefault();
        return;
    }

    //候補の描画
    for(int i = candidates_scroll_index; i < n; i++){
        int w = candidates_width[i];
        if(cands_x + w + CANDIDATES_MARGIN > 200) break; //幅を超えそうになったら終わり

        OSData::frame->setCursor(cands_x, cands_y);
        OSData::frame->print(IME_Functions::candidates[i]);

        OSData::frame->drawFastVLine(cands_x + w + 2, START_CANDIDATES_Y, CANDIDATES_H, PICO_BLACK); //区切り線

        cands_x += w + CANDIDATES_MARGIN;
    }

    FontFn::SetDefault();
}

//タップ開始
void Keyboard::onPressStart() {
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
            this->needsRender(); //候補が移動したため
            return;
        }

        int cands_x = 0;
        for(int i = candidates_scroll_index; i < IME_Functions::candidatesCount; i++){
            int w = candidates_width[i];
            if(cands_x + w + CANDIDATES_MARGIN > 200) break; //候補が領域を超えそうなときは停止

            if(OSData::touchX - 1 >= cands_x && OSData::touchX <= cands_x + w + 2){ //押されてるかどうか
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
        this->needsRender(); //モードが変わったため
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
            if(this->target){
                if(this->target->GetIsSingleLine()){
                    this->target->onHide(this);
                    this->Visible(false);
                }else{
                    if(swipe_y_index == 2){
                        this->target->onHide(this);
                        this->Visible(false);
                    }else if(swipe_y_index == 3){
                        inputs = "\n";
                        commitAndClear();
                    }
                }
            }else{
                inputs = "\n";
                commitAndClear();
            }
        }else{
            commitAndClear();
        }
    }

    if(!keyboard_mode){ //ひらがなのときだけ
        //濁点、半濁点
        if(swipe_x_index == 1 && swipe_y_index == 3){
            switchDakuten();
            return;
        }
        //英字へ
        if(swipe_x_index == 0 && swipe_y_index == 1){
            this->Visible(false);
            OSData::keyboard_eng->Visible(true);
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
            updateInputs(false);
            updateImeCandidates();
        }
    }
    
    //スワイプ開始
    is_swiping = false;
    if(swipe_x_index >= 1 && swipe_x_index <= 3){ //スワイプ範囲内
        swipe_index = (swipe_x_index - 1) + swipe_y_index * 3;

        is_swiping = true;
        this->needsRender(); //フリックキーの描画のため       
    }
}
void Keyboard::onPressEnd() {
    if(on_press_end) on_press_end();

    //すワイプしてたら
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

        //位置の確定
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

        this->needsRender(); //フリックキーが消えるため
    }
    is_swiping = false;
}

void Keyboard::render() {
    if(!this->needs_redraw) return;
    if(!this->visible) return;

    PICO_GFX::drawDialogBackground();
    OSData::frame->fillRect(0, START_CANDIDATES_Y, SCREEN_WIDTH, SCREEN_HEIGHT - START_CANDIDATES_Y, this->background_color);
    PICO_GFX::markDirtyXYWH(0, START_KEY_Y - SQUARE_H, SCREEN_WIDTH, SCREEN_HEIGHT - START_KEY_Y + SQUARE_H);

    //候補
    OSData::frame->drawFastHLine(0, START_CANDIDATES_Y, SCREEN_WIDTH, PICO_BLACK);

    //キー
    for(int i = 1; i < 5; i++){
        OSData::frame->drawFastVLine(i * SQUARE_W, START_KEY_Y, SQUARE_H * 4, PICO_BLACK); //縦線
    }
    for(int i = 0; i < 4; i++){
        int LINE_WIDTH = 0;
        if(i == 3)
            if(is_inputs_empty && this->target && !this->target->GetIsSingleLine())
                LINE_WIDTH = SCREEN_WIDTH;
            else
                LINE_WIDTH = SCREEN_WIDTH - SQUARE_W;
        else
            LINE_WIDTH = SCREEN_WIDTH;
        OSData::frame->drawFastHLine(0, START_KEY_Y + SQUARE_H * i, LINE_WIDTH, PICO_BLACK); //横線
    }

    //キーの描画
    for(int x = 0; x < 5; x++){
        for(int y = 0; y < 4; y++){
            int draw_x = x * SQUARE_W;
            int draw_y = START_KEY_Y + y * SQUARE_H;

            switch_font_style(keysFontStyleEnv(x + y * 5));
            OSData::frame->setCursor(
                draw_x + (SQUARE_W - OSData::frame->textWidth(keysEnv(x + y * 5))) / 2,
                draw_y + (SQUARE_H - OSData::frame->fontHeight()) / 2
            );
            OSData::frame->print(keysEnv(x + y * 5));
        }
    }
    FontFn::SetDefault();
    drawCandidates();

    //フリックキーの入力
    if(is_swiping){
        int FONT_H = OSData::frame->fontHeight();
        for(int i = 1; i < 5; i++){
            if(swipeEnv(swipe_index * 5 + i) == "NO") continue;

            //座標系
            int FONT_W = OSData::frame->textWidth(swipeEnv(swipe_index * 5 + i));

            int BOX_X = SQUARE_W * (swipe_x_index + swipe_directions[i * 2]);
            int BOX_Y = START_KEY_Y + SQUARE_H * (swipe_y_index + swipe_directions[i * 2 + 1]);

            //塗りつぶし&矩形
            PICO_GFX::fillBackgroundXYWH(BOX_X, BOX_Y, SQUARE_W, SQUARE_H);
            OSData::frame->drawRect(BOX_X, BOX_Y, SQUARE_W, SQUARE_H, PICO_BLACK);

            //スワイプ用のテキストを表示
            OSData::frame->setCursor(BOX_X + (SQUARE_W - FONT_W) / 2, BOX_Y + (SQUARE_H - FONT_H) / 2);
            OSData::frame->print(swipeEnv(swipe_index * 5 + i));
        }  
    }    

    this->needs_redraw = false;
}