#pragma once

#include "widgets/Widget.hpp"
#include "widgets/interfaces/IFontImplementation.hpp"

class Checkbox : public Widget, public IFontImplementation {
    private:
        bool isChecked = false;
        String text = "";

        void setText(String text);

    public:
        using Widget::onPressStart;

        Checkbox(int16_t x, int16_t y, String text){
            this->rect = {x, y, 0, 0};
            this->setText(text);
        }

        void render() override;

        void onPressStart() override;

        String Text() { return this->text; }
        void Text(String text) {
            this->setText(text);
            this->needsRender();
        }

        bool IsChecked() { return this->isChecked; }
        void IsChecked(bool isChecked){
            this->isChecked = isChecked;
            this->needsRender();
        }

        void SetFontSize(FontFn::FontSize size) override {
            this->f_size = size;
            this->needsRender();
        }
};