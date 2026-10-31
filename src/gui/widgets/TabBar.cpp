#include "gui/widgets/TabBar.hpp"

#include "gui/core/GFX_Functions.hpp"
#include "OS_Data.hpp"

int TabBar::tabX(int index) const {
    if(this->tab_count <= 0) return 0;
    if(this->fit_valid){
        int x = 0;
        for(int i = 0; i < index && i < this->tab_count; i++) x += this->fit_w[i];
        return x;
    }
    return index * (this->l_rect.w / this->tab_count);
}

int TabBar::tabW(int index) const {
    if(this->tab_count <= 0) return 0;
    if(this->fit_valid) return (index >= 0 && index < this->tab_count) ? this->fit_w[index] : 0;
    const int base = this->l_rect.w / this->tab_count;
    //最後のタブが余りを吸収する(右端に隙間を残さない)
    if(index == this->tab_count - 1) return this->l_rect.w - base * (this->tab_count - 1);
    return base;
}

void TabBar::relayoutFit(){
    this->fit_valid = false;
    if(!this->fit_labels || this->tab_count <= 0) return;

    //文字の実寸+左右の余白。足りなければ均等割り(折り返し)のまま
    this->fontApply();
    int total = 0;
    const int icon_px = IconRender::IconPixelSize(this->iconSize());
    for(int i = 0; i < this->tab_count; i++){
        const int content_w = this->hasIcon(i) ? icon_px : OSData::frame->textWidth(this->labels[i].c_str());
        this->fit_w[i] = (int16_t)(content_w + kTextPadding * 2 + 2);
        total += this->fit_w[i];
    }
    this->fontDefault();
    if(total > this->l_rect.w) return;

    //余りは全部のタブへ均等に配り、割り切れない分は最後のタブへ(右端に隙間を残さない)
    const int extra = (this->l_rect.w - total) / this->tab_count;
    int used = 0;
    for(int i = 0; i < this->tab_count; i++){
        this->fit_w[i] = (int16_t)(this->fit_w[i] + extra);
        used += this->fit_w[i];
    }
    this->fit_w[this->tab_count - 1] = (int16_t)(this->fit_w[this->tab_count - 1] + (this->l_rect.w - used));
    this->fit_valid = true;
}

IconSize TabBar::iconSize() const {
    //上下に2pxずつ余白が取れる大きさ(24pxが入らなければ16px)
    return (this->l_rect.h >= IconRender::IconPixelSize(IconSize::Px24) + 4) ? IconSize::Px24 : IconSize::Px16;
}

bool TabBar::addTab(const char* label){
    if(this->tab_count >= kMaxTabs) return false;

    this->labels[this->tab_count].assign(label);
    this->icon_mask = (uint8_t)(this->icon_mask & ~(1u << this->tab_count));
    this->tab_count++;
    this->relayoutFit();
    this->needsRender();
    return true;
}

bool TabBar::addTab(IconID icon, const char* label){
    if(this->tab_count >= kMaxTabs) return false;
    const int index = this->tab_count;
    if(!this->addTab(label ? label : "")) return false;
    this->icons[index] = icon;
    this->icon_mask = (uint8_t)(this->icon_mask | (1u << index));
    this->relayoutFit();
    return true;
}

bool TabBar::setLabel(int index, const char* label){
    if(index < 0 || index >= this->tab_count) return false;
    this->labels[index].assign(label);
    this->relayoutFit();
    this->needsRender();
    return true;
}

bool TabBar::removeTab(int index){
    if(index < 0 || index >= this->tab_count) return false;
    for(int i = index; i + 1 < this->tab_count; i++){
        this->labels[i] = this->labels[i + 1];
        this->icons[i] = this->icons[i + 1];
    }
    //index より後ろの印を1つずつ前へ詰める
    const uint8_t low = (uint8_t)(this->icon_mask & ((1u << index) - 1));
    const uint8_t high = (uint8_t)((this->icon_mask >> (index + 1)) << index);
    this->icon_mask = (uint8_t)(low | high);
    this->labels[this->tab_count - 1].clear();
    this->tab_count--;
    if(this->selected > index) this->selected--;
    if(this->selected >= this->tab_count) this->selected = this->tab_count > 0 ? this->tab_count - 1 : 0;
    this->relayoutFit();
    this->needsRender();
    return true;
}

void TabBar::clearTabs(){
    for(int i = 0; i < this->tab_count; i++) this->labels[i].clear();
    this->icon_mask = 0;
    this->tab_count = 0;
    this->selected = 0;
    this->relayoutFit();
    this->needsRender();
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

bool TabBar::isSingleLatinWord(const char* s){
    if(!*s) return false;
    for(; *s; ++s){
        if(*s == ' ' || (uint8_t)*s >= 0x80) return false;
    }
    return true;
}

int TabBar::wrapOffset(const FixedString<PICO_STR_M>& label, int budget_w) const {
    //フォントはfontApply()済みである前提(呼び出し元のrender()が一度だけ適用する)
    if(OSData::frame->textWidth(label.c_str()) <= budget_w) return 0;

    const int chars = FixedString<PICO_STR_M>::charCount(label.c_str());
    if(chars <= 1) return 0; //1文字で溢れるなら折り返しても意味が無い
    //空白の無い英字の1語(訳した "Display" 等)は語の途中で切らず、render() が横に縮めて1行に収める
    if(isSingleLatinWord(label.c_str())) return 0;

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

    //英語等の語の区切り(空白)があれば、入りきる範囲の最後の空白で折り返す(次の行は空白の後から)
    for(int b = last_fit; b > 0; b--){
        if(label.c_str()[b] == ' ') return b + 1;
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

        if(this->hasIcon(i)){
            const IconSize size = this->iconSize();
            const int px = IconRender::IconPixelSize(size);
            IconRender::DrawIcon(this->icons[i], size, x + (w - px) / 2, g_rect.y + (g_rect.h - px) / 2,
                                 is_selected ? this->background_color : this->border_color);
            continue;
        }

        const int str_h = OSData::frame->fontHeight();

        // 1行に収まらないラベルは2行へ折り返す。
        // 「ストップウォッチ」のような長い機能名でも、タブを広げずに全部見せるため
        // (高さが足りない場合は上下がはみ出すので、呼び出し側でhを確保しておくこと)
        const int split = this->wrapOffset(this->labels[i], w - kTextPadding * 2);

        if(split == 0){
            const char* label = this->labels[i].c_str();
            int str_w = OSData::frame->textWidth(label);
            //入りきらない英字の1語は横だけ縮める(wrapOffset() 参照)
            const int budget_w = w - kTextPadding * 2;
            const float sx = OSData::frame->getTextSizeX();
            const float sy = OSData::frame->getTextSizeY();
            const bool squeeze = str_w > budget_w && budget_w > 0 && isSingleLatinWord(label);
            if(squeeze){
                OSData::frame->setTextSize(sx * budget_w / str_w, sy);
                str_w = budget_w;
            }
            OSData::frame->setCursor(x + (w - str_w) / 2, g_rect.y + (g_rect.h - str_h) / 2);
            OSData::frame->print(label);
            if(squeeze) OSData::frame->setTextSize(sx, sy);
        }else{
            FixedString<PICO_STR_M> line;
            const int top = g_rect.y + (g_rect.h - str_h * 2) / 2;

            //空白で折り返したときは1行目の末尾の空白を落とす(中央へ寄せるときにずれないように)
            size_t head_len = (size_t)split;
            while(head_len > 0 && this->labels[i].c_str()[head_len - 1] == ' ') head_len--;
            line.assign(this->labels[i].c_str(), head_len);
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
