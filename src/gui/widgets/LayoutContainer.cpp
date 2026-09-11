#include "gui/widgets/LayoutContainer.hpp"

int LayoutContainer::crossOffset(int child_size, int container_size) const {
    switch(cross_align_){
        case LayoutContainerTools::CENTER:
            return std::max(0, (container_size - child_size) / 2);
        case LayoutContainerTools::END:
            return std::max(0, container_size - child_size);
        case LayoutContainerTools::START:
        default:
            return 0;
    }
}

void LayoutContainer::relayout() {
    const int16_t inner_w = std::max(0, (int)this->l_rect.w - padding_ * 2);
    const int16_t inner_h = std::max(0, (int)this->l_rect.h - padding_ * 2);

    int cursor = padding_;

    for(Widget* child : children_){
        if(!child->getVisible()) continue;

        if(direction_ == LayoutContainerTools::VERTICAL){
            child->setX(padding_ + crossOffset(child->getW(), inner_w));
            child->setY(cursor);
            cursor += child->getH() + gap_;
        }else{
            child->setX(cursor);
            child->setY(padding_ + crossOffset(child->getH(), inner_h));
            cursor += child->getW() + gap_;
        }
    }

    this->needsRender();
}

void LayoutContainer::render() {
    if(!this->needs_redraw) return;
    if(!this->visible) return;

    // グローバル座標で前回位置と比較し、移動していれば旧位置を再描画対象にする
    const Rect g_rect = this->getScreenRect();
    if(this->prev_screen_rect != g_rect){
        markdirty(this->prev_screen_rect);
    }
    this->prev_screen_rect = g_rect;

    this->needs_redraw = false;
}
