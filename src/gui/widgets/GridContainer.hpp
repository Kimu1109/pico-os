#pragma once

#include <algorithm>
#include "gui/widgets/Widget.hpp"

// LayoutContainerの2次元版。列数を固定し、addする度に次のセルへ自動で
// 流し込む(行優先、ColorDialogの4x4グリッドと同じ考え方)。
// LayoutContainerと同様、子の位置(x/y)だけを制御しサイズは子自身に委ねる
// (Widget基底にsetW/setHが無いため)。
namespace GridContainerTools {
    // セル内での子の揃え方(横方向・縦方向で共通の列挙子を使い回す)
    enum Align {
        START,
        CENTER,
        END
    };
}

class GridContainer : public Widget {
    private:
        std::vector<Widget*> children_;

        int cols_;
        int16_t gap_ = 0;
        int16_t padding_ = 0;

        GridContainerTools::Align h_align_ = GridContainerTools::START;
        GridContainerTools::Align v_align_ = GridContainerTools::START;

        Rect prev_screen_rect{0, 0, 0, 0};

        static int crossOffset(int child_size, int container_size, GridContainerTools::Align align);
        void relayout();

    public:
        // reserve_hintは典型的な子の数を渡すためのもの。上限ではなく、
        // それを超えて子を追加してもstd::vectorの通常の再確保で動作し続ける
        // (超過分はパフォーマンスが多少悪化するだけ)。
        GridContainer(int16_t x, int16_t y, int16_t w, int16_t h,
                      int cols, size_t reserve_hint = 8){
            this->l_rect = {x, y, w, h};
            this->cols_ = std::max(1, cols);
            children_.reserve(reserve_hint);
        }

        WidgetType getWidgetType() const override { return WidgetType::GridContainer; }

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

        void setCols(int cols){
            this->cols_ = std::max(1, cols);
            this->relayout();
        }
        int getCols() const { return cols_; }

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

        void setHAlign(GridContainerTools::Align align){
            this->h_align_ = align;
            this->relayout();
        }
        void setVAlign(GridContainerTools::Align align){
            this->v_align_ = align;
            this->relayout();
        }

        void setW(int w){
            this->l_rect.w = w;
            this->relayout();
        }
        void setH(int h){
            this->l_rect.h = h;
            this->relayout();
        }

        void render() override;

        ~GridContainer(){
            for(Widget* child : children_){
                delete child;
            }
        }
};
