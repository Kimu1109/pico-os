#include "gui/scenes/CalendarScene.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Time_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/Network_Functions.hpp"
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

// 複数のカレンダーを重ねたときの色(ファイル名順に割り当てる)。白地で読める濃い色だけ
static const int8_t kCalendarColors[] = {
    PICO_DARKGREEN, PICO_BLUE, PICO_RED, PICO_PURPLE,
    PICO_DARKCYAN, PICO_MAROON, PICO_NAVY, PICO_OLIVE,
};
static constexpr int kCalendarColorCount = sizeof(kCalendarColors) / sizeof(kCalendarColors[0]);

int8_t CalendarScene::colorOf(const IcalEvent& ev) const {
    if(this->file_count <= 1) return -1;
    return kCalendarColors[ev.file_index % kCalendarColorCount];
}

bool CalendarScene::filePath(int file_index, FixedString<PICO_PATH_LEN>& out) const {
    if(file_index < 0 || file_index >= this->file_count) return false;
    return PICO_IO::join(out, PICO_Path::DIR::CALENDAR, this->file_names[file_index].c_str());
}

void CalendarScene::reload(){
    this->cal.clear();
    this->loaded_files = 0;
    this->file_count = 0;
    for(auto& d : this->day_dots) d = MonthGrid::DayDots{};

    if(!OSData::SD_usable) return;

    const int32_t first = Ical::DaysFromCivil(this->view_year, this->view_month, 1);
    const int dim = Ical::DaysInMonth(this->view_year, this->view_month);

    // ---- /calendar/*.ics を名前順に並べる(色とfile_indexを取得のたびに変えないため) ----
    FsFile dir = OSData::SD.open(PICO_Path::DIR::CALENDAR);
    if(!dir) return;

    FsFile file;
    while(file.openNext(&dir, O_RDONLY)){
        char name[128];
        const bool ok = file.getName(name, sizeof(name)) && !file.isDirectory() && EndsWithIcs(name);
        file.close();
        if(!ok) continue;

        if(this->file_count >= kMaxFiles){
            LOG_APP_WARN("カレンダー: .ics が多すぎるため %s を読みません(上限%d)", name, kMaxFiles);
            continue;
        }
        FixedString<PICO_STR_M> n;
        if(!n.assign(name)){
            LOG_APP_WARN("カレンダー: ファイル名が長すぎるため読みません: %s", name);
            continue;
        }
        //挿入ソート(高々8件)
        int i = this->file_count++;
        while(i > 0 && strcmp(this->file_names[i - 1].c_str(), n.c_str()) > 0){
            this->file_names[i] = this->file_names[i - 1];
            i--;
        }
        this->file_names[i] = n;
    }
    dir.close();

    // ---- 読む。格子に見えている範囲(前後の月の空きマスを含む6週間)だけ ----
    Ical::Options opt;
    opt.utc_offset_sec = LocalUtcOffsetSec();
    opt.window_from_day = first - Ical::Weekday(first);
    opt.window_to_day = opt.window_from_day + MonthGrid::kRows * MonthGrid::kCols;

    for(int i = 0; i < this->file_count; i++){
        FixedString<PICO_PATH_LEN> path;
        if(!this->filePath(i, path)) continue;
        opt.file_index = (uint8_t)i;
        if(Ical::ParseFile(path.c_str(), this->cal, opt)) this->loaded_files++;
    }

    // ---- 格子の点 ----
    //点の数は予定の数(3つまで)。色は「違うカレンダーの色」を先に並べ、余った点は同じ色を繰り返す
    //(カレンダーAの予定3件とBの予定1件なら、Aだけの3点ではなくA,B,Aにして両方あると分かるように)
    for(int d = 1; d <= dim; d++){
        const int32_t day = first + (d - 1);
        int8_t distinct[MonthGrid::kMaxDots];
        int nd = 0;
        int total = 0;
        for(int i = 0; i < this->cal.count; i++){
            const IcalEvent& ev = this->cal.events[i];
            if(!Ical::OccursOn(ev, day)) continue;
            total++;
            const int8_t c = (this->file_count > 1) ? this->colorOf(ev) : PICO_DARKGREEN;
            bool seen = false;
            for(int k = 0; k < nd; k++) if(distinct[k] == c) seen = true;
            if(!seen && nd < MonthGrid::kMaxDots) distinct[nd++] = c;
        }
        MonthGrid::DayDots& dd = this->day_dots[d];
        dd.count = (uint8_t)((total < MonthGrid::kMaxDots) ? total : MonthGrid::kMaxDots);
        for(int k = 0; k < dd.count; k++) dd.colors[k] = distinct[k % nd];
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
        this->grid->setDots(this->day_dots);
    }

    this->refreshDayList();
}

