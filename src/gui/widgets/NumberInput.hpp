#pragma once

#include "gui/widgets/Widget.hpp"
#include "gui/widgets/interfaces/ITextInputTarget.hpp"
#include "gui/widgets/interfaces/ITextColor.hpp"
#include "gui/widgets/interfaces/IBorderColor.hpp"
#include "gui/widgets/interfaces/IFontImplementation.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

class NumberInput : public Widget, public ITextInputTarget, public ITextColor, public IBorderColor, public IFontImplementation {
    private:
        FixedString<PICO_STR_LL> num;

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

        // 表示中の文字列をそのまま読み書きする(数字専用キーボードで入力された内容の
        // 確定値。onHide()がキーボード側から受け取って書き込むのと同じ経路)。
        // 呼び出し側は数値へ変換して使うこと(検証・変換はここでは行わない)
        const FixedString<PICO_STR_LL>* getNum() const { return &this->num; }
        void setNum(const char* value) {
            this->num.assign(value);
            this->needsRender();
        }

        //常に1行なので握りつぶす
        bool getIsSingleLine(){return true;}
        void setIsSingleLine(bool is_single_line){};

        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::NumberInput; }
        
        ~NumberInput() override;
};