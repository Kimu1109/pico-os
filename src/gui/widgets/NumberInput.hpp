#pragma once

#include "gui/widgets/Widget.hpp"
#include "gui/widgets/interfaces/ITextInputTarget.hpp"
#include "gui/widgets/interfaces/ITextColor.hpp"
#include "gui/widgets/interfaces/IBorderColor.hpp"
#include "gui/widgets/interfaces/IFontImplementation.hpp"

class NumberInput : public Widget, public ITextInputTarget, public ITextColor, public IBorderColor, public IFontImplementation {
    private:
        String num;

    public:
        NumberInput(int16_t x, int16_t y, int16_t w){
            this->l_rect = {x, y, w, 24 + 4};
        }

        void causeOnPressStart() override;

        void setBorderColor(int8_t palette_color){
            this->border_color = palette_color;
            this->needsRender();
        }
        void setFontSize(FontFn::FontSize size){
            this->f_size = size;
            this->l_rect.h = FontFn::GetFontSize(size) + 4;
            this->needsRender();
        }
        void setTextColor(int8_t palette_color) {
            this->text_color = palette_color;
            this->needsRender();
        }

        void onShow(ITextInputWidget* keyboard);
        void onTextChanged(ITextInputWidget* keyboard);
        void onHide(ITextInputWidget* keyboard);

        //常に1行なので握りつぶす
        bool getIsSingleLine(){return true;}
        void setIsSingleLine(bool is_single_line){};

        void render() override;
        
        ~NumberInput() override;
};