void CalendarScene::formatTime(const IcalEvent& ev, int32_t day, FixedString<PICO_STR_M>& out) const {
    //「終日」「09:30-10:30」「22:00-」(翌日へまたぐ)「02:00まで」(前日からの続きが今日終わる)「(続き)」。
    //「~02:00」にしないのは、16pxフォントの「~」が上線のような形で読めないため
    out.clear();
    if(ev.start.isAllDay()){
        out.append("終日");
        return;
    }

    //繰り返しでも各回の長さは同じなので、その回の開始日から数えて今日が何日目かで終わりが分かる
    const bool starts_today = Ical::StartsOn(ev, day);
    const int32_t span = ev.end.day - ev.start.day;
    int32_t nth_day = 0;
    if(!starts_today){
        for(int32_t back = 1; back <= span; back++){
            if(Ical::StartsOn(ev, day - back)){ nth_day = back; break; }
        }
    }
    const bool ends_today = (nth_day == span);

    if(starts_today){
        out.appendFormat("%02d:%02d", (int)(ev.start.sec / 3600), (int)(ev.start.sec / 60 % 60));
        if(!ends_today)                     out.append("-");
        else if(ev.end.sec != ev.start.sec) out.appendFormat("-%02d:%02d", (int)(ev.end.sec / 3600), (int)(ev.end.sec / 60 % 60));
    }else if(ends_today){
        out.appendFormat("%02d:%02dまで", (int)(ev.end.sec / 3600), (int)(ev.end.sec / 60 % 60));
    }else{
        out.append("(続き)");
    }
}

void CalendarScene::refreshDayList(){
    if(!this->day_label || !this->event_list) return;

    this->event_list->clear();
    this->list_count = 0;

    if(!OSData::SD_usable){
        this->day_label->setText("SDカードがありません");
        return;
    }
    if(this->loaded_files == 0){
        this->day_label->setText("/calendar/ に .ics を置いてください");
        return;
    }

    const int32_t day = Ical::DaysFromCivil(this->view_year, this->view_month, this->selected_day);

    static_assert(sizeof(list_events) == kMaxEventsPerDay, "list_events と kMaxEventsPerDay を揃える");
    const int n = Ical::EventsOn(this->cal, day, this->list_events, kMaxEventsPerDay);
    this->list_count = n;

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
        const IcalEvent& ev = this->cal.events[this->list_events[i]];
        ScrollListTools::Item item;

        FixedString<PICO_STR_M> time;
        this->formatTime(ev, day, time);
        item.text.append(time);
        item.text.append(" ");
        item.text.append(ev.summary.empty() ? "(無題)" : ev.summary.c_str());
        if(!ev.location.empty()){
            item.text.append(" @");
            item.text.append(ev.location.c_str());
        }
        //複数のカレンダーを重ねているときは、格子の点と同じ色で書く
        item.color = this->colorOf(ev);

        this->event_list->add(item);
    }
}

