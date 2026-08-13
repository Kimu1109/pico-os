#pragma once

#include "functions/Font_Functions.hpp"

class IFontImplementation {
    protected:
        FontFn::FontSize f_size = FontFn::FontSize::Normal;

        void fontApply(){
            FontFn::SetFontSize(this->f_size);
        }
        void fontDefault(){
            FontFn::SetDefault();
        }

    public:
        FontFn::FontSize GetFontSize() { return f_size; }
        virtual void SetFontSize(FontFn::FontSize size);
};