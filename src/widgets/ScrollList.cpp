#include "ScrollList.hpp"

#include "functions/GFX_Functions.hpp"
#include "OS_Data.hpp"

void ScrollList::render(){
    if(!this->needs_redraw) return;
    if(!this->visible) return;

    //前の描画分
    PICO_GFX::markDirty(this->prev_rect);

    this->fontApply();
    this->font_h = OSData::frame->fontHeight();

    //現在の描画
    PICO_GFX::markDirty(this->rect);

    const int ITEM_HEIGHT = this->font_h + MARGIN;
    const int ITEMS_TOTAL_HEIGHT = ITEM_HEIGHT * this->dataSource->size();

    //スクロールバーの領域
    OSData::frame->drawRect(this->rect.x + this->rect.w - SCROLL_BAR_W, this->rect.y, SCROLL_BAR_W, this->rect.h, this->border_color);
    OSData::frame->fillRect(
        this->rect.x + this->rect.w - SCROLL_BAR_W,
        this->rect.y + ((float)this->scrollY / (float)ITEMS_TOTAL_HEIGHT) * this->rect.h,
        SCROLL_BAR_W,
        ((float)this->rect.h / (float)ITEMS_TOTAL_HEIGHT) * this->rect.h,
        this->border_color
    );

    //はみ出したテキストの切り取り用
    OSData::frame->setClipRect(this->rect.x, this->rect.y, this->rect.w - SCROLL_BAR_W, this->rect.h);

    //テキスト描画(本番)
    int start_index = this->scrollY / ITEM_HEIGHT;
    int draw_y = start_index * ITEM_HEIGHT - this->scrollY;
    for(int i = start_index; i < this->dataSource->size(); i++){

        if(selected_index == i){
            OSData::frame->fillRect(this->rect.x, this->rect.y + draw_y - MARGIN * 0.5, this->rect.w - SCROLL_BAR_W, ITEM_HEIGHT, this->text_color);
            OSData::frame->setTextColor(this->background_color);
        }else{
            this->textColorApply();
        }

        OSData::frame->setCursor(this->rect.x + MARGIN, this->rect.y + draw_y);
        OSData::frame->print(this->dataSource->at(i));

        draw_y += ITEM_HEIGHT;
        if(draw_y > this->rect.h) break;
    }
    this->textColorDefault();
    OSData::frame->clearClipRect();

    //枠
    OSData::frame->drawRect(this->rect.x, this->rect.y, this->rect.w, this->rect.h, this->border_color);
    this->fontDefault();

    //前の描画位置を記録
    this->prev_rect.copy(this->rect);

    this->needs_redraw = false;
}

void ScrollList::onPressStart(){
    if(this->on_press_start) this->on_press_start();

    if(OSData::touchX > this->rect.x + this->rect.w - SCROLL_BAR_W){
        this->is_scrolling = true;
        this->ref_touch_y = OSData::touchY;
        this->ref_scroll_y = this->scrollY;
    }else{
        const int ITEM_HEIGHT = this->font_h + MARGIN;

        int start_index = this->scrollY / ITEM_HEIGHT;
        int draw_y = start_index * ITEM_HEIGHT - this->scrollY;
        for(int i = start_index; i < this->dataSource->size(); i++){
            const int draw_start_y = this->rect.y + draw_y - MARGIN * 0.5;
            if(OSData::touchY > draw_start_y && OSData::touchY <= draw_start_y + ITEM_HEIGHT){
                this->selected_index = i;
                this->needs_redraw = true;
                break;
            }

            draw_y += ITEM_HEIGHT;
            if(draw_y > this->rect.h) break;
        }
    }
}

void ScrollList::onPressMove(){
    if(this->on_press_move) this->on_press_move();

    if(this->is_scrolling){
        this->scrollY = min(
            max(this->ref_scroll_y + (OSData::touchY - this->ref_touch_y) * 1.3, 0),
            (this->font_h + MARGIN) * this->dataSource->size() - this->rect.h
        );
        this->needs_redraw = true;
    }
}

void ScrollList::onPressEnd(){
    if(this->on_press_end) this->on_press_end();

    this->is_scrolling = false;
}