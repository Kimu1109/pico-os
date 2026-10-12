#include "gui/widgets/apps/CalibView.hpp"
#include "OS_Data.hpp"

void CalibView::clearMarks(){
    this->mark_count = 0;
    this->active = -1;
    this->needsRender();
}

int CalibView::addMark(int x, int y){
    if(this->mark_count >= kMaxMarks) return -1;
    this->marks[this->mark_count] = { (int16_t)x, (int16_t)y, false };
    this->needsRender();
    return this->mark_count++;
}

void CalibView::setMarkDone(int index, bool done){
    if(index < 0 || index >= this->mark_count) return;
    if(this->marks[index].done == done) return;
    this->marks[index].done = done;
    this->needsRender();
}

void CalibView::setActive(int index){
    if(this->active == index) return;
    this->active = index;
    this->needsRender();
}

void CalibView::setProbe(bool show, int x, int y){
    if(this->probe_show == show && this->probe_x == x && this->probe_y == y) return;
    this->probe_show = show;
    this->probe_x = (int16_t)x;
    this->probe_y = (int16_t)y;
    this->needsRender();
}

void CalibView::render(){
    if(!this->needs_redraw) return;
    if(!this->visible) return;

    const Rect g = this->getScreenRect();
    markdirty(g);

    OSData::frame->fillRect(g.x, g.y, g.w, g.h, this->background_color);

    for(int i = 0; i < this->mark_count; i++){
        const Mark& m = this->marks[i];
        if(i == this->active){
            // 狙う点: 大きな十字と二重の輪
            OSData::frame->drawFastHLine(m.x - 12, m.y, 25, PICO_RED);
            OSData::frame->drawFastVLine(m.x, m.y - 12, 25, PICO_RED);
            OSData::frame->drawCircle(m.x, m.y, 7, PICO_RED);
            OSData::frame->drawCircle(m.x, m.y, 3, PICO_RED);
        }else{
            // それ以外: 済みは小さな灰色、未は薄い十字
            const int8_t c = m.done ? PICO_DARKGREY : PICO_LIGHTGREY;
            OSData::frame->drawFastHLine(m.x - 4, m.y, 9, c);
            OSData::frame->drawFastVLine(m.x, m.y - 4, 9, c);
        }
    }

    if(this->probe_show){
        OSData::frame->fillCircle(this->probe_x, this->probe_y, 3, PICO_BLUE);
        OSData::frame->drawCircle(this->probe_x, this->probe_y, 6, PICO_BLUE);
    }

    this->prev_l_rect.copy(this->l_rect);
    this->needs_redraw = false;
}
