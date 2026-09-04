#pragma once

#include "gui/widgets/Widget.hpp"

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

        ScrollContainer(int16_t x, int16_t y, int16_t w, int16_t h){
            this->l_rect = {x, y, w, h};
        }

        void render() override;

        void causeOnPressStart() override;
        void causeOnPressMove() override;

        const std::vector<Widget*>& getChildren() const override {
            return children_;
        }

        void add(Widget* w){
            w->setParent(this);
            children_.push_back(w);
            this->needs_children_update = true;
            this->updateContentBounds();
        }

        // 横スクロール専用のナビバー等、縦横どちらのスクロールバーを
        // 表示するかを構築時に切り替えたい用途向け。
        // 呼び出し後、既に追加済みの子がある場合に備えてcontentBoundsを再計算する。
        void setScrollAxes(bool horizontal, bool vertical){
            this->horizontal_scroll = horizontal;
            this->vertical_scroll = vertical;
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

        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::OPAQUE; }

        int getScrollOffsetX() const override { return scroll_x; }
        int getScrollOffsetY() const override { return scroll_y; }
};