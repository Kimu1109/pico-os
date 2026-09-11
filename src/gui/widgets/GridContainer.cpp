#include "gui/widgets/GridContainer.hpp"

int GridContainer::crossOffset(int child_size, int container_size, GridContainerTools::Align align) {
    switch(align){
        case GridContainerTools::CENTER:
            return std::max(0, (container_size - child_size) / 2);
        case GridContainerTools::END:
            return std::max(0, container_size - child_size);
        case GridContainerTools::START:
        default:
            return 0;
    }
}

void GridContainer::relayout() {
    const int16_t inner_w = std::max(0, (int)this->l_rect.w - padding_ * 2);
    const int total_col_gap = gap_ * (cols_ - 1);
    const int cell_w = std::max(0, ((int)inner_w - total_col_gap) / cols_);

    // 非表示の子は行優先の流し込み対象から外す(LayoutContainerと同じ挙動)
    std::vector<Widget*> visible;
    visible.reserve(children_.size());
    for(Widget* child : children_){
        if(child->getVisible()) visible.push_back(child);
    }

    if(visible.empty()){
        this->needsRender();
        return;
    }

    const int rows = (int)((visible.size() + cols_ - 1) / cols_);

    // 各行の高さは、その行に入る子のうち一番背の高いものに合わせる
    std::vector<int> row_heights(rows, 0);
    for(size_t i = 0; i < visible.size(); i++){
        int r = (int)(i / cols_);
        row_heights[r] = std::max(row_heights[r], visible[i]->getH());
    }

    int cursor_y = padding_;
    for(int r = 0; r < rows; r++){
        for(int c = 0; c < cols_; c++){
            const size_t idx = (size_t)r * cols_ + c;
            if(idx >= visible.size()) break;
            Widget* child = visible[idx];

            const int cell_x = padding_ + c * (cell_w + gap_);
            child->setX(cell_x + crossOffset(child->getW(), cell_w, h_align_));
            child->setY(cursor_y + crossOffset(child->getH(), row_heights[r], v_align_));
        }
        cursor_y += row_heights[r] + gap_;
    }

    this->needsRender();
}

void GridContainer::render() {
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
