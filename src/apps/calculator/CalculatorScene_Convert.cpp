// CalculatorScene の「変換」ページ(単位・通貨)。ウィジェットの生成とキーの処理。
// 単位の表は Calc_Units.hpp、通貨のレートは Calc_Currency。
#include "apps/calculator/CalculatorScene.hpp"
#include "apps/calculator/Calc_Units.hpp"
#include "gui/core/Widget_Functions.hpp"
#include "gui/core/Font_Functions.hpp"
#include "gui/widgets/dialogs/PickerDialog.hpp"
#include "hal/Network_Functions.hpp"
#include "OS_Data.hpp"

#include <cstring>
#include <cstdio>

namespace {
    // 変換の値の桁の上限(%.10gで表せる桁より長く打っても意味が無い)
    constexpr size_t kMaxConvInput = 18;

    // 数値は大きい字で出し、収まらなければ字を小さくする(電卓ページの結果欄と同じ)
    void SetFittedText(Label<PICO_STR_M>* label, const char* text, int8_t color){
        if(!label) return;
        label->setTextColor(color);
        const int max_w = label->getMaxWidth();
        FontFn::FontSize size = FontFn::Big;
        FontFn::SetBig();
        if(OSData::frame->textWidth(text) > max_w){
            size = FontFn::Normal;
            FontFn::SetNormal();
            if(OSData::frame->textWidth(text) > max_w) size = FontFn::Small;
        }
        FontFn::SetDefault();
        label->setFontSize(size);
        label->setText(text);
    }

    void CopyCode(char (&dst)[4], const char* src){
        strncpy(dst, src ? src : "", 3);
        dst[3] = '\0';
    }
}

// ---------------------------------------------------------------------------
// 値の入力
// ---------------------------------------------------------------------------

bool CalculatorScene::AppendConvInput(FixedString<PICO_STR_S>& in, const char* key){
    const char* e = strchr(in.c_str(), 'E');

    if(strcmp(key, "NEG") == 0){
        //"E"の直後なら指数の符号。それ以外は値全体の符号を入れ替える
        if(!in.empty() && in.c_str()[in.length() - 1] == 'E') return in.append("-");
        if(in.c_str()[0] == '-'){
            FixedString<PICO_STR_S> cut;
            cut.assign(in.c_str() + 1);
            in = cut;
            return true;
        }
        if(in.length() + 1 > kMaxConvInput) return false;
        FixedString<PICO_STR_S> neg;
        neg.assign("-");
        neg.append(in.c_str());
        in = neg;
        return true;
    }

    if(in.length() + strlen(key) > kMaxConvInput) return false;

    if(strcmp(key, ".") == 0){
        if(e || strchr(in.c_str(), '.')) return false; //小数点は1つ。指数の中には置かない
        const bool no_digit = in.empty() || strcmp(in.c_str(), "-") == 0;
        return in.append(no_digit ? "0." : ".");
    }
    if(strcmp(key, "E") == 0){
        //指数は数字の後に1つだけ
        if(e || in.empty()) return false;
        const char last = in.c_str()[in.length() - 1];
        if(last < '0' || last > '9') return false;
        return in.append("E");
    }
    if(key[0] >= '0' && key[0] <= '9' && key[1] == '\0'){
        //先頭の"0"は打った数字で置き換える("05"にしない)
        if(strcmp(in.c_str(), "0") == 0){ in.assign(key); return true; }
        if(strcmp(in.c_str(), "-0") == 0){ in.assign("-"); return in.append(key); }
        return in.append(key);
    }
    return false;
}

// ---------------------------------------------------------------------------
// 単位/通貨の選択
// ---------------------------------------------------------------------------

bool CalculatorScene::isCurrency() const {
    return this->conv_cat >= CalcUnits::kCategoryCount;
}

int CalculatorScene::convUnitCount() const {
    if(this->isCurrency()) return this->currency.count();
    return CalcUnits::kCategories[this->conv_cat].count;
}

const char* CalculatorScene::convUnitSymbol(int i) const {
    if(this->isCurrency()) return (i >= 0) ? this->currency.codeAt(i) : "---";
    const CalcUnits::Category& c = CalcUnits::kCategories[this->conv_cat];
    return (i >= 0 && i < c.count) ? c.units[i].symbol : "";
}

