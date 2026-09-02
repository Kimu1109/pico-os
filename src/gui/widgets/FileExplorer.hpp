#pragma once

#include "gui/widgets/Widget.hpp"
#include "gui/widgets/Icon.hpp"
#include "gui/widgets/ScrollList.hpp"

class FileExplorer : public Widget {
    private:
        std::vector<Widget*> children_;

        char currentPath[128] = "/";

        void update_list();
        void update_places(){
            this->list->setW(this->l_rect.w);
            this->list->setH(this->l_rect.h - 20);
        }
        
        void on_press_back();
        void on_press_item(int index);

        ScrollList* list;
        Icon* back;

    public:
        FileExplorer(int16_t x, int16_t y, int16_t w, int16_t h){
            this->l_rect = {x, y, w, h};

            back = new Icon(0, (20 - 16) / 2, IconID::ArrowLeft, IconSize::Px16);
            back->setParent(this);
            back->setOnPressStart([this](){
                this->on_press_back();
            });

            list = new ScrollList(0, 20, w, h - 20);
            list->setParent(this);
            list->setFontSize(FontFn::FontSize::Small);
            list->setEnableIcon(true);
            list->setOnSelectItem([this](int index){
                this->on_press_item(index);
            });

            children_.push_back(back);
            children_.push_back(list);

            this->update_list();
        }

        const std::vector<Widget*>& getChildren() const override {
            return children_;
        }

        void render() override;
};