#pragma once

#include "gui/widgets/Widget.hpp"
#include "gui/widgets/Label.hpp"
#include "functions/UTF8_Functions.hpp"
#include "functions/Font_Functions.hpp"
#include "gui/widgets/interfaces/ITextInputTarget.hpp"

// 電卓等で使う数字専用キーボード。
// 「0〜9・カーソル移動・決定・削除」は常時固定で表示し、
// その上に「数字/四則演算/数学記号」の3タブで切り替わる記号行を重ねる構成。
// (KeyboardEng.hppと同じく、キー配列テーブル+causeOnPressStartでの座標判定という
//  素朴な作りに揃えている。Buttonウィジェットを1キーごとに newしないのはヒープ節約のため)
class KeyboardNum : public Widget, public ITextInputWidget {
    public:
        // 記号行(タブ)の種類
        enum class SymbolMode {
            Digit,  // 数字モード: ( ) . , など、数値入力の補助記号のみ
            Arith,  // 四則演算モード: + - × ÷ ( )
            Math    // 数学記号モード: √ π e ^ % ± など(分数/べき乗/インテグラル等の構造入力は対象外)
        };

    protected:
        std::vector<Widget*> children_;
        ITextInputTarget* target = nullptr;

    private:
        // --- レイアウト定数 ---
        // タブ行(モード切替)
        constexpr static int TAB_H = 28;
        constexpr static int TAB_COLS = 3;
        constexpr static int TAB_W = SCREEN_WIDTH / TAB_COLS;
        // 記号行(モード依存, 6等分)
        constexpr static int SYMBOL_H = 28;
        constexpr static int SYMBOL_COLS = 6;
        constexpr static int SYMBOL_W = SCREEN_WIDTH / SYMBOL_COLS;
        // 数字パッド行(常時固定, 4行)
        constexpr static int PAD_H = 28;
        constexpr static int PAD_COLS = 4;
        constexpr static int PAD_W = SCREEN_WIDTH / PAD_COLS;

        constexpr static int KB_H = TAB_H + SYMBOL_H + PAD_H * 4;
        constexpr static int KB_TOP = SCREEN_HEIGHT - KB_H;

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

        const char* tab_labels[TAB_COLS] = { "数字", "四則演算", "数学記号" };

        SymbolMode mode = SymbolMode::Digit;

        const SymbolKey* currentSymbols() const {
            switch (mode) {
                case SymbolMode::Arith: return symbols_arith;
                case SymbolMode::Math:  return symbols_math;
                case SymbolMode::Digit:
                default:                return symbols_digit;
            }
        }

        String inputs = "";

        // カーソル位置(input_labelのcursor_pos, 文字インデックス)に文字列を挿入する
        void addInputAtCursor(String str) {
            int cursorChar = input_label->getCursorPos();
            int byteOffset = UTF8_Functions::Utf8ByteOffsetOfChar(inputs, cursorChar);

            inputs = inputs.substring(0, byteOffset) + str + inputs.substring(byteOffset);

            input_label->setText(inputs);
            input_label->setCursorPos(cursorChar + UTF8_Functions::Utf8Length(str));

            if (this->target) this->target->onTextChanged(this);
        }

        // カーソルの直前の1文字を削除する(backspace)
        void removeBeforeCursor() {
            int cursorChar = input_label->getCursorPos();
            if (cursorChar <= 0 || inputs.length() == 0) return;

            int byteOffsetEnd = UTF8_Functions::Utf8ByteOffsetOfChar(inputs, cursorChar);
            int byteOffsetStart = UTF8_Functions::Utf8ByteOffsetOfChar(inputs, cursorChar - 1);

            inputs = inputs.substring(0, byteOffsetStart) + inputs.substring(byteOffsetEnd);

            input_label->setText(inputs);
            input_label->setCursorPos(cursorChar - 1);

            if (this->target) this->target->onTextChanged(this);
        }

        void moveCursor(int delta) {
            input_label->setCursorMove(delta);
        }

        void submit() {
            // KeyboardEngの submit/go キーに合わせ、targetへhide通知した上で自身を隠す
            if (this->target) this->target->onHide(this);
            this->setVisible(false);
        }

    public:
        Label* input_label;

        void setVisible(bool visible) override;

        KeyboardNum(Label* input_label) {
            this->l_rect = { 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT };

            this->input_label = input_label;
            this->input_label->setVisible(false);
            this->visible = false;

            children_.push_back(input_label);
        }

        void causeOnPressStart() override;
        void render() override;

        const std::vector<Widget*>& getChildren() const override {
            return children_;
        }

        void setX(int x) override {};
        void setY(int y) override {};

        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::TRANSLUCENT; }

        void setInputTarget(ITextInputTarget* target) override {
            this->target = target;
        }
        void removeInputTarget(ITextInputTarget* valid_target) override {
            if (this->target == valid_target) {
                this->target = nullptr;
            }
        }
        ITextInputTarget* getInputTarget() override {
            return this->target;
        }

        void setText(String text) override {
            this->inputs = text;
            input_label->setText(inputs);
            input_label->setCursorToEnd();
        }
        String getText() override {
            return this->inputs;
        }
};
