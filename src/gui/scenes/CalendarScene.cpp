#include "gui/scenes/CalendarScene.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Time_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "storage/SD_IO.hpp"
#include "storage/SD_Path.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstring>
#include <ctime>

static const char* const WDAY_JP[7] = { "日", "月", "火", "水", "木", "金", "土" };

// 格子1行の高さ。16pxの数字 + 予定の点(3px) + 余白
static constexpr int kGridRowH = 24;

// 「2026年12月」が収まる幅。84pxでは2行へ折り返した(数字の字幅が8pxより広い)ので余裕を持たせる
static constexpr int kTitleW = 100;

// NTP同期前の時計(1970年代)を「今日」として扱わないための下限
static constexpr int kMinValidYear = 2020;

int32_t CalendarScene::LocalUtcOffsetSec(){
    const time_t now = time(nullptr);
    struct tm l, u;
    localtime_r(&now, &l);
    gmtime_r(&now, &u);

    auto secs = [](const struct tm& t) -> int64_t {
        return (int64_t)Ical::DaysFromCivil(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday) * 86400
             + t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec;
    };
    return (int32_t)(secs(l) - secs(u));
}

bool CalendarScene::refreshToday(){
    const struct tm& t = TimeFunctions::timeinfo;
    int y = t.tm_year + 1900;
    int m = t.tm_mon + 1;
    int d = t.tm_mday;
    if(y < kMinValidYear || m < 1 || m > 12 || d < 1){ y = 0; m = 0; d = 0; }

    if(y == this->today_year && m == this->today_month && d == this->today_day) return false;
    this->today_year = y;
    this->today_month = m;
    this->today_day = d;
    return true;
}

// ---------------------------------------------------------------------------
// 読み込み
// ---------------------------------------------------------------------------

namespace {
    bool EndsWithIcs(const char* name){
        const size_t n = strlen(name);
        if(n < 4) return false;
        const char* ext = name + n - 4;
        return ext[0] == '.' &&
               (ext[1] == 'i' || ext[1] == 'I') &&
               (ext[2] == 'c' || ext[2] == 'C') &&
               (ext[3] == 's' || ext[3] == 'S');
    }
}

void CalendarScene::reload(){
    this->cal.clear();
    this->loaded_files = 0;
    memset(this->counts, 0, sizeof(this->counts));

    if(!OSData::SD_usable) return;

    const int32_t first = Ical::DaysFromCivil(this->view_year, this->view_month, 1);
    const int dim = Ical::DaysInMonth(this->view_year, this->view_month);

    //格子に見えている範囲(前後の月の空きマスを含む6週間)だけを読む
    Ical::Options opt;
    opt.utc_offset_sec = LocalUtcOffsetSec();
    opt.window_from_day = first - Ical::Weekday(first);
    opt.window_to_day = opt.window_from_day + MonthGrid::kRows * MonthGrid::kCols;

    FsFile dir = OSData::SD.open(PICO_Path::DIR::CALENDAR);
    if(!dir) return;

    FsFile file;
    while(file.openNext(&dir, O_RDONLY)){
        char name[128];
        const bool ok = file.getName(name, sizeof(name)) && !file.isDirectory() && EndsWithIcs(name);
        //ParseFile()が同じファイルを開き直すので、先に閉じておく
        file.close();
        if(!ok) continue;

        FixedString<PICO_PATH_LEN> path;
        PICO_IO::join(path, PICO_Path::DIR::CALENDAR, name);
        if(Ical::ParseFile(path.c_str(), this->cal, opt)) this->loaded_files++;
    }
    dir.close();

    for(int d = 1; d <= dim; d++){
        const int32_t day = first + (d - 1);
        int n = 0;
        for(int i = 0; i < this->cal.count; i++){
            if(Ical::OccursOn(this->cal.events[i], day)) n++;
        }
        this->counts[d] = (uint8_t)(n > 255 ? 255 : n);
    }

    LOG_APP_MSG("カレンダー: %d年%d月 %d件 (%dファイル, 捨てた%d件, 繰り返し未対応%d件)",
                this->view_year, this->view_month, this->cal.count, this->loaded_files,
                this->cal.dropped, this->cal.unsupported_rules);
}

// ---------------------------------------------------------------------------
// 表示
// ---------------------------------------------------------------------------

