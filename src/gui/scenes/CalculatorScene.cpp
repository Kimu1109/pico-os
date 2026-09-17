#include "gui/scenes/CalculatorScene.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "util/Calc_Eval.hpp"

#include <cstring>

namespace {
    // "="確定時に評価が失敗した場合のメッセージ。途中式のプレビュー失敗では出さない
    // (refreshDisplays()側は黙って空欄にするだけ)
    const char* ErrorMessage(CalcEval::Error e){
        switch(e){
            case CalcEval::Error::DivByZero:  return "0で割ることはできません";
            case CalcEval::Error::Domain:     return "計算できません";
            case CalcEval::Error::TooComplex: return "式が複雑すぎます";
            case CalcEval::Error::Syntax:
            default:                          return "式が正しくありません";
        }
    }

    // 数値を表示用に整形する。%.10gは有効数字10桁で末尾の0を自動的に落とすので、
    // 電卓の結果表示としてちょうどよい(11 → "11", sqrt(10) → "3.16227766"等)
    void FormatNumber(double v, FixedString<PICO_STR_M>& out){
        out.clear();
        out.appendFormat("%.10g", v);
    }
}

Rect CalculatorScene::bodyRect() const {
    const Rect content = Scene::contentRect();
    const int16_t top = content.y + MARGIN + this->top_row_h + MARGIN;

    return {
        content.x,
        top,
        content.w,
        (int16_t)(content.y + content.h - top)
    };
}

void CalculatorScene::applyPage(){
    const bool is_calc = (this->page == Page::Calculator);

    if(this->expr_label)   this->expr_label->setVisible(is_calc);
    if(this->result_label) this->result_label->setVisible(is_calc);
    if(this->keypad)       this->keypad->setVisible(is_calc);

    if(this->history_clear_button) this->history_clear_button->setVisible(!is_calc);
    if(this->history_list)         this->history_list->setVisible(!is_calc);
}

int CalculatorScene::openParenCount() const {
    int depth = 0;
    for(size_t i = 0; i < this->expression.length(); i++){
        if(this->expression[i] == '(') depth++;
        else if(this->expression[i] == ')') depth--;
    }
    return depth;
}

void CalculatorScene::refreshDisplays(){
    if(this->expr_label){
        this->expr_label->setText(this->expression.empty() ? "0" : this->expression.c_str());
    }

    if(!this->result_label) return;

    //入力中のプレビューは「まだ確定していない」ことが分かるよう灰色にする
    //("="で確定した答えは黒くはっきり出す。commitCalculation()側を参照)。
    //ここに来るのは常に数値のプレビューなので、エラー文言用に縮めたフォント
    //(下記commitCalculation()参照)を戻しておく
    this->result_label->setTextColor(PICO_DARKGREY);
    this->result_label->setFontSize(FontFn::Bigger);

    //空、または不完全な式(例: "3+")は評価に失敗して当然なので、エラー扱いにせず
    //黙って前のプレビューを消すだけにする。エラーとして出すのは"="を押した時だけ
    if(this->expression.empty()){
        this->result_label->setText("");
        return;
    }

    const CalcEval::Result r = CalcEval::Evaluate(this->expression.c_str());
    if(!r.ok()){
        this->result_label->setText("");
        return;
    }

    FixedString<PICO_STR_M> text;
    FormatNumber(r.value, text);
    this->result_label->setText(text);
}

