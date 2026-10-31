#include "gui/widgets/apps/MonthGrid.hpp"

#include "apps/calendar/Ical.hpp"
#include "gui/core/Font_Functions.hpp"
#include "gui/core/Focus_Functions.hpp"
#include "OS_Data.hpp"
#include "system/I18n_Functions.hpp"

#include <cstdio>
#include <cstring>

static const char* const WDAY_JP[7] = { TR_NOOP("日"), TR_NOOP("月"), TR_NOOP("火"), TR_NOOP("水"), TR_NOOP("木"), TR_NOOP("金"), TR_NOOP("土") };

// 予定の点の大きさ
static constexpr int kDotSize = 3;
static constexpr int kDotGap = 2;

// 幅を7で割った余りは最後の列(土曜)へ足す。TabBarと同じく、右端の罫線を浮かせないため
int MonthGrid::cellX(int col) const {
    return col * (this->l_rect.w / kCols);
}

int MonthGrid::cellW(int col) const {
    const int base = this->l_rect.w / kCols;
    if(col == kCols - 1) return this->l_rect.w - base * (kCols - 1);
    return base;
}

int MonthGrid::cellH() const {
    return (this->l_rect.h - kHeaderH) / kRows;
}

void MonthGrid::setMonth(int y, int m){
    if(m < 1 || m > 12) return;
    if(y == this->year && m == this->month) return;

    this->year = y;
    this->month = m;
    this->first_wday = Ical::Weekday(Ical::DaysFromCivil(y, m, 1));
    this->days_in_month = Ical::DaysInMonth(y, m);
    if(this->selected > this->days_in_month) this->selected = this->days_in_month;
    this->needsRender();
}

void MonthGrid::setToday(int day){
    if(day == this->today) return;
    this->today = day;
    this->needsRender();
}

void MonthGrid::setSelected(int day){
    if(day < 0 || day > this->days_in_month) day = 0;
    if(day == this->selected) return;
    this->selected = day;
    this->needsRender();
}

void MonthGrid::setDots(const DayDots* dots_by_day){
    bool changed = false;
    for(int d = 1; d < 32; d++){
        DayDots next;
        if(d <= this->days_in_month) next = dots_by_day[d];
        DayDots& cur = this->dots[d];
        if(cur.count != next.count || memcmp(cur.colors, next.colors, sizeof(cur.colors)) != 0){
            cur = next;
            changed = true;
        }
    }
    if(changed) this->needsRender();
}

int MonthGrid::dayAt(int sx, int sy) const {
    const Rect g = this->getScreenRect();
    const int lx = sx - g.x;
    const int ly = sy - g.y - kHeaderH;
    const int ch = this->cellH();
    if(lx < 0 || ly < 0 || ch <= 0) return 0;

    const int row = ly / ch;
    if(row >= kRows) return 0;

    int col = -1;
    for(int c = 0; c < kCols; c++){
        const int x = this->cellX(c);
        if(lx >= x && lx < x + this->cellW(c)){ col = c; break; }
    }
    if(col < 0) return 0;

    const int day = row * kCols + col - this->first_wday + 1;
    return (day >= 1 && day <= this->days_in_month) ? day : 0;
}

void MonthGrid::causeOnPressStart(){
    Widget::causeOnPressStart();

    const int day = this->dayAt(OSData::touchX, OSData::touchY);
    if(day == 0) return; //前後の月の空きマスと見出しは何もしない

    this->setSelected(day);
    if(this->on_select_day) this->on_select_day(day);
}

Rect MonthGrid::focusRect() const {
    const Rect g = this->getScreenRect();
    const int day = this->selected > 0 ? this->selected : 1;
    const int i = day - 1 + this->first_wday;
    const int col = i % kCols;
    return {(int16_t)(g.x + this->cellX(col)), (int16_t)(g.y + kHeaderH + (i / kCols) * this->cellH()),
            (int16_t)this->cellW(col), (int16_t)this->cellH()};
}

