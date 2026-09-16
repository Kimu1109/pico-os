#include "gui/widgets/TabBar.hpp"

#include "functions/GFX_Functions.hpp"
#include "OS_Data.hpp"

int TabBar::tabX(int index) const {
    if(this->tab_count <= 0) return 0;
    return index * (this->l_rect.w / this->tab_count);
}

int TabBar::tabW(int index) const {
    if(this->tab_count <= 0) return 0;
    const int base = this->l_rect.w / this->tab_count;
    //最後のタブが余りを吸収する(右端に隙間を残さない)
    if(index == this->tab_count - 1) return this->l_rect.w - base * (this->tab_count - 1);
    return base;
}

bool TabBar::addTab(const char* label){
    if(this->tab_count >= kMaxTabs) return false;

    this->labels[this->tab_count].assign(label);
    this->tab_count++;
    this->needsRender();
    return true;
}

void TabBar::setSelected(int index, bool notify){
    if(index < 0 || index >= this->tab_count) return;
    if(index == this->selected) return;

    this->selected = index;
    this->needsRender();

    if(notify && this->on_changed) this->on_changed(index);
}

void TabBar::causeOnPressStart(){
    Widget::causeOnPressStart();

    const Rect g_rect = this->getScreenRect();
    const int local_x = OSData::touchX - g_rect.x;

    for(int i = 0; i < this->tab_count; i++){
        const int x = this->tabX(i);
        if(local_x >= x && local_x < x + this->tabW(i)){
            this->setSelected(i, true);
            break;
        }
    }
}

int TabBar::wrapOffset(const FixedString<PICO_STR_M>& label, int budget_w) const {
    //フォントはfontApply()済みである前提(呼び出し元のrender()が一度だけ適用する)
    if(OSData::frame->textWidth(label.c_str()) <= budget_w) return 0;

    const int chars = FixedString<PICO_STR_M>::charCount(label.c_str());
    if(chars <= 1) return 0; //1文字で溢れるなら折り返しても意味が無い

    //収まる最大の文字数を探す。バイトではなく文字単位で刻むのは、
    //UTF-8の途中で切ると文字が丸ごと化けるため
    FixedString<PICO_STR_M> head;
    int last_fit = 0;

    for(int i = 1; i < chars; i++){
        const int bytes = FixedString<PICO_STR_M>::byteOffsetOfChar(label.c_str(), i);
        head.assign(label.c_str(), (size_t)bytes);
        if(OSData::frame->textWidth(head.c_str()) > budget_w) break;
        last_fit = bytes;
    }

    //先頭1文字すら入らない場合は諦めて1行のまま(はみ出させる)
    return last_fit;
}

void TabBar::render(){
    if(!this->needs_redraw) return;
    if(!this->visible) return;
    if(this->tab_count <= 0) return;

    //前回の描画内容の変更(削除)
    if(this->prev_l_rect != this->l_rect)
        markdirty(getScreenPrevRect());

    const Rect g_rect = this->getScreenRect();
    markdirty(g_rect);

    this->fontApply();

    for(int i = 0; i < this->tab_count; i++){
        const int x = g_rect.x + this->tabX(i);
        const int w = this->tabW(i);
        const bool is_selected = (i == this->selected);

        if(is_selected){
            OSData::frame->fillRect(x, g_rect.y, w, g_rect.h, this->border_color);
        }
        OSData::frame->drawRect(x, g_rect.y, w, g_rect.h, this->border_color);

        OSData::frame->setTextColor(is_selected ? this->background_color : this->border_color);

        const int str_h = OSData::frame->fontHeight();

        // 1行に収まらないラベルは2行へ折り返す。
        // 「ストップウォッチ」のような長い機能名でも、タブを広げずに全部見せるため
        // (高さが足りない場合は上下がはみ出すので、呼び出し側でhを確保しておくこと)
        const int split = this->wrapOffset(this->labels[i], w - kTextPadding * 2);

        if(split == 0){
            const char* label = this->labels[i].c_str();
            const int str_w = OSData::frame->textWidth(label);
            OSData::frame->setCursor(x + (w - str_w) / 2, g_rect.y + (g_rect.h - str_h) / 2);
            OSData::frame->print(label);
        }else{
            FixedString<PICO_STR_M> line;
            const int top = g_rect.y + (g_rect.h - str_h * 2) / 2;

            line.assign(this->labels[i].c_str(), (size_t)split);
            OSData::frame->setCursor(x + (w - OSData::frame->textWidth(line.c_str())) / 2, top);
            OSData::frame->print(line.c_str());

            line.assign(this->labels[i].c_str() + split);
            OSData::frame->setCursor(x + (w - OSData::frame->textWidth(line.c_str())) / 2, top + str_h);
            OSData::frame->print(line.c_str());
        }
    }

    OSData::frame->setTextColor(PICO_BLACK);
    this->fontDefault();

    this->prev_l_rect.copy(this->getLocalRect());
    this->needs_redraw = false;
}