void CalculatorScene::commitCalculation(){
    if(this->expression.empty()) return;

    const CalcEval::Result r = CalcEval::Evaluate(this->expression.c_str());
    if(!r.ok()){
        if(this->result_label){
            //構文エラー等は結果ではないので、灰色のプレビューとは別に赤で区別する
            //(ACキーの赤字と同じく、状態否定にPICO_REDを使う既存の慣習に揃える)。
            //数値結果と同じBigger(48px)のままだとメッセージが画面幅を超えてしまうため、
            //エラー文言のときだけSmallへ縮める(数値に戻る時はrefreshDisplays()が戻す)
            this->result_label->setTextColor(PICO_RED);
            this->result_label->setFontSize(FontFn::Small);
            this->result_label->setText(ErrorMessage(r.error));
        }
        LOG_SYS_WARN("電卓: 式の評価に失敗しました (%s)", this->expression.c_str());
        return;
    }

    FixedString<PICO_STR_M> text;
    FormatNumber(r.value, text);

    this->pushHistory(this->expression, text);

    if(this->expr_label)   this->expr_label->setText(this->expression.c_str());
    if(this->result_label){
        //"="で確定した答えは灰色のプレビューと区別できるよう黒ではっきり出す。
        //直前がエラー表示(Smallへ縮めてある)だった場合に備えてBiggerへ戻す
        this->result_label->setTextColor(PICO_FORECOLOR);
        this->result_label->setFontSize(FontFn::Bigger);
        this->result_label->setText(text);
    }

    this->result_value_text = text;
    this->last_was_result   = true;
}

void CalculatorScene::onKey(const char* key){
    if(strcmp(key, "AC") == 0){
        this->expression.clear();
        this->last_was_result = false;
        this->refreshDisplays();
        return;
    }

    if(strcmp(key, "X") == 0){
        if(this->last_was_result){
            //確定結果を見ている状態でのXは、新しい式を打ち直す合図として扱う
            this->expression.clear();
            this->last_was_result = false;
        }else{
            this->expression.removeLastChar();
        }
        this->refreshDisplays();
        return;
    }

    if(strcmp(key, "=") == 0){
        this->commitCalculation();
        return;
    }

    const bool is_operator = (strcmp(key, "+") == 0 || strcmp(key, "-") == 0 ||
                               strcmp(key, "×") == 0 || strcmp(key, "÷") == 0);

    if(this->last_was_result){
        if(is_operator){
            //直前の結果から続けて計算する(一般的な電卓の挙動)
            this->expression = this->result_value_text;
        }else{
            this->expression.clear();
        }
        this->last_was_result = false;
    }

    //")"は開いている"("が無ければ捨てる(壊れた式を作らせない)
    if(strcmp(key, ")") == 0 && this->openParenCount() <= 0) return;

    //"√"単体は入力にならないので、括弧を自動で開いて引数を促す
    const char* to_append = (strcmp(key, "√") == 0) ? "√(" : key;

    if(this->expression.length() + strlen(to_append) >= this->expression.capacity()){
        return; //桁あふれ。切り詰めて別の式になるくらいなら無視する
    }
    this->expression.append(to_append);

    this->refreshDisplays();
}

void CalculatorScene::pushHistory(const FixedString<PICO_STR_L>& expr, const FixedString<PICO_STR_M>& result){
    const int last = (this->history_count < kMaxHistory) ? this->history_count : (kMaxHistory - 1);
    for(int i = last; i > 0; i--) this->history[i] = this->history[i - 1];

    this->history[0].expression = expr;
    this->history[0].result     = result;
    if(this->history_count < kMaxHistory) this->history_count++;

    this->refreshHistoryList();
}

void CalculatorScene::clearHistory(){
    this->history_count = 0;
    this->refreshHistoryList();
}

void CalculatorScene::refreshHistoryList(){
    if(!this->history_list) return;

    this->history_list->clear();
    for(int i = 0; i < this->history_count; i++){
        ScrollListTools::Item item;
        item.text.assign(this->history[i].expression);
        item.text.append(" = ");
        item.text.append(this->history[i].result);
        this->history_list->add(item);
    }
}