int CalculatorScene::convFromIndex() const {
    return this->isCurrency() ? this->currency.find(this->conv_from_code) : this->conv_from;
}

int CalculatorScene::convToIndex() const {
    return this->isCurrency() ? this->currency.find(this->conv_to_code) : this->conv_to;
}

void CalculatorScene::setConvUnit(bool from, int index){
    if(index < 0 || index >= this->convUnitCount()) return;
    if(this->isCurrency()){
        CopyCode(from ? this->conv_from_code : this->conv_to_code, this->currency.codeAt(index));
    }else if(from){
        this->conv_from = index;
    }else{
        this->conv_to = index;
    }
    this->refreshConv();
}

void CalculatorScene::selectConvCategory(int cat){
    if(cat < 0 || cat > CalcUnits::kCategoryCount) return;
    this->conv_cat = cat;
    if(!this->isCurrency()){
        //種類を変えたら、その種類のよく使う組み合わせから
        this->conv_from = CalcUnits::kCategories[cat].default_from;
        this->conv_to   = CalcUnits::kCategories[cat].default_to;
    }
    this->applyPage(); //[更新]の出し入れ
    this->refreshConv();
}

void CalculatorScene::openConvCategoryPicker(){
    auto* dialog = new PickerDialog(PickerDialog::Mode::Choice, "変換の種類", "", "キャンセル");
    if(!dialog) return;
    for(int i = 0; i < CalcUnits::kCategoryCount; i++) dialog->addChoice(CalcUnits::kCategories[i].name);
    dialog->addChoice("通貨(為替)");
    dialog->layout();
    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    dialog->setOnClosed([this, dialog](bool is_ok){
        const int sel = dialog->getSelectedIndex();
        WidgetFunctions::DestroyLater(dialog);
        if(is_ok && sel >= 0) this->selectConvCategory(sel);
    });
}

void CalculatorScene::openConvUnitPicker(bool from){
    const int n = this->convUnitCount();
    if(n <= 0){
        //通貨のレートがまだ無い
        this->refreshConvStatus();
        return;
    }
    auto* dialog = new PickerDialog(PickerDialog::Mode::Choice, from ? "変換元" : "変換先", "", "キャンセル");
    if(!dialog) return;
    for(int i = 0; i < n; i++){
        char buf[PICO_STR_M];
        if(this->isCurrency()){
            const char* code = this->currency.codeAt(i);
            const char* name = CurrencyRates::NameOf(code);
            snprintf(buf, sizeof(buf), "%s %s", code, name ? name : "");
        }else{
            const CalcUnits::Unit& u = CalcUnits::kCategories[this->conv_cat].units[i];
            if(strcmp(u.symbol, u.name) == 0) snprintf(buf, sizeof(buf), "%s", u.name);
            else                              snprintf(buf, sizeof(buf), "%s %s", u.symbol, u.name);
        }
        dialog->addChoice(buf);
    }
    dialog->layout();
    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    dialog->setOnClosed([this, dialog, from](bool is_ok){
        const int sel = dialog->getSelectedIndex();
        WidgetFunctions::DestroyLater(dialog);
        if(is_ok && sel >= 0) this->setConvUnit(from, sel);
    });
}

void CalculatorScene::startCurrencyUpdate(){
    if(this->currency.busy()) return;
    if(!NetworkFunctions::IsConnected()){
        if(this->conv_status){
            this->conv_status->setTextColor(PICO_RED);
            this->conv_status->setText("Wi-Fiに繋がっていません");
        }
        return;
    }
    this->currency.beginFetch();
    this->currency_state = this->currency.state();
    this->refreshConvStatus();
}

// ---------------------------------------------------------------------------
// 表示
// ---------------------------------------------------------------------------

