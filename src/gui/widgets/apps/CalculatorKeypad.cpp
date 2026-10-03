#include "gui/widgets/apps/CalculatorKeypad.hpp"
#include "functions/Font_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "consts.hpp"
#include "OS_Data.hpp"

#include <cstring>
#include <cstdio>

using Kind = CalculatorKeypad::Kind;

// 7行6列。上3行が関数、下4行が数字と四則演算(KeyboardNumのkPadKeysと同じ「行・開始列・列幅」のテーブル形式)。
// 表示は実機のフォントで確実に出るASCIIとπ/√/×÷だけにしてある(上付きの²や⁻¹はフォントに無いことがある)。
// "mod"の位置はグラフの式を入れている間だけ変数"x"になる(labelOf()/insertOf())。
const CalculatorKeypad::Key CalculatorKeypad::kKeys[] = {
    { "SHIFT", "",    nullptr, nullptr, 0, 0, 1, Kind::Shift },
    { "HYP",   "",    nullptr, nullptr, 0, 1, 1, Kind::Hyp },
    { "DEG",   "DRG", nullptr, nullptr, 0, 2, 1, Kind::Command }, // 表示は今の単位(setAngleLabel)
    { "(",     "(",   nullptr, nullptr, 0, 3, 1, Kind::Insert },
    { ")",     ")",   nullptr, nullptr, 0, 4, 1, Kind::Insert },
    { "AC",    "AC",  nullptr, nullptr, 0, 5, 1, Kind::Command },

    { "sin",   "sin", nullptr, nullptr, 1, 0, 1, Kind::Trig },
    { "cos",   "cos", nullptr, nullptr, 1, 1, 1, Kind::Trig },
    { "tan",   "tan", nullptr, nullptr, 1, 2, 1, Kind::Trig },
    { "log",   "log(", "10^x", "10^(", 1, 3, 1, Kind::Insert },
    { "ln",    "ln(",  "e^x",  "e^(",  1, 4, 1, Kind::Insert },
    { "DEL",   "DEL",  nullptr, nullptr, 1, 5, 1, Kind::Command },

    { "√",     "√(",    "cbrt", "cbrt(", 2, 0, 1, Kind::Insert },
    { "x^2",   "^2",    "x^3",  "^3",    2, 1, 1, Kind::Insert },
    { "x^y",   "^",     "root", "root(", 2, 2, 1, Kind::Insert },
    { "x^-1",  "^(-1)", "abs",  "abs(",  2, 3, 1, Kind::Insert },
    { "x!",    "!",     nullptr, nullptr, 2, 4, 1, Kind::Insert },
    { "π",     "π",     "e",    "e",     2, 5, 1, Kind::Insert },

    { "7",   "7",   nullptr, nullptr, 3, 0, 1, Kind::Insert },
    { "8",   "8",   nullptr, nullptr, 3, 1, 1, Kind::Insert },
    { "9",   "9",   nullptr, nullptr, 3, 2, 1, Kind::Insert },
    { "nCr", "C",   "nPr",   "P",     3, 3, 1, Kind::Insert },
    { "mod", "mod", nullptr, nullptr, 3, 4, 1, Kind::Insert },
    { "÷",   "÷",   nullptr, nullptr, 3, 5, 1, Kind::Insert },

    { "4",   "4",   nullptr, nullptr, 4, 0, 1, Kind::Insert },
    { "5",   "5",   nullptr, nullptr, 4, 1, 1, Kind::Insert },
    { "6",   "6",   nullptr, nullptr, 4, 2, 1, Kind::Insert },
    { ",",   ",",   nullptr, nullptr, 4, 3, 1, Kind::Insert },
    { "e",   "e",   nullptr, nullptr, 4, 4, 1, Kind::Insert },
    { "×",   "×",   nullptr, nullptr, 4, 5, 1, Kind::Insert },

    { "1",   "1",   nullptr, nullptr, 5, 0, 1, Kind::Insert },
    { "2",   "2",   nullptr, nullptr, 5, 1, 1, Kind::Insert },
    { "3",   "3",   nullptr, nullptr, 5, 2, 1, Kind::Insert },
    { "EXP", "E",   nullptr, nullptr, 5, 3, 1, Kind::Insert },
    { "Ans", "Ans", nullptr, nullptr, 5, 4, 1, Kind::Insert },
    { "-",   "-",   nullptr, nullptr, 5, 5, 1, Kind::Insert },

    { "0",   "0",   nullptr, nullptr, 6, 0, 1, Kind::Insert },
    { ".",   ".",   nullptr, nullptr, 6, 1, 1, Kind::Insert },
    { "%",   "%",   nullptr, nullptr, 6, 2, 1, Kind::Insert },
    { "=",   "=",   nullptr, nullptr, 6, 3, 2, Kind::Command },
    { "+",   "+",   nullptr, nullptr, 6, 5, 1, Kind::Insert },
};
const int CalculatorKeypad::kKeyCount = sizeof(kKeys) / sizeof(kKeys[0]);

