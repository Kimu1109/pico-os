#pragma once

#include "widgets/Widget.hpp"

class ScrollContainer : public Widget {
    private:
        std::vector<Widget*> children_;

        int sx;
        int sy;

        int s_scroll_x;
        int s_scroll_y;

        int scroll_x = 0;
        int scroll_y = 0;

        bool horizontal_scroll = false;
        bool vertical_scroll = true;

        bool is_scrolling = false;

        int max_scroll_x = 0;
        int max_scroll_y = 0;

        void updateContentBounds();

        constexpr static int SCROLL_L = 15;

    public:
        using Widget::onPressStart;
        using Widget::onPressMove;

        ScrollContainer(int16_t x, int16_t y, int16_t w, int16_t h){
            this->l_rect = {x, y, w, h};
        }

        void render() override;

        void onPressStart() override;
        void onPressMove() override;

        const std::vector<Widget*>& getChildren() const override {
            return children_;
        }

        void Add(Widget* w){
            w->SetParent(this);
            children_.push_back(w);
            this->needs_children_update = true;
            this->updateContentBounds();
        }

        Rect getScreenClipRect() const override {
            Rect dst = this->getScreenRect();
            if(horizontal_scroll){
                dst.h -= SCROLL_L;
            }
            if(vertical_scroll){
                dst.w -= SCROLL_L;
            }
            // 親がいる場合は親のクリップ範囲と交差させる
            if(parent){
                return parent->getScreenClipRect().intersection(dst);
            }
            return dst;
        }

        WidgetTools::RenderMode GetRenderMode() const override { return WidgetTools::OPAQUE; }

        int getScrollOffsetX() const override { return scroll_x; }
        int getScrollOffsetY() const override { return scroll_y; }
};