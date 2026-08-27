#pragma once

#include "gui/widgets/Widget.hpp"
#include "gui/widgets/interfaces/IFontImplementation.hpp"
#include "gui/widgets/interfaces/ITextColor.hpp"

class Checkbox : public Widget, public IFontImplementation, public ITextColor {
    private:
        bool isChecked = false;
        String text = "";

        void setTextAndCalc(String text);

        std::function<void()> on_change_checked = nullptr;

    public:

        Checkbox(int16_t x, int16_t y, String text){
            this->l_rect = {x, y, 0, 0};
            this->setTextAndCalc(text);
        }

        void render() override;

        void causeOnPressStart() override;

        void causeOnChangeChecked() {
            if(on_change_checked) on_change_checked();
        }
        void setOnChangeChecked(std::function<void()> callback){
            on_change_checked = callback;
        }

        String getText() { return this->text; }
        void setText(String text) {
            this->setTextAndCalc(text);
            this->needsRender();
        }

        bool getIsChecked() { return this->isChecked; }
        void setIsChecked(bool isChecked){
            this->isChecked = isChecked;
            this->needsRender();
        }

        void setFontSize(FontFn::FontSize size) override {
            this->f_size = size;
            this->needsRender();
        }
        void setTextColor(int8_t palette_color) override {
            this->text_color = palette_color;
            this->needsRender();
        }
};