#include "widgets/ScrollContainer.hpp"
#include "OS_Data.hpp"
#include "functions/GFX_Functions.hpp"

void ScrollContainer::updateContentBounds() {
    int max_x = 0;
    int max_y = 0;

    for (Widget* child : children_) {
        if (!child->Visible()) continue;
        const Rect r = child->getLocalRect();
        if (r.x + r.w > max_x) max_x = r.x + r.w;
        if (r.y + r.h > max_y) max_y = r.y + r.h;
    }

    const int view_w = this->l_rect.w - (vertical_scroll ? SCROLL_L : 0);
    const int view_h = this->l_rect.h - (horizontal_scroll ? SCROLL_L : 0);

    max_scroll_x = std::max(0, max_x - view_w);
    max_scroll_y = std::max(0, max_y - view_h);
}

void ScrollContainer::onPressStart(){
    Widget::onPressStart();

    sx = OSData::touchX - getScreenX();
    sy = OSData::touchY - getScreenY();

    is_scrolling = false;
    if(vertical_scroll && sx >= this->l_rect.w - SCROLL_L){
        is_scrolling = true;
    }
    if(horizontal_scroll && sy >= this->l_rect.h - SCROLL_L){
        is_scrolling = true;
    }

    if(is_scrolling){
        this->updateContentBounds();
    }

    s_scroll_x = scroll_x;
    s_scroll_y = scroll_y;
}

void ScrollContainer::onPressMove(){
    Widget::onPressMove();

    if(!is_scrolling) return;

    int rx = OSData::touchX - getScreenX();
    int ry = OSData::touchY - getScreenY();

    int old_scroll_x = scroll_x;
    int old_scroll_y = scroll_y;

    if(horizontal_scroll){
        int new_x = s_scroll_x + (rx - sx) * PICO_SCROLL_EX;
        scroll_x = constrain(new_x, 0, max_scroll_x);
    }

    if(vertical_scroll){
        int new_y = s_scroll_y + (ry - sy) * PICO_SCROLL_EX;
        scroll_y = constrain(new_y, 0, max_scroll_y);
    }

    if(old_scroll_x != scroll_x || old_scroll_y != scroll_y){
        this->needsRender();
        for(Widget* child : children_){
            child->needsRender();
        }
    }
}

void ScrollContainer::render() {
    if(prev_l_rect != l_rect){
        markdirty(this->getScreenPrevRect());
    }

    const Rect g_rect = this->getScreenRect();

    // 外枠
    OSData::frame->drawRect(g_rect.x, g_rect.y, g_rect.w, g_rect.h, PICO_BLACK);

    // 垂直スクロールバー
    if(vertical_scroll){
        const int bar_x = g_rect.x + g_rect.w - SCROLL_L;
        const int bar_y = g_rect.y;
        const int bar_w = SCROLL_L;
        const int bar_h = g_rect.h - (horizontal_scroll ? SCROLL_L : 0);

        // トラック枠
        OSData::frame->drawRect(bar_x, bar_y, bar_w, bar_h, PICO_BLACK);

        // ツマミ
        if(max_scroll_y > 0){
            const int total_h = max_scroll_y + bar_h;
            int thumb_h = std::max(10, (bar_h * bar_h) / total_h);
            if(thumb_h > bar_h) thumb_h = bar_h;
            const int thumb_y = bar_y + (scroll_y * (bar_h - thumb_h)) / max_scroll_y;

            OSData::frame->fillRect(bar_x + 2, thumb_y + 2, bar_w - 4, std::max(1, thumb_h - 4), PICO_BLACK);
        }
    }

    // 水平スクロールバー
    if(horizontal_scroll){
        const int bar_x = g_rect.x;
        const int bar_y = g_rect.y + g_rect.h - SCROLL_L;
        const int bar_w = g_rect.w - (vertical_scroll ? SCROLL_L : 0);
        const int bar_h = SCROLL_L;

        // トラック枠
        OSData::frame->drawRect(bar_x, bar_y, bar_w, bar_h, PICO_BLACK);

        // ツマミ
        if(max_scroll_x > 0){
            const int total_w = max_scroll_x + bar_w;
            int thumb_w = std::max(10, (bar_w * bar_w) / total_w);
            if(thumb_w > bar_w) thumb_w = bar_w;
            const int thumb_x = bar_x + (scroll_x * (bar_w - thumb_w)) / max_scroll_x;

            OSData::frame->fillRect(thumb_x + 2, bar_y + 2, std::max(1, thumb_w - 4), bar_h - 4, PICO_BLACK);
        }
    }

    markdirty(g_rect);
    prev_l_rect.copy(l_rect);
}