void CalendarScene::showDetail(int index){
    if(index < 0 || index >= this->list_count) return;
    if(this->detail_dialog) return; //開いているものがあればそちらを先に閉じてもらう

    const IcalEvent& ev = this->cal.events[this->list_events[index]];
    const int32_t day = Ical::DaysFromCivil(this->view_year, this->view_month, this->selected_day);

    EventDetailDialog::Body body;

    //日時。1日で終わる予定は「9月23日(水) 09:30-10:30」、
    //日をまたぐ予定はその回の始まりと終わりを両方出す(一覧の「(続き)」だけでは分からないので)
    const int32_t span = ev.end.day - ev.start.day - (ev.end.isAllDay() ? 1 : 0);
    auto append_date = [&](int32_t d){
        int y, m, dd;
        Ical::CivilFromDays(d, y, m, dd);
        body.appendFormat("%d月%d日(%s)", m, dd, WDAY_JP[Ical::Weekday(d)]);
    };
    if(span <= 0 || (!ev.start.isAllDay() && span == 1 && ev.end.sec == 0)){
        FixedString<PICO_STR_M> time;
        this->formatTime(ev, day, time);
        append_date(day);
        body.append(" ");
        body.append(time);
    }else{
        //この日にかかっている回が何日に始まったか(繰り返しでも各回の長さは同じ)
        int32_t occ = day;
        for(int32_t back = 0; back <= span; back++){
            if(Ical::StartsOn(ev, day - back)){ occ = day - back; break; }
        }
        append_date(occ);
        if(!ev.start.isAllDay()) body.appendFormat(" %02d:%02d", (int)(ev.start.sec / 3600), (int)(ev.start.sec / 60 % 60));
        body.append(" から\n");
        append_date(occ + span);
        if(!ev.end.isAllDay()) body.appendFormat(" %02d:%02d", (int)(ev.end.sec / 3600), (int)(ev.end.sec / 60 % 60));
        body.append(" まで");
    }
    body.append("\n");

    if(!ev.location.empty()){
        body.append("場所: ");
        body.append(ev.location.c_str());
        body.append("\n");
    }

    if(ev.rule.freq != IcalRule::Freq::None){
        static const char* const kFreq[] = { "", "毎日", "毎週", "毎月", "毎年" };
        body.append("繰り返し: ");
        body.append(kFreq[(int)ev.rule.freq]);
        if(ev.rule.interval > 1) body.appendFormat("(%d回に1回)", (int)ev.rule.interval);
        body.append("\n");
    }else if(!ev.rule.supported){
        body.append("繰り返し: 未対応の規則のため初回だけ表示しています\n");
    }

    //どのカレンダーの予定か(ファイル名から .ics を落としたもの)
    if(ev.file_index < this->file_count){
        const FixedString<PICO_STR_M>& fname = this->file_names[ev.file_index];
        FixedString<PICO_STR_M> cal_name;
        cal_name.assign(fname.c_str(), fname.length() >= 4 ? fname.length() - 4 : fname.length());
        body.append("カレンダー: ");
        body.append(cal_name);
        body.append("\n");
    }

    //説明文は持っていないので、ここで読み直す(1件ぶんだけ。先頭のほうの予定ほど早い)
    FixedString<PICO_PATH_LEN> path;
    Ical::Description desc;
    if(this->filePath(ev.file_index, path) && Ical::ReadDescription(path.c_str(), ev.ordinal, desc)){
        body.append("\n");
        body.append(desc);
    }

    this->detail_dialog = new EventDetailDialog();
    this->detail_dialog->setContent(ev.summary.empty() ? "(無題)" : ev.summary.c_str(), body.c_str());
    WidgetFunctions::AddDialog(this->detail_dialog);
    this->detail_dialog->setVisible(true);
    EventDetailDialog* dialog = this->detail_dialog;
    this->detail_dialog->setOnClosed([this, dialog](bool){
        if(this->detail_dialog == dialog) this->detail_dialog = nullptr;
        WidgetFunctions::DestroyLater(dialog);
    });
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

    // ---- 選んだ日の見出し + [更新] ----
    const int day_row_y = grid_y + grid_h + MARGIN;

    //押すたびに文字が変わる(更新/取得中/再試行)ので、一番長い文字で幅を固定する
    this->sync_button = make_button("取得中");
    this->sync_button->setW(this->sync_button->getLocalRect().w);
    this->sync_button->setX(content.x + content.w - MARGIN - this->sync_button->getLocalRect().w);
    this->sync_button->setY(day_row_y);
    this->sync_button->setOnPressEnd([this](){ this->startSync(); });
    WidgetFunctions::Add(this->sync_button);

    this->source_count = CalendarSync::CountSources();
    //取得元が無ければボタンは出さず、見出しに幅を全部使う
    this->sync_button->setVisible(this->source_count > 0);
    const int day_label_w = (this->source_count > 0)
        ? this->sync_button->getLocalRect().x - MARGIN - (content.x + MARGIN)
        : content.w - MARGIN * 2;

    this->day_label = new Label<PICO_STR_M>(content.x + MARGIN, day_row_y, "");
    this->day_label->setFontSize(FontFn::Small);
    this->day_label->setMaxWidth(day_label_w);
    this->day_label->setY(day_row_y + (row_h - FontFn::GetFontSize(FontFn::Small)) / 2);
    WidgetFunctions::Add(this->day_label);

    const int list_y = day_row_y + row_h + MARGIN;
    this->event_list = new ScrollList(
        content.x + MARGIN, list_y,
        content.w - MARGIN * 2, content.y + content.h - MARGIN - list_y,
        kMaxEventsPerDay
    );
    this->event_list->setFontSize(FontFn::Small);
    //1回目のタップで選択、2回目で詳細を開く(ScrollListの流儀。SearchDialogと同じ)
    this->event_list->setOnSelectItem([this](int index, bool already_selected){
        if(already_selected) this->showDetail(index);
    });
    WidgetFunctions::Add(this->event_list);

    this->frames_since_enter = 0;
    this->refreshSyncButton();

    //ファイルが別のアプリで書き換わっているかもしれないので、戻ってきたときも読み直す
    this->reload();
    this->refreshView();
}

