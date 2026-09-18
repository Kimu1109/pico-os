#pragma once

#include "gui/widgets/Widget.hpp"
#include "gui/widgets/interfaces/IFontImplementation.hpp"
#include "gui/widgets/interfaces/IBorderColor.hpp"
#include "gui/widgets/interfaces/ITextColor.hpp"
#include "gui/icons/icon_render.h"
#include "util/FixedString.hpp"
#include "consts.hpp"
#include "Arduino.h"

class Button :
    public Widget,
    public IFontImplementation,
    public IBorderColor,
    public ITextColor
{
    private:
        FixedString<PICO_STR_M> text;

        const int TEXT_SPACING = 6;
        const int _3D_PIX_LEN = 2;

        int text_w;
        int text_h;

        // setW()/setH()で明示的に指定された箱の大きさ(0 = 文字の実寸に合わせる)。
        // setText()やsetFontSize()で文字を測り直した時に指定を上書きしないよう覚えておく
        // — ラベルが変わるたびに幅が伸び縮みすると、並べたボタンの位置がずれてしまうため。
        int fixed_w = 0;
        int fixed_h = 0;

        bool allowTextSpacing = true;

        // アイコンボタン(文字の代わりにアイコンを描く)。戻るボタンのように
        // 「1行使うのがもったいない」場所向け。setIcon()を呼ぶまでは
        // 従来通りテキストのボタンとして振る舞う
        bool has_icon = false;
        IconID icon_id = IconID::AppBox;
        IconSize icon_size = IconSize::Px16;

        void calcTextSize(const char* text);
        // 中身(テキストまたはアイコン)を箱の中央へ描く。pressOffsetは
        // 押し込み表示時の見た目のずれ分(_3D_PIX_LEN、非押下時は0)
        void drawContent(const Rect& g_rect, int text_spacing, int pressOffset);

    public:

        template<size_t N>
        Button(int x, int y, FixedString<N> text){
            this->l_rect.x = x;
            this->l_rect.y = y;
            this->calcTextSize(text.c_str());
            this->text.assign(text);
            this->needs_redraw = true;
        }
        Button(int x, int y, const char* text) : Button(x, y, FixedString<PICO_STR_M>(text)) {}
        template<size_t N>
        Button(FixedString<N> text){
            this->calcTextSize(text.c_str());
            this->text.assign(text);
            this->needs_redraw = true;
        }
        Button(const char* text) : Button(FixedString<PICO_STR_M>(text)) {}

        void causeOnPressStart() override {
            if(this->on_press_start) this->on_press_start();
            this->needsRender();
        }
        void causeOnPressEnd() override {
            if(this->on_press_end) this->on_press_end();
            this->needsRender();
        }

        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::Button; }

        Rect getLocalRect() const override { 
            const int text_spacing = this->allowTextSpacing ? TEXT_SPACING : 0;

            const int16_t BOX_W = this->l_rect.w + text_spacing + _3D_PIX_LEN + 1;
            const int16_t BOX_H = this->l_rect.h + text_spacing + _3D_PIX_LEN + 1;
    
            return {
                this->l_rect.x,
                this->l_rect.y,
                BOX_W,
                BOX_H
            };
        }

        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::OPAQUE; }

        void setFontSize(FontFn::FontSize size) override {
            this->f_size = size;
            this->calcTextSize(this->text.c_str());
            this->needsRender();
        }
        void setBorderColor(int8_t palette_color) override {
            this->border_color = palette_color;
            this->needsRender();
        }
        void setTextColor(int8_t palette_color) override {
            this->text_color = palette_color;
            this->needsRender();
        }

        // ボタンの文字列を差し替える。setW()/setH()で与えた大きさは保たれる
        void setText(const char* text);
        template<size_t N>
        void setText(const FixedString<N>& text){ this->setText(text.c_str()); }

        const FixedString<PICO_STR_M>& getText() const { return this->text; }

        void setW(int w){
            this->fixed_w = w;
            this->l_rect.w = w;
        }
        void setH(int h){
            this->fixed_h = h;
            this->l_rect.h = h;
        }

        void setAllowTextSpacing(bool v){
            this->allowTextSpacing = v;
            this->needsRender();
        }
        bool getAllowTextSpacing() { return this->allowTextSpacing; }

        // 文字の代わりにアイコンを描くボタンにする。setW()/setH()を別途
        // 呼んでいなければ箱の大きさもアイコンぴったりへ合わせる
        // (呼んだ後にsetIcon()しても、既に指定済みの大きさは上書きしない)
        void setIcon(IconID id, IconSize size){
            this->has_icon = true;
            this->icon_id = id;
            this->icon_size = size;

            const int icon_px = IconRender::IconPixelSize(size);
            if(!this->fixed_w) this->l_rect.w = icon_px;
            if(!this->fixed_h) this->l_rect.h = icon_px;

            this->needsRender();
        }
        bool getHasIcon() { return this->has_icon; }
        IconID getIconId() { return this->icon_id; }
};