void CalculatorScene::refreshConv(){
    const int from = this->convFromIndex();
    const int to   = this->convToIndex();

    if(this->conv_cat_button){
        char buf[PICO_STR_M];
        snprintf(buf, sizeof(buf), "種類: %s",
                 this->isCurrency() ? "通貨(為替)" : CalcUnits::kCategories[this->conv_cat].name);
        this->conv_cat_button->setText(buf);
    }
    if(this->conv_from_button) this->conv_from_button->setText(this->convUnitSymbol(from));
    if(this->conv_to_button)   this->conv_to_button->setText(this->convUnitSymbol(to));

    SetFittedText(this->conv_from_label, this->conv_input.empty() ? "0" : this->conv_input.c_str(), PICO_FORECOLOR);

    //打ちかけ("1E" "-")は数にならないので、換算した値を空にしておく
    const char* in = this->conv_input.empty() ? "0" : this->conv_input.c_str();
    const CalcEval::Result r = CalcEval::Evaluate(in);
    double out = 0.0;
    bool ok = r.ok() && from >= 0 && to >= 0;
    if(ok){
        ok = this->isCurrency() ? this->currency.convert(from, to, r.value, out)
                                : CalcUnits::Convert(this->conv_cat, from, to, r.value, out);
    }
    FixedString<PICO_STR_M> text;
    if(ok) FormatNumber(out == 0.0 ? 0.0 : out, text);
    SetFittedText(this->conv_to_label, text.c_str(), PICO_FORECOLOR);

    this->refreshConvStatus();
}

void CalculatorScene::refreshConvStatus(){
    if(!this->conv_status) return;
    const int from = this->convFromIndex();
    const int to   = this->convToIndex();

    FixedString<PICO_STR_L> msg;
    int8_t color = PICO_DARKGREY;
    if(this->isCurrency()){
        if(this->currency.busy()){
            msg.assign(this->currency.message());
        }else if(this->currency.state() == CurrencyRates::State::Failed){
            msg.assign(this->currency.message());
            color = PICO_RED;
        }else if(!this->currency.hasRates()){
            msg.assign("レート未取得。[更新]で取得");
        }else{
            double one = 0.0;
            FixedString<PICO_STR_M> num;
            if(from >= 0 && to >= 0 && this->currency.convert(from, to, 1.0, one)) FormatNumber(one, num);
            msg.appendFormat("1 %s = %s %s\n%s のレート", this->convUnitSymbol(from), num.c_str(),
                             this->convUnitSymbol(to), this->currency.date());
        }
    }else{
        double one = 0.0;
        FixedString<PICO_STR_M> num;
        if(CalcUnits::Convert(this->conv_cat, from, to, 1.0, one)) FormatNumber(one, num);
        msg.appendFormat("1 %s = %s %s", this->convUnitSymbol(from), num.c_str(), this->convUnitSymbol(to));
    }
    this->conv_status->setTextColor(color);
    this->conv_status->setText(msg);
}

void CalculatorScene::handleConvKey(const char* key){
    if(strcmp(key, "AC") == 0){
        this->conv_input.clear();
    }else if(strcmp(key, "DEL") == 0){
        this->conv_input.removeLastChar();
    }else if(strcmp(key, "SWAP") == 0){
        if(this->isCurrency()){
            char tmp[4];
            CopyCode(tmp, this->conv_from_code);
            CopyCode(this->conv_from_code, this->conv_to_code);
            CopyCode(this->conv_to_code, tmp);
        }else{
            const int tmp = this->conv_from;
            this->conv_from = this->conv_to;
            this->conv_to = tmp;
        }
    }else{
        AppendConvInput(this->conv_input, key);
    }
    this->refreshConv();
}

bool CalculatorScene::onConvKey(const KeyInputFunctions::Event& ev){
    using K = KeyInputFunctions::Key;
    switch(ev.key){
        case K::Backspace: this->handleConvKey("DEL"); return true;
        case K::Escape:    this->handleConvKey("AC"); return true;
        case K::Tab:       this->handleConvKey("SWAP"); return true;
        default: break;
    }
    if(!ev.isPlainChar() || ev.cp >= 0x80) return false;
    const char c = (char)ev.cp;
    if(c >= '0' && c <= '9'){
        const char key[2] = { c, '\0' };
        this->handleConvKey(key);
        return true;
    }
    if(c == '.'){ this->handleConvKey("."); return true; }
    if(c == '-'){ this->handleConvKey("NEG"); return true; }
    if(c == 'e' || c == 'E'){ this->handleConvKey("E"); return true; }
    return false;
}

