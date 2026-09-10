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
        this->inputs = *this->input_label->getText();
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

    // --- タブ行(許可モードが2つ以上ある時だけ存在する) ---
    if (this->tab_h > 0 && py >= kb_top && py < kb_top + tab_h) {
        int col = px / tab_w;
        if (col >= 0 && col < tab_cols) {
            SymbolMode newMode = visibleTabAt(col);
            if (newMode != this->mode) {
                this->mode = newMode;
                this->needs_redraw = true;
                markdirty(this->getScreenRect());
            }
        }
        return;
    }

    // --- 記号行(モード依存) ---
    const int symbolTop = kb_top + tab_h;
    if (py >= symbolTop && py < symbolTop + SYMBOL_H) {
        int col = px / SYMBOL_W;
        if (col >= 0 && col < SYMBOL_COLS) {
            const char* str = currentSymbols()[col].str;
            if (str[0] != '\0') {
                addInputAtCursor(str);
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

        const char* str = key.str;
        if (strcmp(str, "←") == 0) {
            moveCursor(-1);
        } else if (strcmp(str, "→") == 0) {
            moveCursor(1);
        } else if (strcmp(str, "X") == 0) {
            removeBeforeCursor();
        } else if (strcmp(str, "決定") == 0) {
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
    OSData::frame->fillRect(0, kb_top, SCREEN_WIDTH, kb_h, this->background_color);

    FontFn::SetSmall();

    // --- タブ行(許可モードが2つ以上ある時だけ描画) ---
    if (this->tab_h > 0) {
        for (int col = 0; col < tab_cols; col++) {
            SymbolMode tabMode = visibleTabAt(col);
            int x = col * tab_w;
            int y = kb_top;
            bool selected = (tabMode == this->mode);

            if (selected) {
                OSData::frame->fillRect(x, y, tab_w, tab_h, PICO_BLACK);
            }
            OSData::frame->drawRect(x, y, tab_w + 1, tab_h + 1, PICO_BLACK);

            const char* label = labelFor(tabMode);
            int str_w = OSData::frame->textWidth(label);
            int str_h = OSData::frame->fontHeight();
            OSData::frame->setTextColor(selected ? PICO_WHITE : PICO_BLACK);
            OSData::frame->setCursor(x + (tab_w - str_w) / 2, y + (tab_h - str_h) / 2);
            OSData::frame->print(label);
        }
        OSData::frame->setTextColor(PICO_BLACK);
    }

    // --- 記号行(モード依存) ---
    const SymbolKey* symbols = currentSymbols();
    int symbolTop = kb_top + tab_h;
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
