#include "ScrollList.hpp"

#include "functions/GFX_Functions.hpp"
#include "OS_Data.hpp"

void ScrollList::render(){
    if(!this->needs_redraw) return;
    if(!this->visible) return;

    //前の描画分
    if(this->prev_l_rect != this->l_rect)
        markdirty(this->getScreenPrevRect());

    this->fontApply();
    this->font_h = OSData::frame->fontHeight();

    const Rect g_rect = this->getScreenRect();

    //現在の描画
    markdirty(g_rect);

    const int ITEM_HEIGHT = this->font_h + MARGIN;
    const int ITEMS_TOTAL_HEIGHT = ITEM_HEIGHT * (int)this->dataSource.size();

    // TextViewと同じ細い薄灰の棒(収まっているときは描かない)
    if(ITEMS_TOTAL_HEIGHT > g_rect.h){
        int thumb_h = g_rect.h * g_rect.h / ITEMS_TOTAL_HEIGHT;
        if(thumb_h < 10) thumb_h = 10;
        const int max_scroll = ITEMS_TOTAL_HEIGHT - g_rect.h;
        const int thumb_y = g_rect.y + (g_rect.h - thumb_h) * this->scrollY / max_scroll;
        OSData::frame->fillRect(g_rect.x + g_rect.w - 3, thumb_y, 3, thumb_h, PICO_LIGHTGREY);
    }

    //はみ出したテキストの切り取り用
    int32_t orig_x, orig_y, orig_w, orig_h;
    OSData::frame->getClipRect(&orig_x, &orig_y, &orig_w, &orig_h);
    const Rect orig_clip = {(int16_t)orig_x, (int16_t)orig_y, (int16_t)orig_w, (int16_t)orig_h};
    const Rect text_area = {g_rect.x, g_rect.y, (int16_t)(g_rect.w - SCROLL_BAR_W), g_rect.h};
    const Rect text_clip = orig_clip.intersection(text_area);

    OSData::frame->setClipRect(text_clip.x, text_clip.y, text_clip.w, text_clip.h);

    //テキスト描画(本番)
    int start_index = this->scrollY / ITEM_HEIGHT;
    int draw_y = start_index * ITEM_HEIGHT - this->scrollY;

    int icon_size = FontFn::GetFontSize(this->getFontSize());

    for(int i = start_index; i < this->dataSource.size(); i++){
        const int8_t item_color = this->dataSource.at(i).color;
        int8_t l_text_color = (item_color >= 0) ? item_color : this->text_color;
        if(selected_index == i){
            OSData::frame->fillRect(g_rect.x, g_rect.y + draw_y - MARGIN * 0.5, g_rect.w - SCROLL_BAR_W, ITEM_HEIGHT, this->text_color);
            l_text_color = this->background_color;
        }
        if(this->enable_icon)
            IconRender::DrawIcon(this->dataSource.at(i).icon, IconRender::GetIconSize(icon_size), g_rect.x + MARGIN, g_rect.y + draw_y + (ITEM_HEIGHT - icon_size) / 2, l_text_color);

        OSData::frame->setCursor(g_rect.x + MARGIN + (this->enable_icon ? (icon_size + MARGIN) : 0), g_rect.y + draw_y);
        OSData::frame->setTextColor(l_text_color);
        OSData::frame->print(this->dataSource.at(i).text.c_str());

        draw_y += ITEM_HEIGHT;
        if(draw_y > g_rect.h) break;
    }
    this->textColorDefault();
    OSData::frame->setClipRect(orig_x, orig_y, orig_w, orig_h);

    //枠
    OSData::frame->drawRect(g_rect.x, g_rect.y, g_rect.w, g_rect.h, this->border_color);
    this->fontDefault();

    //前の描画位置を記録
    this->prev_l_rect.copy(this->l_rect);

    this->needs_redraw = false;
}

