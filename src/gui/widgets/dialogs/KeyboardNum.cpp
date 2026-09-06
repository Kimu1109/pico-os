#include "gui/widgets/dialogs/KeyboardNum.hpp"
#include "functions/GFX_Functions.hpp"
#include "OS_Data.hpp"

namespace {
    // 数字パッド(常時固定)のキー1つ分の定義
    // col_start/col_spanはPAD_W(=SCREEN_WIDTH/4)単位のグリッド位置
    struct PadKey {
        const char* str;
        int row;
        int col_start;
        int col_span;
    };

    // 7 8 9 ← / 4 5 6 → / 1 2 3 ⌫ / 0(2列幅) . 決定
    const PadKey kPadKeys[] = {
        { "7", 0, 0, 1 }, { "8", 0, 1, 1 }, { "9", 0, 2, 1 }, { "←", 0, 3, 1 },
        { "4", 1, 0, 1 }, { "5", 1, 1, 1 }, { "6", 1, 2, 1 }, { "→", 1, 3, 1 },
        { "1", 2, 0, 1 }, { "2", 2, 1, 1 }, { "3", 2, 2, 1 }, { "X", 2, 3, 1 },
        { "0", 3, 0, 2 }, { ".", 3, 2, 1 }, { "決定", 3, 3, 1 },
    };
    const int kPadKeyCount = sizeof(kPadKeys) / sizeof(kPadKeys[0]);
}

void KeyboardNum::setVisible(bool visible) {
    this->visible = visible;
    this->input_label->setVisible(visible);
    this->input_label->setMaxHeight(SCREEN_HEIGHT - 10 * 2 - this->l_rect.h);

    if (visible) {
        this->inputs = this->input_label->getText();
        if (this->target) this->target->onShow(this);
    } else {
        if (this->target) this->target->onHide(this);
    }

    this->needs_redraw = true;
    markdirty(this->getScreenRect());
}

void KeyboardNum::causeOnPressStart() {
    Widget::causeOnPressStart();

    const int px = OSData::touchX;
    const int py = OSData::touchY;

    // --- タブ行 ---
    if (py >= KB_TOP && py < KB_TOP + TAB_H) {
        int col = px / TAB_W;
        if (col >= 0 && col < TAB_COLS) {
            SymbolMode newMode = (col == 0) ? SymbolMode::Digit
                                : (col == 1) ? SymbolMode::Arith
                                              : SymbolMode::Math;
            if (newMode != this->mode) {
                this->mode = newMode;
                this->needs_redraw = true;
                markdirty(this->getScreenRect());
            }
        }
        return;
    }

    // --- 記号行(モード依存) ---
    const int symbolTop = KB_TOP + TAB_H;
    if (py >= symbolTop && py < symbolTop + SYMBOL_H) {
        int col = px / SYMBOL_W;
        if (col >= 0 && col < SYMBOL_COLS) {
            const char* str = currentSymbols()[col].str;
            if (str[0] != '\0') {
                addInputAtCursor(String(str));
            }
        }
        return;
    }

    // --- 数字パッド(常時固定) ---
    const int padTop = symbolTop + SYMBOL_H;
    if (py < padTop) return;

    int row = (py - padTop) / PAD_H;
    if (row < 0 || row > 3) return;

    for (int i = 0; i < kPadKeyCount; i++) {
        const PadKey& key = kPadKeys[i];
        if (key.row != row) continue;

        int x = key.col_start * PAD_W;
        int w = key.col_span * PAD_W;
        if (px < x || px >= x + w) continue;

        String str = key.str;
        if (str == "←") {
            moveCursor(-1);
        } else if (str == "→") {
            moveCursor(1);
        } else if (str == "X") {
            removeBeforeCursor();
        } else if (str == "決定") {
            submit();
        } else {
            addInputAtCursor(str);
        }
        break;
    }
}

void KeyboardNum::render() {
    if (!this->needs_redraw) return;
    if (!this->visible) return;

    markdirty(this->getScreenRect());
    PICO_GFX::DrawDialogBackground();
    OSData::frame->fillRect(0, KB_TOP, SCREEN_WIDTH, KB_H, this->background_color);

    FontFn::SetSmall();

    // --- タブ行 ---
    for (int col = 0; col < TAB_COLS; col++) {
        int x = col * TAB_W;
        int y = KB_TOP;
        bool selected = (col == 0 && mode == SymbolMode::Digit)
                      || (col == 1 && mode == SymbolMode::Arith)
                      || (col == 2 && mode == SymbolMode::Math);

        if (selected) {
            OSData::frame->fillRect(x, y, TAB_W, TAB_H, PICO_BLACK);
        }
        OSData::frame->drawRect(x, y, TAB_W + 1, TAB_H + 1, PICO_BLACK);

        const char* label = tab_labels[col];
        int str_w = OSData::frame->textWidth(label);
        int str_h = OSData::frame->fontHeight();
        OSData::frame->setTextColor(selected ? PICO_WHITE : PICO_BLACK);
        OSData::frame->setCursor(x + (TAB_W - str_w) / 2, y + (TAB_H - str_h) / 2);
        OSData::frame->print(label);
    }
    OSData::frame->setTextColor(PICO_BLACK);

    // --- 記号行(モード依存) ---
    const SymbolKey* symbols = currentSymbols();
    int symbolTop = KB_TOP + TAB_H;
    for (int col = 0; col < SYMBOL_COLS; col++) {
        int x = col * SYMBOL_W;
        int y = symbolTop;

        OSData::frame->drawRect(x, y, SYMBOL_W + 1, SYMBOL_H + 1, PICO_BLACK);

        const char* str = symbols[col].str;
        if (str[0] == '\0') continue;

        int str_w = OSData::frame->textWidth(str);
        int str_h = OSData::frame->fontHeight();
        OSData::frame->setCursor(x + (SYMBOL_W - str_w) / 2, y + (SYMBOL_H - str_h) / 2);
        OSData::frame->print(str);
    }

    // --- 数字パッド(常時固定) ---
    int padTop = symbolTop + SYMBOL_H;
    for (int i = 0; i < kPadKeyCount; i++) {
        const PadKey& key = kPadKeys[i];
        int x = key.col_start * PAD_W;
        int y = padTop + key.row * PAD_H;
        int w = key.col_span * PAD_W;

        OSData::frame->drawRect(x, y, w + 1, PAD_H + 1, PICO_BLACK);

        int str_w = OSData::frame->textWidth(key.str);
        int str_h = OSData::frame->fontHeight();
        OSData::frame->setCursor(x + (w - str_w) / 2, y + (PAD_H - str_h) / 2);
        OSData::frame->print(key.str);
    }

    FontFn::SetDefault();

    this->needs_redraw = false;
}
