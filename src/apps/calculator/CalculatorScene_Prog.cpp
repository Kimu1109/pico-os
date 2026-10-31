// CalculatorScene の「進数」ページ(プログラマ電卓)。ウィジェットの生成とキーの処理。
// 式の評価は Calc_Prog.hpp、4つの基数の表示は BaseView。
#include "apps/calculator/CalculatorScene.hpp"
#include "apps/calculator/Calc_Prog.hpp"
#include "gui/core/Widget_Functions.hpp"
#include "gui/core/Font_Functions.hpp"
#include "OS_Data.hpp"
#include "system/I18n_Functions.hpp"

#include <cstring>
#include <cstdio>

namespace {
    const char* ProgErrorMessage(CalcProg::Error e){
        switch(e){
            case CalcProg::Error::DivByZero:  return TR("0で割ることはできません");
            case CalcProg::Error::BadDigit:   return TR("この基数では使えない数字です");
            case CalcProg::Error::Overflow:   return TR("64bitに収まりません");
            case CalcProg::Error::TooComplex: return TR("式が複雑すぎます");
            case CalcProg::Error::Syntax:
            default:                          return TR("式が正しくありません");
        }
    }

    // DELで丸ごと消す塊(長いものから)。キーパッドが一度に足す単位と合わせてある
    const char* const kProgTokens[] = {
        " NAND ", " AND ", " NOR ", " XOR ", " OR ", " RoL ", " RoR ", " mod ", " << ", " >> ",
        "NOT ", "Ans", "×", "÷",
    };

    // "="の直後に押すと「Ansから続ける」キー(前に数が要る2項演算子)
    bool ProgContinuesFromAnswer(const char* key){
        if(key[0] == ' ') return true; // " AND " 等
        static const char* const kOps[] = { "+", "-", "×", "÷" };
        for(const char* op : kOps) if(strcmp(key, op) == 0) return true;
        return false;
    }

    void BitsLabel(int bits, char* out, size_t cap){ snprintf(out, cap, "%dbit", bits); }
}

