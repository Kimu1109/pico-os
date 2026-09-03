#pragma once

#include "gui/widgets/Widget.hpp"
#include "gui/widgets/interfaces/IFontImplementation.hpp"
#include "gui/widgets/interfaces/ITextColor.hpp"
#include "gui/widgets/interfaces/IBorderColor.hpp"
#include "gui/icons/icon_render.h"

namespace ScrollListTools {
    struct Item {
        IconID icon = IconID::AppBox;
        char text[128];
    };
};

class ScrollList : public Widget, public IFontImplementation, public IBorderColor, public ITextColor {

    private:
        std::vector<ScrollListTools::Item>* dataSource = new std::vector<ScrollListTools::Item>();
        int scrollY = 0;

        const static int MARGIN = 2;
        const static int SCROLL_BAR_W = 15;

        bool is_scrolling = false;
        int ref_touch_y = 0;
        int ref_scroll_y = 0;

        int font_h = 0;

        int selected_index = -1;
        bool enable_icon = false;

        std::function<void(int index, bool already_selected)> on_selectitem = nullptr;

    public:

        ScrollList(int16_t x, int16_t y, int16_t w, int16_t h, int16_t default_size = -1){
            this->l_rect = {x, y, w, h};
            if(default_size != -1){
                dataSource->reserve(default_size);
            }
        }

        void add(const ScrollListTools::Item value){
            dataSource->push_back(value);
            this->needsRender();
        }
        void clear(){
            dataSource->clear();
            this->needsRender();
        }

        void render() override;

        void causeOnPressStart() override;
        void causeOnPressMove() override;
        void causeOnPressEnd() override;

        void setOnSelectItem(std::function<void(int index, bool already_selected)> on_selectitem) {
            this->on_selectitem = on_selectitem;
        }
        void causeOnSelectItem(bool already_selected){
            if(this->on_selectitem) this->on_selectitem(this->selected_index, already_selected);
        }

        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::OPAQUE; }

        void setFontSize(FontFn::FontSize size) override {
            this->f_size = size;
            this->needsRender();
        }
        void setBorderColor(int8_t palette_color){
            this->border_color = palette_color;
            this->needsRender();
        }
        void setTextColor(int8_t palette_color){
            this->text_color = palette_color;
            this->needsRender();
        }

        void setW(int w){
            this->l_rect.w = w;
            this->needsRender();
        }
        void setH(int h){
            this->l_rect.h = h;
            this->needsRender();
        }

        void setSelectedIndex(int index){
            this->selected_index = index;
            this->needsRender();
        }
        void clearSelectedIndex(){
            this->selected_index = -1;
        }
        int getSelectedIndex(){ return this->selected_index; }

        void setEnableIcon(bool value){
            this->enable_icon = value;
        }
        bool getEnableIcon() { return this->enable_icon; }

        ScrollListTools::Item* itemAt(int index){
            if (index < 0 || index >= this->dataSource->size()) {
                return nullptr;
            }
            return &this->dataSource->at(index);
        }

        int getFittingHeight(){
            if(this->font_h == 0){
                this->font_h = FontFn::GetFontSize(getFontSize());
            }
            return min(this->dataSource->size() * (this->font_h + MARGIN), SCREEN_HEIGHT - this->getScreenY());
        }      
};