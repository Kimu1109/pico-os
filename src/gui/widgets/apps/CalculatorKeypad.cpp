#include "gui/widgets/apps/CalculatorKeypad.hpp"
#include "functions/Font_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "consts.hpp"
#include "OS_Data.hpp"

#include <cstring>

// 6行4列のグリッド。"0"だけ3マス幅、"="は右下の1マスに置いて押しやすくする
// (KeyboardNumのkPadKeysと同じ「行・開始列・列幅」のテーブル形式)。
const CalculatorKeypad::Key CalculatorKeypad::kKeys[] = {
    { "AC", 0, 0, 1 }, { "(", 0, 1, 1 }, { ")", 0, 2, 1 }, { "⌫", 0, 3, 1 },
    { "7",  1, 0, 1 }, { "8", 1, 1, 1 }, { "9", 1, 2, 1 }, { "÷", 1, 3, 1 },
    { "4",  2, 0, 1 }, { "5", 2, 1, 1 }, { "6", 2, 2, 1 }, { "×", 2, 3, 1 },
    { "1",  3, 0, 1 }, { "2", 3, 1, 1 }, { "3", 3, 2, 1 }, { "-", 3, 3, 1 },
    { "√",  4, 0, 1 }, { "π", 4, 1, 1 }, { ".", 4, 2, 1 }, { "+", 4, 3, 1 },
    { "0",  5, 0, 3 }, { "=", 5, 3, 1 },
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

void CalculatorKeypad::causeOnPressStart() {
    Widget::causeOnPressStart();

    const Rect g = this->getScreenRect();
    const Key* key = this->hitKey(OSData::touchX - g.x, OSData::touchY - g.y);
    if (key && this->on_key) this->on_key(key->label);
}

void CalculatorKeypad::render() {
    if (!this->needs_redraw) return;
    if (!this->visible) return;

    const Rect g = this->getScreenRect();
    markdirty(g);

    OSData::frame->fillRect(g.x, g.y, g.w, g.h, this->background_color);

    FontFn::SetSmall();
    const int str_h = OSData::frame->fontHeight();

    for (int i = 0; i < kKeyCount; i++) {
        const Key& key = kKeys[i];

        const int x = g.x + this->colX(key.col_start);
        const int y = g.y + this->rowY(key.row);
        const int w = this->colSpanW(key.col_start, key.col_span);
        const int h = this->rowH(key.row);

        //「=」は主操作なので反転表示で目立たせる(TabBarの選択中タブと同じ見た目)
        const bool emphasize = (strcmp(key.label, "=") == 0);
        if (emphasize) OSData::frame->fillRect(x, y, w, h, PICO_BLACK);
        OSData::frame->drawRect(x, y, w, h, PICO_BLACK);

        //「AC」は消去操作だと分かるよう赤字にする(状態否定にPICO_REDを使う既存の慣習と揃える)
        const bool is_ac = (strcmp(key.label, "AC") == 0);
        OSData::frame->setTextColor(emphasize ? PICO_WHITE : (is_ac ? PICO_RED : PICO_BLACK));

        const int str_w = OSData::frame->textWidth(key.label);
        OSData::frame->setCursor(x + (w - str_w) / 2, y + (h - str_h) / 2);
        OSData::frame->print(key.label);
    }

    OSData::frame->setTextColor(PICO_BLACK);
    FontFn::SetDefault();

    this->needs_redraw = false;
}
