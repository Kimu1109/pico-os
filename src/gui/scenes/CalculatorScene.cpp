#include "gui/scenes/CalculatorScene.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Font_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "OS_Data.hpp"

#include <cstring>

namespace {
    // "="確定時に評価が失敗した場合のメッセージ。途中式のプレビュー失敗では出さない
    // (refreshDisplays()側は黙って空欄にするだけ)
    const char* ErrorMessage(CalcEval::Error e){
        switch(e){
            case CalcEval::Error::DivByZero:  return "0で割ることはできません";
            case CalcEval::Error::Domain:     return "計算できません(定義域の外)";
            case CalcEval::Error::TooComplex: return "式が複雑すぎます";
            case CalcEval::Error::Overflow:   return "桁があふれました";
            case CalcEval::Error::NoVariable: return "xはグラフでだけ使えます";
            case CalcEval::Error::Syntax:
            default:                          return "式が正しくありません";
        }
    }

    const char* AngleLabel(CalcEval::AngleMode a){
        switch(a){
            case CalcEval::AngleMode::Rad:  return "RAD";
            case CalcEval::AngleMode::Grad: return "GRA";
            default:                        return "DEG";
        }
    }

    // "="の直後に押すと「Ansから続ける」キー(前に数が要る演算子)
    bool ContinuesFromAnswer(const char* key){
        static const char* const kOps[] = { "+", "-", "×", "÷", "^", "^2", "^3", "^(-1)", "!", "%", "C", "P", "mod" };
        for(const char* op : kOps) if(strcmp(key, op) == 0) return true;
        return false;
    }

    // DELで丸ごと消す塊(長いものから)。キーパッドが一度に足す単位と合わせてある
    const char* const kTokens[] = {
        "asinh(", "acosh(", "atanh(", "^(-1)",
        "asin(", "acos(", "atan(", "sinh(", "cosh(", "tanh(", "cbrt(", "root(",
        "sin(", "cos(", "tan(", "log(", "abs(", "10^(",
        "ln(", "e^(", "Ans", "mod", "√(",
    };
}

// ---------------------------------------------------------------------------
// 式の組み立て
// ---------------------------------------------------------------------------

int CalculatorScene::OpenParenCount(const char* expr){
    int depth = 0;
    for(const char* p = expr; *p; p++){
        if(*p == '(') depth++;
        else if(*p == ')') depth--;
    }
    return depth;
}

void CalculatorScene::CloseParens(const Expr& in, Expr& out){
    out = in;
    for(int n = OpenParenCount(in.c_str()); n > 0; n--){
        if(!out.append(")")) break;
    }
}

void CalculatorScene::RemoveLastToken(Expr& e){
    const size_t len = e.length();
    for(const char* t : kTokens){
        const size_t tl = strlen(t);
        if(tl <= len && strcmp(e.c_str() + len - tl, t) == 0){
            Expr cut;
            cut.assign(e.c_str(), len - tl);
            e = cut;
            return;
        }
    }
    e.removeLastChar();
}

bool CalculatorScene::AppendToken(Expr& e, const char* token){
    //")"は開いている"("が無ければ捨てる(壊れた式を作らせない)
    if(strcmp(token, ")") == 0 && OpenParenCount(e.c_str()) <= 0) return false;
    //桁あふれ。切り詰めて別の式になるくらいなら無視する
    if(e.length() + strlen(token) > e.capacity()) return false;
    e.append(token);
    return true;
}

void CalculatorScene::FormatNumber(double v, FixedString<PICO_STR_M>& out){
    // %.10gは有効数字10桁で末尾の0を自動的に落とすので、電卓の結果表示としてちょうどよい
    char buf[32];
    snprintf(buf, sizeof(buf), "%.10g", v);
    // 指数表記 "1.5e+20" / "2e-07" をEXPキーと同じ "1.5E20" / "2E-7" にする(そのまま式へ戻せる)
    out.clear();
    for(const char* p = buf; *p; p++){
        if(*p != 'e'){ out.append(*p); continue; }
        out.append('E');
        p++;
        if(*p == '-'){ out.append('-'); p++; }
        else if(*p == '+'){ p++; }
        while(*p == '0' && p[1] >= '0' && p[1] <= '9') p++; // 先頭の0を落とす
        p--;
    }
}

