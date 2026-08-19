#pragma once

#include "widgets/Widget.hpp"
#include "widgets/interfaces/IFontImplementation.hpp"
#include "widgets/interfaces/ITextColor.hpp"
#include "widgets/interfaces/IBorderColor.hpp"

class ScrollList : public Widget, public IFontImplementation, public IBorderColor, public ITextColor {

    private:
        std::vector<String>* dataSource = new std::vector<String>();
        int scrollY = 0;

        const static int MARGIN = 2;
        const static int SCROLL_BAR_W = 15;

        bool is_scrolling = false;
        int ref_touch_y = 0;
        int ref_scroll_y = 0;

        int font_h = 0;

        int selected_index = -1;

    public:
        using Widget::onPressStart;
        using Widget::onPressMove;
        using Widget::onPressEnd;

        ScrollList(int16_t x, int16_t y, int16_t w, int16_t h, int16_t default_size = -1){
            this->rect = {x, y, w, h};
            if(default_size != -1){
                dataSource->reserve(default_size);
            }
        }

        void Add(const String text){
            dataSource->push_back(text);
        }

        void render() override;

        void onPressStart() override;
        void onPressMove() override;
        void onPressEnd() override;

        WidgetTools::RenderMode GetRenderMode() const override { return WidgetTools::OPAQUE; }

        void SetFontSize(FontFn::FontSize size) override {
            this->f_size = size;
            this->needsRender();
        }
        void SetBorderColor(int8_t palette_color){
            this->border_color = palette_color;
            this->needsRender();
        }
        void SetTextColor(int8_t palette_color){
            this->text_color = palette_color;
            this->needsRender();
        }
};