int CalculatorKeypad::colX(int col) const {
    return col * (this->l_rect.w / kCols);
}
int CalculatorKeypad::colW(int col) const {
    const int base = this->l_rect.w / kCols;
    if (col == kCols - 1) return this->l_rect.w - base * (kCols - 1);
    return base;
}
int CalculatorKeypad::colSpanW(int col_start, int col_span) const {
    int total = 0;
    for (int c = col_start; c < col_start + col_span; c++) total += this->colW(c);
    return total;
}
int CalculatorKeypad::rowY(int row) const {
    return row * (this->l_rect.h / kRows);
}
int CalculatorKeypad::rowH(int row) const {
    const int base = this->l_rect.h / kRows;
    if (row == kRows - 1) return this->l_rect.h - base * (kRows - 1);
    return base;
}

const CalculatorKeypad::Key* CalculatorKeypad::hitKey(int local_x, int local_y) const {
    for (int i = 0; i < kKeyCount; i++) {
        const Key& key = kKeys[i];

        const int y = this->rowY(key.row);
        const int h = this->rowH(key.row);
        if (local_y < y || local_y >= y + h) continue;

        const int x = this->colX(key.col_start);
        const int w = this->colSpanW(key.col_start, key.col_span);
        if (local_x < x || local_x >= x + w) continue;

        return &key;
    }
    return nullptr;
}

const char* CalculatorKeypad::labelOf(const Key& key) const {
    if (key.kind == Kind::Trig) {
        // "a" + "sin" + "h" の組み合わせ(SHIFT=逆関数、HYP=双曲線)
        const int slot = key.row == 1 ? key.col_start : 0;
        char* buf = this->trig_buf[slot % 3];
        snprintf(buf, sizeof(this->trig_buf[0]), "%s%s%s", this->shift ? "a" : "", key.insert, this->hyp ? "h" : "");
        return buf;
    }
    if (key.kind == Kind::Command && strcmp(key.insert, "DRG") == 0) return this->angle_label;
    if (key.kind == Kind::Command && strcmp(key.insert, "=") == 0) return this->graph_mode ? "描画" : "=";
    if (strcmp(key.insert, "mod") == 0) {
        if (this->graph_mode) return this->shift ? "mod" : "x";
        return "mod";
    }
    if (this->shift && key.shift_label) return key.shift_label;
    return key.label;
}

const char* CalculatorKeypad::insertOf(const Key& key) const {
    if (strcmp(key.insert, "mod") == 0) {
        if (this->graph_mode && !this->shift) return "x";
        return "mod";
    }
    if (this->shift && key.shift_insert) return key.shift_insert;
    return key.insert;
}

bool CalculatorKeypad::keyCenter(const char* label, int& x, int& y) const {
    const Rect g = this->getScreenRect();
    for (int i = 0; i < kKeyCount; i++) {
        const Key& key = kKeys[i];
        if (strcmp(this->labelOf(key), label) != 0) continue;
        x = g.x + this->colX(key.col_start) + this->colSpanW(key.col_start, key.col_span) / 2;
        y = g.y + this->rowY(key.row) + this->rowH(key.row) / 2;
        return true;
    }
    return false;
}