int ScrollList::itemIndexAt(int touch_y) const{
    const Rect g_rect = this->getScreenRect();
    const int ITEM_HEIGHT = this->font_h + MARGIN;
    if(ITEM_HEIGHT <= 0) return -1;

    int start_index = this->scrollY / ITEM_HEIGHT;
    int draw_y = start_index * ITEM_HEIGHT - this->scrollY;
    for(int i = start_index; i < (int)this->dataSource.size(); i++){
        const int draw_start_y = g_rect.y + draw_y - MARGIN * 0.5;
        if(touch_y > draw_start_y && touch_y <= draw_start_y + ITEM_HEIGHT) return i;

        draw_y += ITEM_HEIGHT;
        if(draw_y > g_rect.h) break;
    }
    return -1;
}

void ScrollList::causeOnPressStart(){
    Widget::causeOnPressStart();

    const Rect g_rect = this->getScreenRect();

    this->ref_touch_y = OSData::touchY;
    this->ref_scroll_y = this->scrollY;
    this->dragged = false;
    this->pending_index = -1;
    this->is_scrolling = OSData::touchX > g_rect.x + g_rect.w - SCROLL_BAR_W;
    // 本体のタップによる選択は、ドラッグでスクロールする可能性があるので離したときに行う
    if(!this->is_scrolling) this->pending_index = this->itemIndexAt(OSData::touchY);
}

void ScrollList::causeOnPressMove(){
    Widget::causeOnPressMove();

    const int dy = OSData::touchY - this->ref_touch_y;
    if(!this->is_scrolling && !this->dragged){
        if(dy > kDragThreshold || dy < -kDragThreshold) this->dragged = true;
        else return;
    }

    const int view_h = this->getScreenRect().h;
    const int total = (this->font_h + MARGIN) * (int)this->dataSource.size();
    if(view_h > 0 && total > view_h){
        if(this->is_scrolling){
            // スクロールバーのつまみが指に付いてくるよう「全体の高さ/表示領域の高さ」倍
            this->scrollY = this->ref_scroll_y + (int)((float)dy * total / view_h);
        }else{
            // 中身が指に付いてくる(下へなぞると上の項目が見える)
            this->scrollY = this->ref_scroll_y - dy;
        }
    }
    // 項目が少なく収まる場合は常に0(下限・上限とも範囲内へ)
    this->clampScroll();
    this->needs_redraw = true;
}

void ScrollList::causeOnPressEnd(){
    Widget::causeOnPressEnd();

    const bool tap = !this->is_scrolling && !this->dragged && this->pending_index >= 0;
    const int index = this->pending_index;
    this->is_scrolling = false;
    this->dragged = false;
    this->pending_index = -1;

    if(tap){
        const bool already_selected = index == this->selected_index;
        this->selected_index = index;
        this->needs_redraw = true;
        this->causeOnSelectItem(already_selected);
    }
}
void ScrollList::ensureVisible(int index){
    if(index < 0 || index >= (int)this->dataSource.size()) return;
    if(this->font_h == 0) this->font_h = FontFn::GetFontSize(getFontSize());
    const int item_h = this->font_h + MARGIN;
    const int top = index * item_h;
    if(top < this->scrollY) this->scrollY = top;
    else if(top + item_h > this->scrollY + this->l_rect.h) this->scrollY = top + item_h - this->l_rect.h;
    this->clampScroll();
    this->needsRender();
}

bool ScrollList::onFocusKey(FocusKey key){
    const int count = (int)this->dataSource.size();
    if(key == FocusKey::Activate){
        if(this->selected_index < 0 || this->selected_index >= count) return count > 0 ? this->onFocusKey(FocusKey::Down) : true;
        this->causeOnSelectItem(true);
        return true;
    }
    if(key != FocusKey::Up && key != FocusKey::Down) return false;
    int next = this->selected_index + (key == FocusKey::Down ? 1 : -1);
    if(this->selected_index < 0) next = 0;
    if(next < 0 || next >= count) return false;
    this->selected_index = next;
    this->ensureVisible(next);
    this->causeOnSelectItem(false);
    return true;
}
