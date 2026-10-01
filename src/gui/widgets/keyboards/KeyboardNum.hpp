#pragma once

#include "gui/widgets/keyboards/KeyboardPanel.hpp"
#include "functions/Font_Functions.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

// 電卓等で使う数字専用キーボード。
// 「0〜9・カーソル移動・決定・削除」は常時固定で表示し、
// その上に「数字/四則演算/数学記号」の3タブで切り替わる記号行を重ねる構成。
// (KeyboardEng.hppと同じく、キー配列テーブル+causeOnPressStartでの座標判定という
//  素朴な作りに揃えている。Buttonウィジェットを1キーごとに newしないのはヒープ節約のため)
//
// 呼び出し側で使える記号モードを制限できる(例: 数字のみ/数字+四則演算のみ)。
// 制限はコンストラクタ引数、または setAllowedModes() で後から変更可能。
// 許可されていないタブは非表示・当たり判定なしになり、現在のモードが
// 許可外になった場合は自動的に「許可されている中で最初のモード」に切り替わる。
class KeyboardNum : public KeyboardPanel {
    public:
        // 記号行(タブ)の種類
        enum class SymbolMode {
            Digit,  // 数字モード: ( ) . , など、数値入力の補助記号のみ
            Arith,  // 四則演算モード: + - × ÷ ( )
            Math    // 数学記号モード: √ π e ^ % ± など(分数/べき乗/インテグラル等の構造入力は対象外)
        };

        // setAllowedModes()に渡すビットマスク定数
        // (例: MODE_DIGIT | MODE_ARITH で「数字と四則演算だけ許可」)
        static constexpr uint8_t MODE_DIGIT = 1 << 0;
        static constexpr uint8_t MODE_ARITH = 1 << 1;
        static constexpr uint8_t MODE_MATH  = 1 << 2;
        static constexpr uint8_t MODE_ALL   = MODE_DIGIT | MODE_ARITH | MODE_MATH;

    protected:
        std::vector<Widget*> children_;
        ITextInputTarget* target = nullptr;

    private:
        // --- レイアウト定数(タブ行以外は常時固定) ---
        constexpr static int TAB_ROW_H = 28;
        // 記号行(モード依存, 6等分)
        constexpr static int SYMBOL_H = 28;
        constexpr static int SYMBOL_COLS = 6;
        constexpr static int SYMBOL_W = SCREEN_WIDTH / SYMBOL_COLS;
        // 数字パッド行(常時固定, 4行)
        constexpr static int PAD_H = 28;
        constexpr static int PAD_COLS = 4;
        constexpr static int PAD_W = SCREEN_WIDTH / PAD_COLS;

        // --- タブ行以下、許可モード数に応じて変わるレイアウト値 ---
        // (許可モードが1つだけならタブ行自体を消して1行分詰める)
        int tab_h = TAB_ROW_H;
        int tab_cols = 3;
        int tab_w = SCREEN_WIDTH / 3;
        int kb_h = TAB_ROW_H + SYMBOL_H + PAD_H * 4;
        int kb_top = SCREEN_HEIGHT - kb_h;

        uint8_t allowed_modes = MODE_ALL;

        struct SymbolKey {
            const char* str; // ""の場合は空き(表示・当たり判定なし)
        };

        // 記号行の中身(モードごとに最大6個、余りは""で埋める)
        const SymbolKey symbols_digit[SYMBOL_COLS] = {
            { "(" }, { ")" }, { "," }, { "00" }, { "" }, { "" }
        };
        const SymbolKey symbols_arith[SYMBOL_COLS] = {
            { "+" }, { "-" }, { "×" }, { "÷" }, { "(" }, { ")" }
        };
        const SymbolKey symbols_math[SYMBOL_COLS] = {
            { "√" }, { "π" }, { "e" }, { "^" }, { "%" }, { "±" }
        };

        SymbolMode mode = SymbolMode::Digit;

