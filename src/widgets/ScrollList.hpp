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

        std::function<void(int index)> on_selectitem = nullptr;

    public:
        using Widget::onPressStart;
        using Widget::onPressMove;
        using Widget::onPressEnd;

        using Widget::W;
        using Widget::H;

        ScrollList(int16_t x, int16_t y, int16_t w, int16_t h, int16_t default_size = -1){
            this->l_rect = {x, y, w, h};
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

        void onSelectItem(std::function<void(int index)> on_selectitem) {
            this->on_selectitem = on_selectitem;
        }
        void onSelectItem(){
            if(this->on_selectitem) this->on_selectitem(this->selected_index);
        }

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

        void W(int w){
            this->l_rect.w = w;
            this->needsRender();
        }
        void H(int h){
            this->l_rect.h = h;
            this->needsRender();
        }

        void SelectedIndex(int index){
            this->selected_index = index;
            this->needsRender();
        }
        int SelectedIndex(){ return this->selected_index; }

        String ItemAt(int index){
            if(index == -1) return "";

            return this->dataSource->at(index);
        }

        int getFittingHeight(){
            if(this->font_h == 0){
                this->font_h = FontFn::GetFontSize(GetFontSize());
            }
            return min(this->dataSource->size() * (this->font_h + MARGIN), SCREEN_HEIGHT - this->getScreenY());
        }
};