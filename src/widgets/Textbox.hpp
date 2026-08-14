#pragma once

#include "widgets/Label.hpp"
#include "widgets/interfaces/ITextInputTarget.hpp"

class Textbox : public Label, public ITextInputTarget {
    private:
        bool is_single_line = false;
    public:
        Textbox(String text, int16_t x, int16_t y, int16_t w, int16_t h, bool is_single_line) : Label(x, y, text) {
            this->MaxWidth(w);
            this->MaxHeight(h);

            this->is_single_line = is_single_line;

            SetBorderColor(this->border_color);
            BorderWidth(1);
            BackgroundColor(this->background_color);
        }

        using Widget::onPressStart;

        void onPressStart() override;

        void onShow(ITextInputWidget* keyboard) override;
        void onTextChanged(ITextInputWidget* keyboard) override;
        void onHide(ITextInputWidget* keyboard) override;

        bool GetIsSingleLine() override {
            return this->is_single_line;
        }
        void SetIsSingleLine(bool is_single_line) override {
            this->is_single_line = is_single_line;
        }

        ~Textbox() override;
};