void CalculatorScene::RemoveLastProgToken(Expr& e){
    const size_t len = e.length();
    for(const char* t : kProgTokens){
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

void CalculatorScene::refreshProg(){
    if(this->prog_expr_label){
        this->prog_expr_label->setText(this->prog_expr.empty() ? "0" : this->prog_expr.c_str());
    }
    if(!this->prog_view) return;

    //途中の式("FF AND")は評価できなくて当然なので、空欄にするだけ(誤りは"="のときだけ出す)
    if(this->prog_expr.empty()){
        this->prog_view->setValue(0, this->prog_bits);
        return;
    }
    Expr closed;
    CloseParens(this->prog_expr, closed);
    const CalcProg::Result r = CalcProg::Evaluate(closed.c_str(), this->prog_base, this->prog_bits, this->prog_ans);
    if(r.ok()) this->prog_view->setValue(r.value, this->prog_bits);
    else       this->prog_view->clearValue();
}

void CalculatorScene::commitProg(){
    if(this->prog_expr.empty()) return;
    Expr closed;
    CloseParens(this->prog_expr, closed);
    const CalcProg::Result r = CalcProg::Evaluate(closed.c_str(), this->prog_base, this->prog_bits, this->prog_ans);
    if(!r.ok()){
        if(this->prog_view) this->prog_view->setError(ProgErrorMessage(r.error));
        return;
    }
    this->prog_expr = closed;
    this->prog_ans = r.value;
    this->prog_last_was_result = true;
    if(this->prog_expr_label) this->prog_expr_label->setText(this->prog_expr.c_str());
    if(this->prog_view) this->prog_view->setValue(r.value, this->prog_bits);
}

void CalculatorScene::reexpressProg(int new_base, int new_bits){
    //今の式を評価できれば、その値を新しい基数・語長で書き直した式にする(途中の式は消す)
    if(!this->prog_expr.empty()){
        Expr closed;
        CloseParens(this->prog_expr, closed);
        const CalcProg::Result r = CalcProg::Evaluate(closed.c_str(), this->prog_base, this->prog_bits, this->prog_ans);
        this->prog_expr.clear();
        if(r.ok()){
            char buf[72];
            CalcProg::Format(r.value, new_base, new_bits, buf, sizeof(buf));
            this->prog_expr.assign(buf);
        }
    }
    this->prog_ans = CalcProg::Wrap((uint64_t)this->prog_ans, new_bits);
    this->prog_base = new_base;
    this->prog_bits = new_bits;
    this->prog_last_was_result = false;

    if(this->prog_keypad) this->prog_keypad->setDigitBase(new_base);
    if(this->prog_view) this->prog_view->setBase(new_base);
    if(this->prog_bits_button){
        char label[16];
        BitsLabel(new_bits, label, sizeof(label));
        this->prog_bits_button->setText(label);
    }
    this->refreshProg();
}

void CalculatorScene::setProgBase(int base){
    if(base == this->prog_base) return;
    this->reexpressProg(base, this->prog_bits);
}

void CalculatorScene::cycleProgBits(){
    int next = 64;
    switch(this->prog_bits){
        case 64: next = 32; break;
        case 32: next = 16; break;
        case 16: next = 8;  break;
        default: next = 64; break;
    }
    this->reexpressProg(this->prog_base, next);
}

void CalculatorScene::handleProgKey(const char* key){
    if(strcmp(key, "AC") == 0){
        this->prog_expr.clear();
        this->prog_last_was_result = false;
    }else if(strcmp(key, "DEL") == 0){
        if(this->prog_last_was_result) this->prog_expr.clear();
        else                            RemoveLastProgToken(this->prog_expr);
        this->prog_last_was_result = false;
    }else if(strcmp(key, "=") == 0){
        this->commitProg();
        return;
    }else{
        if(this->prog_last_was_result){
            this->prog_expr.clear();
            if(ProgContinuesFromAnswer(key)) this->prog_expr.append("Ans");
            this->prog_last_was_result = false;
        }
        AppendToken(this->prog_expr, key);
    }
    this->refreshProg();
}

bool CalculatorScene::onProgKey(const KeyInputFunctions::Event& ev){
    using K = KeyInputFunctions::Key;
    switch(ev.key){
        case K::Enter:     this->handleProgKey("="); return true;
        case K::Backspace: this->handleProgKey("DEL"); return true;
        case K::Escape:    this->handleProgKey("AC"); return true;
        //↑↓で基数を選ぶ(HEX→DEC→OCT→BINの並び)
        case K::Up:
        case K::Down: {
            const int row = BaseView::RowOfBase(this->prog_base) + (ev.key == K::Up ? -1 : 1);
            if(row >= 0 && row < BaseView::kRowCount) this->setProgBase(BaseView::BaseOfRow(row));
            return true;
        }
        default: break;
    }
    if(!ev.isPlainChar() || ev.cp >= 0x80) return false;

    char c = (char)ev.cp;
    if(c >= 'a' && c <= 'f') c = (char)(c - 'a' + 'A');
    const int digit = CalcProg::DigitValue(c);
    if(digit >= 0){
        if(digit >= this->prog_base) return true; //今の基数で使えない数字は捨てる
        const char key[2] = { c, '\0' };
        this->handleProgKey(key);
        return true;
    }
    //PCのキーボードで打ちやすい記号をキーパッドの演算子へ
    const char* key = nullptr;
    switch(c){
        case '&': key = " AND "; break;
        case '|': key = " OR ";  break;
        case '^': key = " XOR "; break;
        case '~': key = "NOT ";  break;
        case '<': key = " << ";  break;
        case '>': key = " >> ";  break;
        case '%': key = " mod "; break;
        case '*': key = "×"; break;
        case '/': key = "÷"; break;
        case '+': key = "+"; break;
        case '-': key = "-"; break;
        case '(': key = "("; break;
        case ')': key = ")"; break;
        case '=': key = "="; break;
        default: return false;
    }
    this->handleProgKey(key);
    return true;
}

void CalculatorScene::buildProgPage(const Rect& body){
    // ---- [64bit] 式 ----
    const int btn_h = 20 + Button::kFrameExtra;
    const int bits_w = 52;
    int y = body.y + MARGIN;

    char label[16];
    BitsLabel(this->prog_bits, label, sizeof(label));
    this->prog_bits_button = new Button(body.x + MARGIN, y, label);
    this->prog_bits_button->setFontSize(FontFn::Small);
    this->prog_bits_button->setW(bits_w);
    this->prog_bits_button->setH(btn_h);
    this->prog_bits_button->setOnPressEnd([this](){ this->cycleProgBits(); });
    WidgetFunctions::Add(this->prog_bits_button);

    const int expr_h = Label<PICO_STR_M>::GetLineHeight(FontFn::Small);
    const int expr_x = body.x + MARGIN + bits_w + MARGIN;
    this->prog_expr_label = new Label<PICO_STR_LL>(expr_x, y + (btn_h - expr_h) / 2, "0");
    this->prog_expr_label->setFontSize(FontFn::Small);
    this->prog_expr_label->setMaxWidth(body.x + body.w - MARGIN - expr_x);
    this->prog_expr_label->setMaxHeight(expr_h);
    this->prog_expr_label->setTextAlign(TextAlign::Right);
    this->prog_expr_label->setDisableAutoTextDecoration(true);
    WidgetFunctions::Add(this->prog_expr_label);
    y += btn_h + MARGIN;

    // ---- HEX/DEC/OCT/BIN ----
    const int view_h = BaseView::FittingHeight();
    this->prog_view = new BaseView(body.x + MARGIN, y, body.w - MARGIN * 2, view_h);
    this->prog_view->setBase(this->prog_base);
    this->prog_view->setOnSelectBase([this](int base){ this->setProgBase(base); });
    WidgetFunctions::Add(this->prog_view);
    y += view_h + MARGIN;

    // ---- キーパッド ----
    this->prog_keypad = new CalculatorKeypad(body.x, y, body.w, body.y + body.h - y, CalculatorKeypad::Layout::Programmer);
    this->prog_keypad->setDigitBase(this->prog_base);
    this->prog_keypad->setOnKey([this](const char* key){ this->handleProgKey(key); });
    WidgetFunctions::Add(this->prog_keypad);

    this->refreshProg();
}