// ---------------------------------------------------------------------------

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

CalcEval::Context CalculatorScene::context() const {
    CalcEval::Context c;
    c.angle = this->angle;
    c.ans = this->ans;
    return c;
}

void CalculatorScene::applyPage(){
    const bool is_calc  = (this->page == Page::Calculator);
    const bool is_graph = (this->page == Page::Graph);
    const bool is_hist  = (this->page == Page::History);
    const bool g_edit = is_graph && !this->graph_plotting;
    const bool g_plot = is_graph && this->graph_plotting;

    if(this->expr_label)   this->expr_label->setVisible(is_calc);
    if(this->result_label) this->result_label->setVisible(is_calc);
    if(this->keypad)       this->keypad->setVisible(is_calc);

    if(this->graph_list)   this->graph_list->setVisible(g_edit);
    if(this->graph_status) this->graph_status->setVisible(g_edit);
    if(this->graph_keypad) this->graph_keypad->setVisible(g_edit);

    if(this->graph_view)           this->graph_view->setVisible(g_plot);
    if(this->graph_edit_button)    this->graph_edit_button->setVisible(g_plot);
    if(this->graph_zoomin_button)  this->graph_zoomin_button->setVisible(g_plot);
    if(this->graph_zoomout_button) this->graph_zoomout_button->setVisible(g_plot);
    if(this->graph_reset_button)   this->graph_reset_button->setVisible(g_plot);

    if(this->history_clear_button) this->history_clear_button->setVisible(is_hist);
    if(this->history_list)         this->history_list->setVisible(is_hist);
}

void CalculatorScene::cycleAngle(){
    auto next = [](CalcEval::AngleMode a){
        switch(a){
            case CalcEval::AngleMode::Deg: return CalcEval::AngleMode::Rad;
            case CalcEval::AngleMode::Rad: return CalcEval::AngleMode::Grad;
            default:                       return CalcEval::AngleMode::Deg;
        }
    };

    if(this->page == Page::Graph){
        this->graph_angle = next(this->graph_angle);
        if(this->graph_keypad) this->graph_keypad->setAngleLabel(AngleLabel(this->graph_angle));
        if(this->graph_view)   this->graph_view->setAngleMode(this->graph_angle);
    }else{
        this->angle = next(this->angle);
        if(this->keypad) this->keypad->setAngleLabel(AngleLabel(this->angle));
    }
    this->refreshGraphStatus();
}

void CalculatorScene::setResultText(const char* text, int8_t color, bool is_error){
    if(!this->result_label) return;
    this->result_label->setTextColor(color);

    //数値は大きい字で出し、収まらない長さ(指数表記等)とエラー文言だけ字を小さくする。
    //エラー文言はBigだと画面幅を超えるので常にSmall
    FontFn::FontSize size = FontFn::Big;
    if(is_error){
        size = FontFn::Small;
    }else{
        const int max_w = this->result_label->getMaxWidth();
        FontFn::SetBig();
        if(OSData::frame->textWidth(text) > max_w){
            size = FontFn::Normal;
            FontFn::SetNormal();
            if(OSData::frame->textWidth(text) > max_w) size = FontFn::Small;
        }
        FontFn::SetDefault();
    }
    this->result_label->setFontSize(size);
    this->result_label->setText(text);
}

void CalculatorScene::refreshDisplays(){
    if(this->expr_label){
        this->expr_label->setText(this->expression.empty() ? "0" : this->expression.c_str());
    }

    if(!this->result_label) return;

    //空、または不完全な式(例: "3+")は評価に失敗して当然なので、エラー扱いにせず
    //黙って前のプレビューを消すだけにする。エラーとして出すのは"="を押した時だけ。
    //入力中のプレビューは「まだ確定していない」ことが分かるよう灰色にする
    if(this->expression.empty()){
        this->setResultText("", PICO_DARKGREY, false);
        return;
    }

    Expr closed;
    CloseParens(this->expression, closed);
    const CalcEval::Result r = CalcEval::Evaluate(closed.c_str(), this->context());
    if(!r.ok()){
        this->setResultText("", PICO_DARKGREY, false);
        return;
    }

    FixedString<PICO_STR_M> text;
    FormatNumber(r.value, text);
    this->setResultText(text.c_str(), PICO_DARKGREY, false);
}

