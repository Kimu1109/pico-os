#pragma once

#include "gui/widgets/Widget.hpp"
#include "gui/widgets/interfaces/IFontImplementation.hpp"
#include "gui/widgets/interfaces/ITextColor.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

class Checkbox : public Widget, public IFontImplementation, public ITextColor {
    private:
        bool isChecked = false;
        FixedString<PICO_STR_L> text;

        // テキストを設定し、アイコン込みの表示サイズを計算し直す。
        //
        // 注意: メンバテンプレート(template<size_t N>)は呼び出し側で使われた組み合わせごとに
        // 暗黙インスタンス化させる必要があるため、クラス本体内でインライン定義しておく。
        // Checkbox.cppに定義を置くと、そのファイルの中で使われた特殊化しか実体化されず、
        // 他の翻訳単位から new Checkbox(...) するとリンクエラーになる(Label.hppと同じ理由)。
        // 実際の計算は非テンプレートのrecalcSize()に寄せてあるので、
        // ヘッダ側にOS_Data等を持ち込まずに済む。
        template<size_t N>
        void setTextAndCalc(const FixedString<N>& text){
            this->text.assign(text);
            this->recalcSize();
        }

        // 現在のtextとフォント設定から l_rect のサイズを決める
        void recalcSize();

        std::function<void()> on_change_checked = nullptr;

    public:

        template<size_t N>
        Checkbox(int16_t x, int16_t y, FixedString<N> text){
            this->l_rect = {x, y, 0, 0};
            this->setTextAndCalc(text);
        }
        Checkbox(int16_t x, int16_t y, const char* text) : Checkbox(x, y, FixedString<PICO_STR_L>(text)) {}

        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::Checkbox; }

        void causeOnPressStart() override;

        void causeOnChangeChecked() {
            if(on_change_checked) on_change_checked();
        }
        void setOnChangeChecked(std::function<void()> callback){
            on_change_checked = callback;
        }

        const FixedString<PICO_STR_L>* getText() { return &this->text; }
        template<size_t N>
        void setText(FixedString<N> text) {
            this->setTextAndCalc(text);
            this->needsRender();
        }
        void setText(const char* text) {
            this->setTextAndCalc(FixedString<PICO_STR_L>(text));
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