// ---------------------------------------------------------------------------
// 取得
// ---------------------------------------------------------------------------

void CalendarScene::startSync(){
    if(this->sync.state() == CalendarSync::State::Running) return;
    this->auto_synced = true;
    if(!this->sync.begin()){
        this->source_count = 0;
        if(this->sync_button) this->sync_button->setVisible(false);
        return;
    }
    this->refreshSyncButton();
}

void CalendarScene::refreshSyncButton(){
    if(!this->sync_button) return;
    const char* text = "更新";
    if(this->sync.state() == CalendarSync::State::Running) text = "取得中";
    else if(this->last_sync_failed)                        text = "再試行";
    this->sync_button->setText(text);
    this->sync_button->setTextColor(this->last_sync_failed ? PICO_RED : PICO_BLACK);
}

void CalendarScene::onUpdate(){
    // ---- 取得 ----
    if(this->frames_since_enter < 1000) this->frames_since_enter++;

    //ランチャから開いたときは、1回描いてから自動で取りに行く。
    //Wi-Fiが無いのに行くと、名前解決の待ちで画面が止まるだけなので行かない
    if(!this->auto_synced && this->source_count > 0 && this->frames_since_enter >= 2
       && NetworkFunctions::IsConnected()){
        this->startSync();
    }

    if(this->sync.state() == CalendarSync::State::Running){
        this->sync.update();
        if(this->sync.state() != CalendarSync::State::Running){
            this->last_sync_failed = (this->sync.failedCount() > 0);
            this->refreshSyncButton();
            //中身が変わったものがあれば読み直す。304ばかりなら今の表示のままでよい
            if(this->sync.updatedCount() > 0){
                this->reload();
                this->refreshView();
            }
        }
    }

    // ---- 今日 ----
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
    //onUpdate()が来なくなるので取得は打ち切る(手元の .ics はそのまま)。
    //戻ってきたら「更新」で取り直せる
    this->sync.cancel();

    this->back_button = nullptr;
    this->sync_button = nullptr;
    this->detail_dialog = nullptr; //ダイアログ層ごとフレームワークが片付ける
    this->prev_button = nullptr;
    this->next_button = nullptr;
    this->today_button = nullptr;
    this->title_label = nullptr;
    this->grid = nullptr;
    this->day_label = nullptr;
    this->event_list = nullptr;
}