void CalculatorScene::commitCalculation(){
    if(this->expression.empty()) return;

    //閉じ忘れた括弧は自動で閉じる(一般的な関数電卓と同じ)
    Expr closed;
    CloseParens(this->expression, closed);

    const CalcEval::Result r = CalcEval::Evaluate(closed.c_str(), this->context());
    if(!r.ok()){
        //構文エラー等は結果ではないので、灰色のプレビューとは別に赤で区別する
        //(ACキーの赤字と同じく、状態否定にPICO_REDを使う既存の慣習に揃える)
        this->setResultText(ErrorMessage(r.error), PICO_RED, true);
        LOG_SYS_WARN("電卓: 式の評価に失敗しました (%s)", this->expression.c_str());
        return;
    }

    FixedString<PICO_STR_M> text;
    FormatNumber(r.value, text);

    this->expression = closed;
    this->pushHistory(this->expression, text);

    if(this->expr_label) this->expr_label->setText(this->expression.c_str());
    //"="で確定した答えは灰色のプレビューと区別できるよう黒ではっきり出す
    this->setResultText(text.c_str(), PICO_FORECOLOR, false);

    this->ans = r.value;
    this->last_was_result = true;
    if(this->graph_view) this->graph_view->setAns(this->ans);
}

void CalculatorScene::handleKey(const char* key){
    if(strcmp(key, "DRG") == 0){
        this->cycleAngle();
        if(this->page == Page::Calculator) this->refreshDisplays();
        return;
    }
    if(this->page == Page::Graph){
        this->handleGraphKey(key);
        return;
    }
    if(this->page != Page::Calculator) return;

    if(strcmp(key, "AC") == 0){
        this->expression.clear();
        this->last_was_result = false;
        this->refreshDisplays();
        return;
    }

    if(strcmp(key, "DEL") == 0){
        if(this->last_was_result){
            //確定結果を見ている状態でのDELは、新しい式を打ち直す合図として扱う
            this->expression.clear();
            this->last_was_result = false;
        }else{
            RemoveLastToken(this->expression);
        }
        this->refreshDisplays();
        return;
    }

    if(strcmp(key, "=") == 0){
        this->commitCalculation();
        return;
    }

    if(this->last_was_result){
        //演算子なら直前の答え(Ans)から続けて計算する。数字等なら新しい式
        this->expression.clear();
        if(ContinuesFromAnswer(key)) this->expression.append("Ans");
        this->last_was_result = false;
    }

    AppendToken(this->expression, key);
    this->refreshDisplays();
}

void CalculatorScene::handleGraphKey(const char* key){
    if(this->graph_plotting) return;
    Expr& e = this->graph_exprs[this->graph_sel];

    if(strcmp(key, "AC") == 0){
        e.clear();
    }else if(strcmp(key, "DEL") == 0){
        RemoveLastToken(e);
    }else if(strcmp(key, "=") == 0){
        //[描画]: 閉じ忘れた括弧を閉じてからグラフを見せる
        for(Expr& g : this->graph_exprs){
            Expr closed;
            CloseParens(g, closed);
            g = closed;
        }
        this->refreshGraphList();
        this->showGraph(true);
        return;
    }else{
        AppendToken(e, key);
    }
    this->refreshGraphList();
}

void CalculatorScene::refreshGraphList(){
    if(this->graph_list){
        this->graph_list->clear();
        for(int i = 0; i < kGraphFns; i++){
            ScrollListTools::Item item;
            item.text.appendFormat("y%d=", i + 1);
            item.text.append(this->graph_exprs[i].c_str());
            item.color = GraphView::kColors[i];
            this->graph_list->add(item);
        }
        this->graph_list->setSelectedIndex(this->graph_sel);
    }
    this->refreshGraphStatus();
}

