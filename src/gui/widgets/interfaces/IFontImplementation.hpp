#pragma once

#include "functions/Font_Functions.hpp"

template<size_t N> class Label;

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
        FontFn::FontSize getFontSize() { return f_size; }
        virtual void setFontSize(FontFn::FontSize size);

        // Label<N>同士は事実上同じ実装を共有しているとみなし、friendで許可する
        // (以前はDrawPlain()等が別の特殊化Label<PICO_STR_LL>の部品を使い回していたため)。
        template<size_t N> friend class Label;
};