void CalculatorScene::onEnter(){
    const Rect content = Scene::contentRect();

    // ---- 上部: [戻る][電卓|履歴] ----
    this->back_button = new Button(content.x + MARGIN, content.y + MARGIN, "戻る");
    this->back_button->setFontSize(FontFn::Small);
    this->back_button->setH(20);
    this->back_button->setOnPressEnd([](){ SceneFunctions::Pop(); });
    WidgetFunctions::Add(this->back_button);

    const Rect back_box = this->back_button->getLocalRect();
    this->top_row_h = back_box.h;

    const int tab_x = back_box.x + back_box.w + MARGIN;
    const int tab_w = content.x + content.w - MARGIN - tab_x;

    this->page_tab = new TabBar(tab_x, content.y + MARGIN, tab_w, this->top_row_h);
    this->page_tab->addTab("電卓");
    this->page_tab->addTab("履歴");
    this->page_tab->setSelected((int)this->page);
    this->page_tab->setOnChanged([this](int index){
        this->page = (Page)index;
        this->applyPage();
    });
    WidgetFunctions::Add(this->page_tab);

    const Rect body = this->bodyRect();

    // ---- 電卓ページ: 式 + 結果 + キーパッド ----
    const int expr_line_h = Label<PICO_STR_M>::GetLineHeight(FontFn::Normal);
    const int expr_h      = expr_line_h * 2; //長い式は2行まで見せる(それ以上は切り詰め)
    const int result_h    = Label<PICO_STR_M>::GetLineHeight(FontFn::Bigger);
    const int display_h   = expr_h + MARGIN + result_h + MARGIN;

    this->expr_label = new Label<PICO_STR_L>(body.x + MARGIN, body.y, "0");
    this->expr_label->setFontSize(FontFn::Normal);
    this->expr_label->setMaxWidth(body.w - MARGIN * 2);
    this->expr_label->setMaxHeight(expr_h);
    this->expr_label->setTextAlign(TextAlign::Right);
    WidgetFunctions::Add(this->expr_label);

    this->result_label = new Label<PICO_STR_M>(body.x + MARGIN, body.y + expr_h + MARGIN, "");
    this->result_label->setFontSize(FontFn::Bigger);
    this->result_label->setMaxWidth(body.w - MARGIN * 2);
    this->result_label->setMaxHeight(result_h);
    this->result_label->setTextAlign(TextAlign::Right);
    //色はrefreshDisplays()/commitCalculation()が状況に応じて都度設定する
    WidgetFunctions::Add(this->result_label);

    this->keypad = new CalculatorKeypad(body.x, body.y + display_h, body.w, body.h - display_h);
    this->keypad->setOnKey([this](const char* key){ this->onKey(key); });
    WidgetFunctions::Add(this->keypad);

    // ---- 履歴ページ: 消去ボタン + 一覧 ----
    this->history_clear_button = new Button(body.x + MARGIN, body.y + MARGIN, "履歴を消去");
    this->history_clear_button->setFontSize(FontFn::Small);
    this->history_clear_button->setH(20);
    this->history_clear_button->setOnPressEnd([this](){ this->clearHistory(); });
    WidgetFunctions::Add(this->history_clear_button);

    const Rect clear_box = this->history_clear_button->getLocalRect();
    const int list_y = body.y + MARGIN + clear_box.h + MARGIN;

    this->history_list = new ScrollList(body.x, (int16_t)list_y, body.w, (int16_t)(body.y + body.h - list_y));
    this->history_list->setFontSize(FontFn::Small);
    //2回タップで開くScrollListの流儀(FileExplorer/SearchDialogと同じ):
    //1回目は選択、選択済みの項目をもう一度タップしたら式を電卓ページへ読み戻す
    this->history_list->setOnSelectItem([this](int index, bool already_selected){
        if(!already_selected) return;
        if(index < 0 || index >= this->history_count) return;

        this->expression      = this->history[index].expression;
        this->last_was_result = false;
        this->refreshDisplays();

        this->page = Page::Calculator;
        if(this->page_tab) this->page_tab->setSelected((int)Page::Calculator);
        this->applyPage();
    });
    WidgetFunctions::Add(this->history_list);

    this->refreshHistoryList();
    this->applyPage();
    this->refreshDisplays();
}

void CalculatorScene::onExit(){
    this->back_button = nullptr;
    this->page_tab    = nullptr;

    this->expr_label   = nullptr;
    this->result_label = nullptr;
    this->keypad        = nullptr;

    this->history_clear_button = nullptr;
    this->history_list         = nullptr;
}