void CalendarScene::refreshView(){
    char buf[PICO_STR_S];
    snprintf(buf, sizeof(buf), "%d年%d月", this->view_year, this->view_month);
    if(this->title_label) this->title_label->setText(buf);

    if(this->grid){
        this->grid->setMonth(this->view_year, this->view_month);
        const bool this_month = (this->today_year == this->view_year && this->today_month == this->view_month);
        this->grid->setToday(this_month ? this->today_day : 0);
        this->grid->setSelected(this->selected_day);
        this->grid->setCounts(this->counts);
    }

    this->refreshDayList();
}

void CalendarScene::refreshDayList(){
    if(!this->day_label || !this->event_list) return;

    this->event_list->clear();

    if(!OSData::SD_usable){
        this->day_label->setText("SDカードがありません");
        return;
    }
    if(this->loaded_files == 0){
        this->day_label->setText("/calendar/ に .ics を置いてください");
        return;
    }

    const int32_t day = Ical::DaysFromCivil(this->view_year, this->view_month, this->selected_day);

    uint8_t idx[kMaxEventsPerDay];
    const int n = Ical::EventsOn(this->cal, day, idx, kMaxEventsPerDay);

    char head[PICO_STR_M];
    if(n == 0){
        snprintf(head, sizeof(head), "%d月%d日(%s) 予定なし",
                 this->view_month, this->selected_day, WDAY_JP[Ical::Weekday(day)]);
    }else{
        snprintf(head, sizeof(head), "%d月%d日(%s) %d件",
                 this->view_month, this->selected_day, WDAY_JP[Ical::Weekday(day)], n);
    }
    this->day_label->setText(head);

    for(int i = 0; i < n; i++){
        const IcalEvent& ev = this->cal.events[idx[i]];
        ScrollListTools::Item item;

        //時刻の欄: 「終日」「09:30-10:30」「22:00-」(翌日へまたぐ)「02:00まで」(前日からの続きが今日終わる)「(続き)」。
        //「~02:00」にしないのは、16pxフォントの「~」が上線のような形で読めないため
        const bool starts_today = Ical::StartsOn(ev, day);
        if(ev.start.isAllDay()){
            item.text.append("終日");
        }else{
            //繰り返しでも各回の長さは同じなので、その回の開始日から数えて今日が何日目かで終わりが分かる
            const int32_t span = ev.end.day - ev.start.day;
            int32_t nth_day = 0;
            if(!starts_today){
                for(int32_t back = 1; back <= span; back++){
                    if(Ical::StartsOn(ev, day - back)){ nth_day = back; break; }
                }
            }
            const bool ends_today = (nth_day == span);

            if(starts_today){
                item.text.appendFormat("%02d:%02d", (int)(ev.start.sec / 3600), (int)(ev.start.sec / 60 % 60));
                if(!ends_today)                     item.text.append("-");
                else if(ev.end.sec != ev.start.sec) item.text.appendFormat("-%02d:%02d", (int)(ev.end.sec / 3600), (int)(ev.end.sec / 60 % 60));
            }else if(ends_today){
                item.text.appendFormat("%02d:%02dまで", (int)(ev.end.sec / 3600), (int)(ev.end.sec / 60 % 60));
            }else{
                item.text.append("(続き)");
            }
        }

        item.text.append(" ");
        item.text.append(ev.summary.empty() ? "(無題)" : ev.summary.c_str());
        if(!ev.location.empty()){
            item.text.append(" @");
            item.text.append(ev.location.c_str());
        }

        this->event_list->add(item);
    }
}

// ---------------------------------------------------------------------------
// 操作
// ---------------------------------------------------------------------------

void CalendarScene::moveMonth(int delta){
    int idx = this->view_year * 12 + (this->view_month - 1) + delta;
    this->view_year = idx / 12;
    this->view_month = idx % 12 + 1;

    //今日のある月へ戻ってきたら今日を、それ以外は1日を選ぶ
    const bool this_month = (this->today_year == this->view_year && this->today_month == this->view_month);
    this->selected_day = this_month ? this->today_day : 1;

    this->reload();
    this->refreshView();
}

void CalendarScene::goToday(){
    if(this->today_year == 0) return; //時計が合っていない
    this->view_year = this->today_year;
    this->view_month = this->today_month;
    this->selected_day = this->today_day;

    this->reload();
    this->refreshView();
}

// ---------------------------------------------------------------------------
// シーン
// ---------------------------------------------------------------------------