        const SymbolKey* symbolsFor(SymbolMode m) const {
            switch (m) {
                case SymbolMode::Arith: return symbols_arith;
                case SymbolMode::Math:  return symbols_math;
                case SymbolMode::Digit:
                default:                return symbols_digit;
            }
        }
        const SymbolKey* currentSymbols() const {
            return symbolsFor(this->mode);
        }
        const char* labelFor(SymbolMode m) const {
            switch (m) {
                case SymbolMode::Arith: return "四則演算";
                case SymbolMode::Math:  return "数学記号";
                case SymbolMode::Digit:
                default:                return "数字";
            }
        }
        uint8_t flagFor(SymbolMode m) const {
            switch (m) {
                case SymbolMode::Arith: return MODE_ARITH;
                case SymbolMode::Math:  return MODE_MATH;
                case SymbolMode::Digit:
                default:                return MODE_DIGIT;
            }
        }
        bool isModeAllowed(SymbolMode m) const {
            return (allowed_modes & flagFor(m)) != 0;
        }
        // 許可されているモードのうち先頭(数字→四則演算→数学記号の順)を返す
        SymbolMode firstAllowedMode() const {
            if (isModeAllowed(SymbolMode::Digit)) return SymbolMode::Digit;
            if (isModeAllowed(SymbolMode::Arith)) return SymbolMode::Arith;
            return SymbolMode::Math;
        }
        // タブとして表示すべきモードの数(1個しかなければタブ切替UI自体が不要)
        int visibleTabCount() const {
            int count = 0;
            if (isModeAllowed(SymbolMode::Digit)) count++;
            if (isModeAllowed(SymbolMode::Arith)) count++;
            if (isModeAllowed(SymbolMode::Math))  count++;
            return count;
        }
        // index番目(0始まり, 数字→四則演算→数学記号の順で許可されているものだけ数える)のモードを返す
        SymbolMode visibleTabAt(int index) const {
            SymbolMode order[3] = { SymbolMode::Digit, SymbolMode::Arith, SymbolMode::Math };
            int seen = 0;
            for (SymbolMode m : order) {
                if (!isModeAllowed(m)) continue;
                if (seen == index) return m;
                seen++;
            }
            return SymbolMode::Digit; // 来ないはずの保険
        }

        // 許可モード数に応じてタブ行の高さ/列幅と、キーボード全体の高さを再計算する
        void recalcLayout() {
            int count = visibleTabCount();
            if (count <= 1) {
                // 選べるモードが1つだけなら切替UIは不要なので消して1行分詰める
                this->tab_h = 0;
                this->tab_cols = count;
                this->tab_w = SCREEN_WIDTH;
            } else {
                this->tab_h = TAB_ROW_H;
                this->tab_cols = count;
                this->tab_w = SCREEN_WIDTH / count;
            }
            this->kb_h = this->tab_h + SYMBOL_H + PAD_H * 4;
            this->kb_top = SCREEN_HEIGHT - this->kb_h;
            this->setPanelHeight(this->kb_h);
        }

        FixedString<PICO_STR_LL> inputs;

        // inputs内の挿入位置(UTF-8文字単位)。KeyboardEngと同じくこちらが位置の真を持つ
        int cursor_char = 0;

        void addInputAtCursor(const char* str) {
            if (inputs.length() + strlen(str) > FixedString<PICO_STR_LL>::capacity()) return;

            inputs.insertAtChar(cursor_char, str);
            cursor_char += FixedString<PICO_STR_LL>::charCount(str);

            this->notifyChanged(true);
        }

        void removeBeforeCursor() {
            if (cursor_char <= 0) {
                if (this->target) this->target->onBackspaceAtStart(this);
                return;
            }

            inputs.removeCharAt(cursor_char - 1);
            cursor_char--;

            this->notifyChanged(true);
        }

