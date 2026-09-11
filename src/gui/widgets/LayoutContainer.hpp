#pragma once

#include <algorithm>
#include "gui/widgets/Widget.hpp"

// Luaアプリ等から子ウィジェットを動的に追加し、縦/横方向に自動で並べるための単純なコンテナ。
// ScrollContainerと違いスクロールは持たず、あくまで「並べる」責務だけを担う。
// 子の位置(x/y)だけを制御し、サイズは子自身に委ねる(Widget基底にsetW/setHが無いため)。
namespace LayoutContainerTools {
    enum Direction {
        VERTICAL,
        HORIZONTAL
    };

    // 主軸に対して垂直な方向(交差軸)での子の揃え方
    enum CrossAlign {
        START,
        CENTER,
        END
    };
}

class LayoutContainer : public Widget {
    private:
        std::vector<Widget*> children_;

        LayoutContainerTools::Direction direction_;
        LayoutContainerTools::CrossAlign cross_align_ = LayoutContainerTools::START;

        int16_t gap_ = 0;
        int16_t padding_ = 0;

        Rect prev_screen_rect{0, 0, 0, 0};

        int crossOffset(int child_size, int container_size) const;
        void relayout();

    public:
        // reserve_hintは典型的な子の数を渡すためのもの。上限ではなく、
        // それを超えて子を追加してもstd::vectorの通常の再確保で動作し続ける
        // (超過分はパフォーマンスが多少悪化するだけ)。
        LayoutContainer(int16_t x, int16_t y, int16_t w, int16_t h,
                         LayoutContainerTools::Direction direction = LayoutContainerTools::VERTICAL,
                         size_t reserve_hint = 8){
            this->l_rect = {x, y, w, h};
            this->direction_ = direction;
            children_.reserve(reserve_hint);
        }

        WidgetType getWidgetType() const override { return WidgetType::LayoutContainer; }

        const std::vector<Widget*>& getChildren() const override {
            return children_;
        }

        // childの所有権を引き受ける(削除は本コンテナのデストラクタ/removeChildが担う)
        void add(Widget* child){
            if(!child) return;
            child->setParent(this);
            children_.push_back(child);
            this->needs_children_update = true;
            this->relayout();
        }

        void removeChild(Widget* child) override {
            auto it = std::find(children_.begin(), children_.end(), child);
            if(it == children_.end()) return;
            children_.erase(it);
            this->relayout();
        }

        void setDirection(LayoutContainerTools::Direction direction){
            this->direction_ = direction;
            this->relayout();
        }
        LayoutContainerTools::Direction getDirection() const { return direction_; }

        void setCrossAlign(LayoutContainerTools::CrossAlign align){
            this->cross_align_ = align;
            this->relayout();
        }
        LayoutContainerTools::CrossAlign getCrossAlign() const { return cross_align_; }

        void setGap(int16_t gap){
            this->gap_ = gap;
            this->relayout();
        }
        int16_t getGap() const { return gap_; }

        void setPadding(int16_t padding){
            this->padding_ = padding;
            this->relayout();
        }
        int16_t getPadding() const { return padding_; }

        void setW(int w){
            this->l_rect.w = w;
            this->relayout();
        }
        void setH(int h){
            this->l_rect.h = h;
            this->relayout();
        }

        void render() override;

        ~LayoutContainer(){
            for(Widget* child : children_){
                delete child;
            }
        }
};