bool MonthGrid::onFocusKey(FocusKey key){
    int day = this->selected;
    switch(key){
        case FocusKey::Activate:
            if(day == 0) day = 1;
            break;
        case FocusKey::Left:  day -= 1; break;
        case FocusKey::Right: day += 1; break;
        case FocusKey::Up:    day -= kCols; break;
        case FocusKey::Down:  day += kCols; break;
        default: return false;
    }
    //まだ選んでいなければ、最初の矢印は1日を選ぶだけ
    if(this->selected == 0) day = 1;
    if(day < 1 || day > this->days_in_month) return false;
    this->setSelected(day);
    if(this->on_select_day) this->on_select_day(day);
    return true;
}

void MonthGrid::render(){
    if(!this->needs_redraw) return;
    if(!this->visible) return;

    if(this->prev_l_rect != this->l_rect)
        markdirty(getScreenPrevRect());

    const Rect g = this->getScreenRect();
    markdirty(g);

    FontFn::SetSmall();
    const int str_h = OSData::frame->fontHeight();
    const int ch = this->cellH();

    auto wday_color = [&](int col) -> int8_t {
        if(col == 0) return PICO_RED;
        if(col == 6) return PICO_BLUE;
        return this->border_color;
    };

    // ---- 曜日の見出し ----
    for(int c = 0; c < kCols; c++){
        const int x = g.x + this->cellX(c);
        const int w = this->cellW(c);
        OSData::frame->setTextColor(wday_color(c));
        const char* wday = I18nFunctions::Tr(WDAY_JP[c]);
        OSData::frame->setCursor(x + (w - OSData::frame->textWidth(wday)) / 2,
                                 g.y + (kHeaderH - str_h) / 2);
        OSData::frame->print(wday);
    }
    OSData::frame->drawFastHLine(g.x, g.y + kHeaderH - 1, g.w, this->border_color);

    // ---- 日付の格子 ----
    for(int i = 0; i < kRows * kCols; i++){
        const int day = i - this->first_wday + 1;
        if(day < 1 || day > this->days_in_month) continue;

        const int col = i % kCols;
        const int row = i / kCols;
        const int x = g.x + this->cellX(col);
        const int y = g.y + kHeaderH + row * ch;
        const int w = this->cellW(col);

        const bool is_selected = (day == this->selected);
        int8_t text_color = wday_color(col);

        if(is_selected){
            OSData::frame->fillRect(x + 1, y + 1, w - 2, ch - 2, this->border_color);
            text_color = this->background_color;
        }
        if(day == this->today){
            OSData::frame->drawRect(x, y, w, ch, PICO_RED);
            OSData::frame->drawRect(x + 1, y + 1, w - 2, ch - 2, PICO_RED);
        }

        char buf[4];
        snprintf(buf, sizeof(buf), "%d", day);
        OSData::frame->setTextColor(text_color);
        OSData::frame->setCursor(x + (w - OSData::frame->textWidth(buf)) / 2, y + 2);
        OSData::frame->print(buf);

        const DayDots& dd = this->dots[day];
        const int dots = (dd.count < kMaxDots) ? dd.count : kMaxDots;
        if(dots > 0){
            const int dots_w = dots * kDotSize + (dots - 1) * kDotGap;
            const int dot_y = y + 2 + str_h + 1;
            for(int k = 0; k < dots; k++){
                //選択中は黒地なので点を白抜きにする(暗い色の点は黒地で見えない)
                const int8_t dot_color = is_selected ? this->background_color : dd.colors[k];
                OSData::frame->fillRect(x + (w - dots_w) / 2 + k * (kDotSize + kDotGap), dot_y,
                                        kDotSize, kDotSize, dot_color);
            }
        }
    }

    // フォーカスの枠(キー/コントローラーで選んでいる日)
    if(FocusFunctions::ring_visible && FocusFunctions::focused == this){
        const Rect r = this->focusRect();
        for(int k = 0; k < FocusFunctions::kRingWidth; k++){
            OSData::frame->drawRect(r.x + k, r.y + k, r.w - 2 * k, r.h - 2 * k, FocusFunctions::kRingColor);
        }
    }

    OSData::frame->setTextColor(PICO_BLACK);
    FontFn::SetDefault();

    this->prev_l_rect.copy(this->getLocalRect());
    this->needs_redraw = false;
}