void CalculatorScene::refreshGraphStatus(){
    if(!this->graph_status) return;

    //選んでいる式を x=1 で試しに評価して、明らかな誤りだけ知らせる
    //(x=1で定義されないだけ(1/(x-1)等)の式はグラフにできるので、定義域の外は咎めない)
    FixedString<PICO_STR_M> msg;
    const Expr& e = this->graph_exprs[this->graph_sel];
    int8_t color = PICO_DARKGREY;
    if(e.empty()){
        msg.appendFormat("y%dの式を入力 (x キーで変数)", this->graph_sel + 1);
    }else{
        Expr closed;
        CloseParens(e, closed);
        CalcEval::Context c = this->context();
        c.angle = this->graph_angle;
        c.has_x = true;
        c.x = 1.0;
        const CalcEval::Result r = CalcEval::Evaluate(closed.c_str(), c);
        if(r.error == CalcEval::Error::Syntax || r.error == CalcEval::Error::TooComplex){
            msg.assign(ErrorMessage(r.error));
            color = PICO_RED;
        }else{
            msg.appendFormat("[描画]でグラフ (%s)", AngleLabel(this->graph_angle));
        }
    }
    this->graph_status->setTextColor(color);
    this->graph_status->setText(msg);
}

void CalculatorScene::showGraph(bool plotting){
    this->graph_plotting = plotting;
    if(plotting && this->graph_view) this->graph_view->functionsChanged();
    this->applyPage();
}

