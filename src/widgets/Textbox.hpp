#pragma once

#include "widgets/Label.hpp"
#include "widgets/interfaces/ITextInputTarget.hpp"

class Textbox : public Label, public ITextInputTarget {
    private:
        bool is_single_line = false;

        std::function<void()> on_text_changed = nullptr;

    public:
        Textbox(String text, int16_t x, int16_t y, int16_t w, int16_t h, bool is_single_line) : Label(x, y, text) {
            this->setMaxWidth(w);
            this->setMaxHeight(h);

            this->is_single_line = is_single_line;

            setBorderColor(this->border_color);
            setBorderWidth(1);
            setBackgroundColor(this->background_color);
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

        ~Textbox() override;
};