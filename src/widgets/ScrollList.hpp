#pragma once

#include "widgets/Widget.hpp"

class ScrollList : public Widget {

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

        bool isOpaque() const override { return true; }
};