void CalculatorScene::pushHistory(const Expr& expr, const FixedString<PICO_STR_M>& result){
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

bool CalculatorScene::onKey(const KeyInputFunctions::Event& ev){
    using K = KeyInputFunctions::Key;

    //グラフを見ている間: 矢印で動かす、+/-で拡大縮小、Esc/Enterで式へ戻る
    if(this->page == Page::Graph && this->graph_plotting){
        if(!this->graph_view) return false;
        switch(ev.key){
            case K::Left:  this->graph_view->pan(-0.1, 0.0); return true;
            case K::Right: this->graph_view->pan( 0.1, 0.0); return true;
            case K::Up:    this->graph_view->pan(0.0,  0.1); return true;
            case K::Down:  this->graph_view->pan(0.0, -0.1); return true;
            case K::Escape:
            case K::Enter: this->showGraph(false); return true;
            default: break;
        }
        if(ev.isPlainChar() && ev.cp == '+'){ this->graph_view->zoom(0.5); return true; }
        if(ev.isPlainChar() && ev.cp == '-'){ this->graph_view->zoom(2.0); return true; }
        return false;
    }

    //式を入れている間の↑↓: 編集するy1〜y3を選ぶ
    if(this->page == Page::Graph && (ev.key == K::Up || ev.key == K::Down)){
        const int next = this->graph_sel + (ev.key == K::Up ? -1 : 1);
        if(next >= 0 && next < kGraphFns){
            this->graph_sel = next;
            if(this->graph_list) this->graph_list->setSelectedIndex(next);
            this->refreshGraphStatus();
        }
        return true;
    }

    const bool typing = (this->page == Page::Calculator) || (this->page == Page::Graph);
    if(!typing) return false;

    switch(ev.key){
        case K::Enter:     this->handleKey("="); return true;
        case K::Backspace: this->handleKey("DEL"); return true;
        case K::Escape:      this->handleKey("AC"); return true;
        default: break;
    }
    if(!ev.isPlainChar() || ev.cp >= 0x80) return false;

    //PCのキーボードで打ちやすい記号を電卓の記号へ
    const char c = (char)ev.cp;
    char buf[2] = { c, '\0' };
    const char* key = buf;
    if(c == '*') key = "×";
    else if(c == '/') key = "÷";
    else if(c == '=') key = "=";
    this->handleKey(key);
    return true;
}

void CalculatorScene::onEnter(){
    const Rect content = Scene::contentRect();

    // ---- 上部: [戻る][電卓|グラフ|履歴] ----
    this->back_button = new Button(content.x + MARGIN, content.y + MARGIN, "戻る");
    this->back_button->setFontSize(FontFn::Small);
    this->back_button->setH(20 + Button::kFrameExtra);
    this->back_button->setOnPressEnd([](){ SceneFunctions::Pop(); });
    WidgetFunctions::Add(this->back_button);

    const Rect back_box = this->back_button->getLocalRect();
    this->top_row_h = back_box.h;

    const int tab_x = back_box.x + back_box.w + MARGIN;
    const int tab_w = content.x + content.w - MARGIN - tab_x;

    this->page_tab = new TabBar(tab_x, content.y + MARGIN, tab_w, this->top_row_h);
    this->page_tab->addTab("電卓");
    this->page_tab->addTab("グラフ");
    this->page_tab->addTab("履歴");
    this->page_tab->setSelected((int)this->page);
    this->page_tab->setOnChanged([this](int index){
        this->page = (Page)index;
        this->applyPage();
    });
    WidgetFunctions::Add(this->page_tab);

    const Rect body = this->bodyRect();
    const char* angle_label = AngleLabel(this->angle);
    const char* graph_angle_label = AngleLabel(this->graph_angle);

    // ---- 電卓ページ: 式 + 結果 + キーパッド ----
    const int expr_line_h = Label<PICO_STR_M>::GetLineHeight(FontFn::Small);
    const int expr_h      = expr_line_h * 2; //長い式は2行まで見せる(それ以上は切り詰め)
    const int result_h    = Label<PICO_STR_M>::GetLineHeight(FontFn::Big);
    const int display_h   = expr_h + MARGIN + result_h + MARGIN;

    this->expr_label = new Label<PICO_STR_LL>(body.x + MARGIN, body.y, "0");
    this->expr_label->setFontSize(FontFn::Small);
    this->expr_label->setMaxWidth(body.w - MARGIN * 2);
    this->expr_label->setMaxHeight(expr_h);
    this->expr_label->setTextAlign(TextAlign::Right);
    //式の"*"や"_"等がマークアップとして消えないように
    this->expr_label->setDisableAutoTextDecoration(true);
    WidgetFunctions::Add(this->expr_label);

    this->result_label = new Label<PICO_STR_M>(body.x + MARGIN, body.y + expr_h + MARGIN, "");
    this->result_label->setFontSize(FontFn::Big);
    this->result_label->setMaxWidth(body.w - MARGIN * 2);
    this->result_label->setMaxHeight(result_h);
    this->result_label->setTextAlign(TextAlign::Right);
    this->result_label->setDisableAutoTextDecoration(true);
    //色はrefreshDisplays()/commitCalculation()が状況に応じて都度設定する
    WidgetFunctions::Add(this->result_label);

    this->keypad = new CalculatorKeypad(body.x, body.y + display_h, body.w, body.h - display_h);
    this->keypad->setAngleLabel(angle_label);
    this->keypad->setOnKey([this](const char* key){ this->handleKey(key); });
    WidgetFunctions::Add(this->keypad);

    // ---- グラフページ(式を入れる): y1〜y3 + 状態 + キーパッド ----
    //ScrollListの1行は実際のフォントの高さ+2px(GetFontSize()の16より大きい)ので、実測して3行ぶんちょうどにする
    FontFn::SetSmall();
    const int list_row_h = OSData::frame->fontHeight() + 2;
    FontFn::SetDefault();
    const int list_h = kGraphFns * list_row_h + 1;
    this->graph_list = new ScrollList(body.x, body.y, body.w, (int16_t)list_h, kGraphFns);
    this->graph_list->setFontSize(FontFn::Small);
    //1回のタップで編集する式を選ぶ
    this->graph_list->setOnSelectItem([this](int index, bool){
        if(index < 0 || index >= kGraphFns) return;
        this->graph_sel = index;
        this->refreshGraphStatus();
    });
    WidgetFunctions::Add(this->graph_list);

    const int status_y = body.y + list_h + MARGIN;
    const int status_h = Label<PICO_STR_M>::GetLineHeight(FontFn::Small);
    this->graph_status = new Label<PICO_STR_M>(body.x + MARGIN, status_y, "");
    this->graph_status->setFontSize(FontFn::Small);
    this->graph_status->setMaxWidth(body.w - MARGIN * 2);
    this->graph_status->setMaxHeight(status_h);
    this->graph_status->setDisableAutoTextDecoration(true);
    WidgetFunctions::Add(this->graph_status);

    const int gk_y = status_y + status_h + MARGIN;
    this->graph_keypad = new CalculatorKeypad(body.x, gk_y, body.w, body.y + body.h - gk_y);
    this->graph_keypad->setGraphMode(true);
    this->graph_keypad->setAngleLabel(graph_angle_label);
    this->graph_keypad->setOnKey([this](const char* key){ this->handleKey(key); });
    WidgetFunctions::Add(this->graph_keypad);

    // ---- グラフページ(グラフを見る): グラフ + [式][拡大][縮小][初期化] ----
    const int btn_h = 20;
    const int btn_y = body.y + body.h - MARGIN - btn_h;
    this->graph_view = new GraphView(body.x, body.y, body.w, btn_y - MARGIN - body.y);
    for(int i = 0; i < kGraphFns; i++) this->graph_view->setFunction(i, this->graph_exprs[i].c_str());
    this->graph_view->setAngleMode(this->graph_angle);
    this->graph_view->setAns(this->ans);
    WidgetFunctions::Add(this->graph_view);

    struct Spec { Button** slot; const char* text; };
    const Spec specs[] = {
        { &this->graph_edit_button,    "式" },
        { &this->graph_zoomin_button,  "拡大" },
        { &this->graph_zoomout_button, "縮小" },
        { &this->graph_reset_button,   "初期化" },
    };
    const int n_btn = sizeof(specs) / sizeof(specs[0]);
    const int btn_w = (body.w - MARGIN * (n_btn + 1)) / n_btn;
    for(int i = 0; i < n_btn; i++){
        Button* b = new Button(body.x + MARGIN + i * (btn_w + MARGIN), btn_y, specs[i].text);
        b->setFontSize(FontFn::Small);
        b->setW(btn_w + Button::kFrameExtra);
        b->setH(btn_h + Button::kFrameExtra);
        WidgetFunctions::Add(b);
        *specs[i].slot = b;
    }
    this->graph_edit_button->setOnPressEnd([this](){ this->showGraph(false); });
    this->graph_zoomin_button->setOnPressEnd([this](){ if(this->graph_view) this->graph_view->zoom(0.5); });
    this->graph_zoomout_button->setOnPressEnd([this](){ if(this->graph_view) this->graph_view->zoom(2.0); });
    this->graph_reset_button->setOnPressEnd([this](){
        if(!this->graph_view) return;
        this->graph_view->resetView();
        this->graph_view->clearTrace();
    });

    // ---- 履歴ページ: 消去ボタン + 一覧 ----
    this->history_clear_button = new Button(body.x + MARGIN, body.y + MARGIN, "履歴を消去");
    this->history_clear_button->setFontSize(FontFn::Small);
    this->history_clear_button->setH(20 + Button::kFrameExtra);
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
    this->refreshGraphList();
    this->applyPage();
    this->refreshDisplays();
}

void CalculatorScene::onExit(){
    this->back_button = nullptr;
    this->page_tab    = nullptr;

    this->expr_label   = nullptr;
    this->result_label = nullptr;
    this->keypad       = nullptr;

    this->graph_list    = nullptr;
    this->graph_status  = nullptr;
    this->graph_keypad  = nullptr;
    this->graph_view    = nullptr;
    this->graph_edit_button    = nullptr;
    this->graph_zoomin_button  = nullptr;
    this->graph_zoomout_button = nullptr;
    this->graph_reset_button   = nullptr;

    this->history_clear_button = nullptr;
    this->history_list         = nullptr;
}