void CalendarScene::onEnter(){
    const Rect content = Scene::contentRect();

    this->refreshToday();
    if(this->view_year == 0){
        //初回。時計が合っていなければ仮に1970年1月を出し、合った時点で今日へ飛ぶ(onUpdate)
        if(this->today_year != 0){
            this->view_year = this->today_year;
            this->view_month = this->today_month;
            this->selected_day = this->today_day;
        }else{
            this->view_year = 1970;
            this->view_month = 1;
            this->selected_day = 1;
        }
    }

    // ---- 上部の行: [戻る] … [<] 2026年9月 [>] [今日] ----
    const int y0 = content.y + MARGIN;

    this->back_button = new Button(content.x + MARGIN, y0, "戻る");
    this->back_button->setFontSize(FontFn::Small);
    this->back_button->setH(20);
    this->back_button->setOnPressEnd([](){
        SceneFunctions::Pop();
    });
    WidgetFunctions::Add(this->back_button);

    //Buttonの箱は文字の余白と立体ぶんが足された大きさになるので、実測して並べる
    const int row_h = this->back_button->getLocalRect().h;

    auto make_button = [&](const char* text){
        Button* b = new Button(0, y0, text);
        b->setFontSize(FontFn::Small);
        b->setH(20);
        return b;
    };
    this->today_button = make_button("今日");
    this->next_button  = make_button(">");
    this->prev_button  = make_button("<");

    //右端から詰めて置く
    int x = content.x + content.w - MARGIN;
    x -= this->today_button->getLocalRect().w;
    this->today_button->setX(x);
    x -= MARGIN + this->next_button->getLocalRect().w;
    this->next_button->setX(x);
    x -= MARGIN + kTitleW;
    const int title_x = x;
    x -= MARGIN + this->prev_button->getLocalRect().w;
    this->prev_button->setX(x);

    this->prev_button->setOnPressEnd([this](){ this->moveMonth(-1); });
    this->next_button->setOnPressEnd([this](){ this->moveMonth(+1); });
    this->today_button->setOnPressEnd([this](){ this->goToday(); });
    WidgetFunctions::Add(this->prev_button);
    WidgetFunctions::Add(this->next_button);
    WidgetFunctions::Add(this->today_button);

    this->title_label = new Label<PICO_STR_S>(title_x, y0, "2026年12月");
    this->title_label->setFontSize(FontFn::Small);
    this->title_label->setMaxWidth(kTitleW);
    this->title_label->setTextAlign(TextAlign::Center);
    //Buttonの文字と高さを揃える(LabelのgetH()は行の余白込みで、それで中央寄せすると上へずれた)
    this->title_label->setY(y0 + (row_h - FontFn::GetFontSize(FontFn::Small)) / 2);
    WidgetFunctions::Add(this->title_label);

    // ---- 月の格子 ----
    const int grid_y = y0 + row_h + MARGIN;
    const int grid_h = MonthGrid::kHeaderH + MonthGrid::kRows * kGridRowH;
    this->grid = new MonthGrid(content.x + MARGIN, grid_y, content.w - MARGIN * 2, grid_h);
    this->grid->setOnSelectDay([this](int day){
        this->selected_day = day;
        this->refreshDayList();
    });
    WidgetFunctions::Add(this->grid);

    // ---- 選んだ日の予定 ----
    this->day_label = new Label<PICO_STR_M>(content.x + MARGIN, grid_y + grid_h + MARGIN, "");
    this->day_label->setFontSize(FontFn::Small);
    this->day_label->setMaxWidth(content.w - MARGIN * 2);
    WidgetFunctions::Add(this->day_label);

    const int list_y = grid_y + grid_h + MARGIN + 16 + 2;
    this->event_list = new ScrollList(
        content.x + MARGIN, list_y,
        content.w - MARGIN * 2, content.y + content.h - MARGIN - list_y,
        kMaxEventsPerDay
    );
    this->event_list->setFontSize(FontFn::Small);
    WidgetFunctions::Add(this->event_list);

    //ファイルが別のアプリで書き換わっているかもしれないので、戻ってきたときも読み直す
    this->reload();
    this->refreshView();
}

void CalendarScene::onUpdate(){
    if(!this->refreshToday()) return;

    //時計が合ったばかりで、まだ仮の月(1970年1月)を出しているなら今日へ飛ぶ
    if(this->today_year != 0 && this->view_year < kMinValidYear){
        this->goToday();
        return;
    }

    //日付が変わった(0時を回った)。今日の印だけ動かす
    if(this->grid){
        const bool this_month = (this->today_year == this->view_year && this->today_month == this->view_month);
        this->grid->setToday(this_month ? this->today_day : 0);
    }
}

void CalendarScene::onExit(){
    this->back_button = nullptr;
    this->prev_button = nullptr;
    this->next_button = nullptr;
    this->today_button = nullptr;
    this->title_label = nullptr;
    this->grid = nullptr;
    this->day_label = nullptr;
    this->event_list = nullptr;
}