// ---------------------------------------------------------------------------
// 生成
// ---------------------------------------------------------------------------

void CalculatorScene::buildConvertPage(const Rect& body){
    const int btn_h = 20 + Button::kFrameExtra;

    // ---- 種類 ----
    int y = body.y + MARGIN;
    this->conv_cat_button = new Button(body.x + MARGIN, y, "種類");
    this->conv_cat_button->setFontSize(FontFn::Small);
    this->conv_cat_button->setW(body.w - MARGIN * 2);
    this->conv_cat_button->setH(btn_h);
    this->conv_cat_button->setOnPressEnd([this](){ this->openConvCategoryPicker(); });
    WidgetFunctions::Add(this->conv_cat_button);
    y += btn_h + MARGIN;

    // ---- [単位] 値 を2行(変換元・変換先) ----
    const int value_h = Label<PICO_STR_M>::GetLineHeight(FontFn::Big);
    const int row_h = value_h > btn_h ? value_h : btn_h;
    const int unit_w = 64;
    const int value_x = body.x + MARGIN + unit_w + MARGIN;
    const int value_w = body.x + body.w - MARGIN - value_x;

    Button** const unit_slots[2] = { &this->conv_from_button, &this->conv_to_button };
    Label<PICO_STR_M>** const value_slots[2] = { &this->conv_from_label, &this->conv_to_label };
    for(int i = 0; i < 2; i++){
        const bool from = (i == 0);
        Button* b = new Button(body.x + MARGIN, y + (row_h - btn_h) / 2, "-");
        b->setFontSize(FontFn::Small);
        b->setW(unit_w);
        b->setH(btn_h);
        b->setOnPressEnd([this, from](){ this->openConvUnitPicker(from); });
        WidgetFunctions::Add(b);
        *unit_slots[i] = b;

        auto* l = new Label<PICO_STR_M>(value_x, y + (row_h - value_h) / 2, "");
        l->setFontSize(FontFn::Big);
        l->setMaxWidth(value_w);
        l->setMaxHeight(value_h);
        l->setTextAlign(TextAlign::Right);
        l->setDisableAutoTextDecoration(true);
        WidgetFunctions::Add(l);
        *value_slots[i] = l;

        y += row_h + MARGIN;
    }

    // ---- 「1 km = 0.62 mi」/ レート + [更新] ----
    const int status_h = Label<PICO_STR_M>::GetLineHeight(FontFn::Small);
    const int update_w = 48;
    this->conv_update_button = new Button(body.x + body.w - MARGIN - update_w, y, "更新");
    this->conv_update_button->setIcon(IconID::Refresh, IconSize::Px16);
    this->conv_update_button->setFontSize(FontFn::Small);
    this->conv_update_button->setW(update_w);
    this->conv_update_button->setH(btn_h);
    this->conv_update_button->setOnPressEnd([this](){ this->startCurrencyUpdate(); });
    WidgetFunctions::Add(this->conv_update_button);

    //2行: 1行目に「1 USD = 140 JPY」、2行目にレートの日付や取得の状態(1行では[更新]の横に収まらない)
    const int status_box_h = status_h * 2 > btn_h ? status_h * 2 : btn_h;
    this->conv_status = new Label<PICO_STR_L>(body.x + MARGIN, y, "");
    this->conv_status->setFontSize(FontFn::Small);
    //[更新]が出ていないとき(単位)でも幅は同じにしておく(出し入れで文字の位置が動かないように)
    this->conv_status->setMaxWidth(body.w - MARGIN * 3 - update_w);
    this->conv_status->setMaxHeight(status_box_h);
    this->conv_status->setDisableAutoTextDecoration(true);
    WidgetFunctions::Add(this->conv_status);
    y += status_box_h + MARGIN;

    // ---- キーパッド ----
    this->conv_keypad = new CalculatorKeypad(body.x, y, body.w, body.y + body.h - y, CalculatorKeypad::Layout::Converter);
    this->conv_keypad->setOnKey([this](const char* key){ this->handleConvKey(key); });
    WidgetFunctions::Add(this->conv_keypad);

    this->refreshConv();
}
