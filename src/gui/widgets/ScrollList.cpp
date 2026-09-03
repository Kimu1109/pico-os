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
    const int ITEMS_TOTAL_HEIGHT = ITEM_HEIGHT * this->dataSource->size();

    //スクロールバーの領域
    OSData::frame->drawRect(g_rect.x + g_rect.w - SCROLL_BAR_W, g_rect.y, SCROLL_BAR_W, g_rect.h, this->border_color);
    OSData::frame->fillRect(
        g_rect.x + g_rect.w - SCROLL_BAR_W + 2,
        g_rect.y + ((float)this->scrollY / (float)ITEMS_TOTAL_HEIGHT) * g_rect.h + 2,
        SCROLL_BAR_W - 4,
        ((float)g_rect.h / (float)ITEMS_TOTAL_HEIGHT) * g_rect.h - 4,
        this->border_color
    );

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

    for(int i = start_index; i < this->dataSource->size(); i++){
        int8_t l_text_color = this->text_color;
        if(selected_index == i){
            OSData::frame->fillRect(g_rect.x, g_rect.y + draw_y - MARGIN * 0.5, g_rect.w - SCROLL_BAR_W, ITEM_HEIGHT, this->text_color);
            l_text_color = this->background_color;
        }
        if(this->enable_icon)
            IconRender::DrawIcon(this->dataSource->at(i).icon, IconRender::GetIconSize(icon_size), g_rect.x + MARGIN, g_rect.y + draw_y + (ITEM_HEIGHT - icon_size) / 2, l_text_color);

        OSData::frame->setCursor(g_rect.x + MARGIN + (this->enable_icon ? (icon_size + MARGIN) : 0), g_rect.y + draw_y);
        OSData::frame->setTextColor(l_text_color);
        OSData::frame->print(this->dataSource->at(i).text);

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

void ScrollList::causeOnPressStart(){
    Widget::causeOnPressStart();

    const Rect g_rect = this->getScreenRect();

    if(OSData::touchX > g_rect.x + g_rect.w - SCROLL_BAR_W){
        this->is_scrolling = true;
        this->ref_touch_y = OSData::touchY;
        this->ref_scroll_y = this->scrollY;
    }else{
        const int ITEM_HEIGHT = this->font_h + MARGIN;

        int start_index = this->scrollY / ITEM_HEIGHT;
        int draw_y = start_index * ITEM_HEIGHT - this->scrollY;
        for(int i = start_index; i < this->dataSource->size(); i++){
            const int draw_start_y = g_rect.y + draw_y - MARGIN * 0.5;
            if(OSData::touchY > draw_start_y && OSData::touchY <= draw_start_y + ITEM_HEIGHT){
                bool already_selected = i == this->selected_index;
                this->selected_index = i;
                this->causeOnSelectItem(already_selected);
                this->needs_redraw = true;
                break;
            }

            draw_y += ITEM_HEIGHT;
            if(draw_y > g_rect.h) break;
        }
    }
}

void ScrollList::causeOnPressMove(){
    Widget::causeOnPressMove();

    if(this->is_scrolling){
        this->scrollY = min(
            max(this->ref_scroll_y + (OSData::touchY - this->ref_touch_y) * PICO_SCROLL_EX, 0),
            (this->font_h + MARGIN) * this->dataSource->size() - this->getScreenRect().h
        );
        this->needs_redraw = true;
    }
}

void ScrollList::causeOnPressEnd(){
    Widget::causeOnPressEnd();

    this->is_scrolling = false;
}