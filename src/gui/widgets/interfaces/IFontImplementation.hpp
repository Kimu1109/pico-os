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

        // DrawPlain()/GetLineHeight()が使い回すutilityInstance()は、呼び出し元のLabel<N>とは
        // 異なるテンプレート特殊化(Label<PICO_STR_LL>)になり得るため、素のprotectedアクセスでは
        // C++のアクセス制御(同一/派生クラス経由でしか許可されない)に引っかかる。
        // Label<N>同士は事実上同じ実装を共有しているとみなし、friendで許可する。
        template<size_t N> friend class Label;
};