void CalculatorKeypad::causeOnPressStart() {
    Widget::causeOnPressStart();

    const Rect g = this->getScreenRect();
    const Key* key = this->hitKey(OSData::touchX - g.x, OSData::touchY - g.y);
    if (!key) return;

    switch (key->kind) {
        case Kind::Shift:
            this->shift = !this->shift;
            this->needsRender();
            return;
        case Kind::Hyp:
            this->hyp = !this->hyp;
            this->needsRender();
            return;
        default:
            break;
    }

    char trig[12];
    const char* out = nullptr;
    if (key->kind == Kind::Trig) {
        snprintf(trig, sizeof(trig), "%s%s%s(", this->shift ? "a" : "", key->insert, this->hyp ? "h" : "");
        out = trig;
    } else {
        out = this->insertOf(*key);
    }

    // SHIFT/HYPは1回きり(次のキーで戻る)
    const bool had_mod = this->shift || this->hyp;
    this->shift = false;
    this->hyp = false;
    if (had_mod) this->needsRender();

    if (this->on_key) this->on_key(out);
}

void CalculatorKeypad::render() {
    if (!this->needs_redraw) return;
    if (!this->visible) return;

    const Rect g = this->getScreenRect();
    markdirty(g);

    LGFX_Sprite* f = OSData::frame;
    f->fillRect(g.x, g.y, g.w, g.h, this->background_color);

    for (int i = 0; i < kKeyCount; i++) {
        const Key& key = kKeys[i];

        const int x = g.x + this->colX(key.col_start);
        const int y = g.y + this->rowY(key.row);
        const int w = this->colSpanW(key.col_start, key.col_span);
        const int h = this->rowH(key.row);

        const char* label = this->labelOf(key);

        //「=」は主操作なので反転表示で目立たせる(TabBarの選択中タブと同じ見た目)。
        // SHIFT/HYPは効いている間だけ反転する
        const bool emphasize = (key.kind == Kind::Command && strcmp(key.insert, "=") == 0)
            || (key.kind == Kind::Shift && this->shift)
            || (key.kind == Kind::Hyp && this->hyp);
        // 関数の段は薄い灰色の地にして数字の段と見分けやすくする
        const bool fn_row = key.row <= 2 && !emphasize;
        if (emphasize)   f->fillRect(x, y, w, h, PICO_BLACK);
        else if (fn_row) f->fillRect(x, y, w, h, PICO_LIGHTGREY);
        f->drawRect(x, y, w, h, PICO_BLACK);

        //「AC」「DEL」は消去操作だと分かるよう赤字に(状態否定にPICO_REDを使う既存の慣習と揃える)。
        // SHIFTの裏の機能が出ている間はその文字を青にして、表と区別できるようにする
        int color = PICO_BLACK;
        if (emphasize) color = PICO_WHITE;
        else if (key.kind == Kind::Command && (strcmp(key.insert, "AC") == 0 || strcmp(key.insert, "DEL") == 0)) color = PICO_RED;
        else if (this->shift && (key.shift_label || key.kind == Kind::Trig)) color = PICO_BLUE;
        else if (this->hyp && key.kind == Kind::Trig) color = PICO_BLUE;

        // 収まらない長い名前("SHIFT" "acosh"等)だけ小さい英数字のフォントにする
        FontFn::SetSmall();
        int str_w = f->textWidth(label);
        if (str_w > w - 4) {
            f->setFont(&fonts::Font0);
            f->setTextSize(1);
            str_w = f->textWidth(label);
        }
        const int str_h = f->fontHeight();
        f->setTextColor(color);
        f->setCursor(x + (w - str_w) / 2, y + (h - str_h) / 2);
        f->print(label);
    }

    f->setTextColor(PICO_BLACK);
    FontFn::SetDefault();

    this->needs_redraw = false;
}
