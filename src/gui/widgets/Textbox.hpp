#pragma once

#include "gui/widgets/Label.hpp"
#include "gui/widgets/interfaces/ITextInputTarget.hpp"

template<size_t N>
class Textbox : public Label<N>, public ITextInputTarget {
    private:
        bool is_single_line = false;

        // 入力欄がキーボードを閉じて確定した(onHide())ときに呼ばれる。
        // 1文字ごとには呼ばない(onTextChanged()自体が「入力途中は背景を更新しない」
        // 方針なのでそれに合わせてある。負荷とLua側の扱いやすさの両面で妥当)
        std::function<void()> on_text_changed = nullptr;
        // 1文字ごとに呼ばれる(設定した場合だけ、入力中も欄の表示を更新する)。Luaの"text_input"用
        std::function<void()> on_text_input = nullptr;

    public:
        Textbox(const char* text, int16_t x, int16_t y, int16_t w, int16_t h, bool is_single_line) : Label<N>(x, y, text) {
            this->setMaxWidth(w);
            this->setMaxHeight(h);

            this->is_single_line = is_single_line;

            this->setBorderColor(this->border_color);
            this->setBorderWidth(1);
            this->setBackgroundColor(this->background_color);

            //入力欄なのでカーソル位置テーブルは必ず要る。ここで有効にしておくと、
            //描画中にカーソルAPIが初めて呼ばれてレイアウトがやり直しになるのを避けられる
            this->enableCursorTracking();
        }

        void causeOnPressStart() override;

        void onShow(ITextInputWidget* keyboard) override;
        void onTextChanged(ITextInputWidget* keyboard) override;
        void onHide(ITextInputWidget* keyboard) override;

        bool getIsSingleLine() override {
            return this->is_single_line;
        }
        void setIsSingleLine(bool is_single_line) override {
            this->is_single_line = is_single_line;
        }

        void setOnTextChanged(std::function<void()> callback){
            this->on_text_changed = callback;
        }

        void setOnTextInput(std::function<void()> callback){
            this->on_text_input = callback;
        }

        ~Textbox() override;

        WidgetType getWidgetType() const override { return WidgetType::Textbox; }
        bool focusableByDefault() const override { return true; }
};