        void moveCursor(int delta) {
            int next = cursor_char + delta;
            const int last = inputs.charCount();
            if (next < 0) next = 0;
            if (next > last) next = last;
            if (next == cursor_char) {
                if (delta != 0 && this->target) this->target->onCursorAtEdge(this, delta < 0 ? -1 : 1);
                return;
            }
            cursor_char = next;
            this->notifyChanged(false);
        }

        // 物理キーボードの1文字を、このキー盤の記号表の表記へ直す(*→× /→÷)。入れられなければnullptr
        const char* physicalSymbolFor(uint32_t cp, char* buf) const {
            if (cp >= '0' && cp <= '9') { buf[0] = (char)cp; buf[1] = '\0'; return buf; }
            if (cp == '.') return ".";
            const char* want = nullptr;
            char one[2] = { (char)cp, '\0' };
            if (cp == '*') want = "×";
            else if (cp == '/') want = "÷";
            else if (cp < 0x80) want = one;
            else return nullptr;
            const SymbolMode order[3] = { SymbolMode::Digit, SymbolMode::Arith, SymbolMode::Math };
            for (SymbolMode m : order) {
                if (!isModeAllowed(m)) continue;
                const SymbolKey* syms = symbolsFor(m);
                for (int i = 0; i < SYMBOL_COLS; i++) {
                    if (strcmp(syms[i].str, want) == 0) return syms[i].str;
                }
            }
            return nullptr;
        }

        // ---- 物理キーボード ----
        bool acceptsPhysicalChar(uint32_t cp) const override {
            char buf[2];
            return physicalSymbolFor(cp, buf) != nullptr;
        }
        void physicalInsert(const char* utf8) override {
            //1文字ずつ届く(KeyboardPanel::onPhysicalKey())ので、先頭の1文字を見ればよい
            uint32_t cp = (uint8_t)utf8[0];
            char buf[2];
            const char* s = (cp < 0x80) ? physicalSymbolFor(cp, buf) : nullptr;
            if (s) addInputAtCursor(s);
        }
        void physicalBackspace() override { removeBeforeCursor(); }
        void physicalMove(int delta) override { moveCursor(delta); }

    public:
        // allowed_modes: MODE_DIGIT/MODE_ARITH/MODE_MATHのビットOR。省略時は全モード許可。
        KeyboardNum(uint8_t allowed_modes = MODE_ALL) : KeyboardPanel(TAB_ROW_H + SYMBOL_H + PAD_H * 4) {
            this->setAllowedModes(allowed_modes);
        }

        // 使用可能なモードを後から変更する。
        // 現在選択中のモードが許可外になった場合、許可モードの先頭に自動的に切り替える。
        void setAllowedModes(uint8_t allowed_modes) {
            allowed_modes &= MODE_ALL;
            if (allowed_modes == 0) allowed_modes = MODE_ALL; // 全部禁止は事故なので安全側に倒す

            this->allowed_modes = allowed_modes;
            if (!isModeAllowed(this->mode)) {
                this->mode = firstAllowedMode();
            }
            recalcLayout();

            this->needsRender();
        }
        uint8_t getAllowedModes() const {
            return this->allowed_modes;
        }
        SymbolMode getMode() const {
            return this->mode;
        }

        void causeOnPressStart() override;
        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::KeyboardNum; }

        void setText(const FixedString<PICO_STR_LL>& text) override {
            this->inputs = text;
            this->cursor_char = this->inputs.charCount();
            this->notifyChanged(false);
        }
        FixedString<PICO_STR_LL> getText() override {
            return this->inputs;
        }
        size_t getCursorByteOffset() override {
            return (size_t)this->inputs.byteOffsetOfChar(this->cursor_char);
        }
        void setCursorByteOffset(size_t byte_offset) override {
            int c = CharIndexOfByte(this->inputs.c_str(), byte_offset);
            const int last = this->inputs.charCount();
            this->cursor_char = (c > last) ? last : c;
            this->notifyChanged(false);
        }
};
