#include "gui/scenes/ClocksScene.hpp"
#include "functions/Alarm_Functions.hpp"
#include "functions/Timer_Functions.hpp"
#include "functions/Power_Functions.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Time_Functions.hpp"

#include "Arduino.h"

static const char* const WDAY_JP[7] = { "日", "月", "火", "水", "木", "金", "土" };

// 操作ボタン(開始/リセット)1つぶんの文字領域の幅。
// 「一時停止」(16px x 4文字)が収まる大きさにしてあり、押すたびに文字が変わっても
// 箱の大きさが変わらないよう Button::setW() で固定する(setW/setHは枠を含む全体の
// 大きさなので、この文字領域に Button::kFrameExtra を足して渡している)
static constexpr int ACTION_BUTTON_W = 74;
static constexpr int ACTION_BUTTON_H = 22;
static constexpr int ACTION_BUTTON_GAP = 14;

Rect ClocksScene::bodyRect() const {
    const Rect content = Scene::contentRect();

    const int16_t top = content.y + MARGIN + this->top_row_h + MARGIN;

    return {
        content.x,
        top,
        content.w,
        (int16_t)(content.y + content.h - BOTTOM_RESERVED - top)
    };
}

// ---------------------------------------------------------------------------
// 表示の切り替え
// ---------------------------------------------------------------------------

void ClocksScene::applyVisibility(){
    const bool is_clock = (this->feature == Feature::Clock);
    const bool is_timer = (this->feature == Feature::Timer);
    const bool is_sw    = (this->feature == Feature::Stopwatch);
    const bool is_alarm = (this->feature == Feature::Alarm);

    const bool digital = is_clock && (this->mode == ClockMode::Digital);
    const bool analog  = is_clock && (this->mode == ClockMode::Analog);

    if(this->mode_tab)    this->mode_tab->setVisible(is_clock);
    if(this->title_label) this->title_label->setVisible(!is_clock);

    if(this->date_label)   this->date_label->setVisible(digital);
    if(this->time_label)   this->time_label->setVisible(digital);
    if(this->analog_clock) this->analog_clock->setVisible(analog);

    if(this->timer_picker) this->timer_picker->setVisible(is_timer);
    if(this->timer_start)  this->timer_start->setVisible(is_timer);
    if(this->timer_reset)  this->timer_reset->setVisible(is_timer);
    if(this->timer_status) this->timer_status->setVisible(is_timer);

    if(this->sw_time)   this->sw_time->setVisible(is_sw);
    if(this->sw_status) this->sw_status->setVisible(is_sw);
    if(this->sw_start)  this->sw_start->setVisible(is_sw);
    if(this->sw_reset)  this->sw_reset->setVisible(is_sw);
    if(this->sw_laps)   this->sw_laps->setVisible(is_sw);

    if(this->alarm_list)   this->alarm_list->setVisible(is_alarm);
    if(this->alarm_picker) this->alarm_picker->setVisible(is_alarm);
    if(this->alarm_toggle) this->alarm_toggle->setVisible(is_alarm);
    if(this->alarm_repeat) this->alarm_repeat->setVisible(is_alarm);
}

void ClocksScene::applyFeature(){
    if(this->title_label && this->feature != Feature::Clock){
        this->title_label->setText(
            (this->feature == Feature::Timer)     ? "タイマー" :
            (this->feature == Feature::Stopwatch) ? "ストップウォッチ" : "アラーム"
        );
    }

    this->applyVisibility();

    //隠れている間は表示を更新していないので、表に出した側を今の値で埋め直す
    this->before_sec  = -1;
    this->before_mday = -1;

    this->refresh_time();
    this->refreshTimer();
    this->refreshStopwatch();

    //アラームの編集を離れるときは、書き残しを保存しておく
    if(this->feature != Feature::Alarm) this->flushAlarm();
    this->refreshAlarmList();
    this->refreshAlarmControls();
}

void ClocksScene::applyMode(){
    this->applyVisibility();

    //隠れている間は時刻を流し込んでいないので、表示へ戻した側を今の時刻で埋め直す
    this->before_sec  = -1;
    this->before_mday = -1;
    this->refresh_time();
}

// ---------------------------------------------------------------------------
// 時計
// ---------------------------------------------------------------------------

void ClocksScene::refresh_time(){
    //見えていない側は更新しない。needsRender()は表示状態を見ないので、
    //隠れたウィジェットを更新すると無駄なdirty矩形が毎秒積まれる
    if(this->feature != Feature::Clock) return;

    const struct tm& t = TimeFunctions::timeinfo;

    if(this->mode == ClockMode::Analog){
        if(this->analog_clock) this->analog_clock->setTime(t.tm_hour, t.tm_min, t.tm_sec);
        return;
    }

    if(t.tm_sec != this->before_sec){
        char buf[PICO_STR_M];
        this->before_sec = t.tm_sec;
        strftime(buf, sizeof(buf), "%H:%M:%S", &t);
        this->time_label->setText(buf);
    }

    if(t.tm_mday != this->before_mday){
        char buf[PICO_STR_M];
        this->before_mday = t.tm_mday;
        const int wday = (t.tm_wday >= 0 && t.tm_wday < 7) ? t.tm_wday : 0;
        snprintf(buf, sizeof(buf), "%d月%d日(%s)",
                 t.tm_mon + 1, t.tm_mday, WDAY_JP[wday]);
        this->date_label->setText(buf);
    }
}

// ---------------------------------------------------------------------------
// タイマー
// ---------------------------------------------------------------------------

// 毎ティック呼ばれる。数字以外は触らないこと
void ClocksScene::refreshTimerDigits(){
    if(this->feature != Feature::Timer) return;
    if(!this->timer_picker) return;

    const bool idle = (TimerFunctions::TimerState() == TimerFunctions::State::Idle);

    //設定できるのは動かしていない間だけ。動作中は▲▼を消して残り時間だけを見せる。
    //どちらもsetterの中で「変化が無ければ何もしない」ので、毎ティック呼んでよい
    //(DurationPickerは秒が変わらないミリ秒の刻みでは再描画を要求しない)
    this->timer_picker->setEditable(idle);
    this->timer_picker->setTotalMs(idle ? TimerFunctions::TimerSetMs() : TimerFunctions::TimerLeftMs());
}

// 状態か設定値が変わったときだけ呼ぶ
void ClocksScene::refreshTimerControls(){
    if(this->feature != Feature::Timer) return;

    const char* start_text = "開始";
    const char* status_text = "▲▼で時間を設定";

    switch(TimerFunctions::TimerState()){
        case TimerFunctions::State::Running:
            start_text  = "一時停止";
            status_text = "カウントダウン中";
            break;
        case TimerFunctions::State::Paused:
            start_text  = "再開";
            status_text = "一時停止中";
            break;
        case TimerFunctions::State::Finished:
            start_text  = "開始";
            status_text = "時間になりました";
            break;
        case TimerFunctions::State::Idle:
        default:
            if(TimerFunctions::TimerSetMs() == 0) status_text = "▲▼で時間を設定";
            else                                  status_text = "開始を押すとカウントダウン";
            break;
    }

    if(this->timer_start)  this->timer_start->setText(start_text);
    if(this->timer_status) this->timer_status->setText(status_text);
}

void ClocksScene::updateTimer(){
    //動かしているのはOS(TimerFunctions)。ここは状態の変化を見て表示を合わせるだけ
    const TimerFunctions::State st = TimerFunctions::TimerState();
    if(st != this->shown_timer_state){
        const bool finished_now = (st == TimerFunctions::State::Finished);
        this->shown_timer_state = st;
        if(finished_now){
            this->timer_blink_on = true;
            this->timer_blink_ms = millis();
            //別の機能を見ている間に鳴り終わっても気づけるように、タイマーへ引き戻す
            if(this->feature != Feature::Timer){
                this->feature = Feature::Timer;
                if(this->feature_tab) this->feature_tab->setSelected((int)Feature::Timer);
                this->applyFeature();
            }
            if(this->timer_picker) this->timer_picker->setTextColor(PICO_RED);
        }else if(this->timer_picker){
            this->timer_picker->setTextColor(PICO_FORECOLOR);
        }
        this->refreshTimer();
        return;
    }

    if(st == TimerFunctions::State::Running){
        //残り時間が変わっただけなのでボタンや状態表示は触らない
        this->refreshTimerDigits();
        return;
    }

    if(st == TimerFunctions::State::Finished){
        const unsigned long now = millis();
        if(now - this->timer_blink_ms >= BLINK_INTERVAL_MS){
            this->timer_blink_ms = now;
            this->timer_blink_on = !this->timer_blink_on;
            //消す側を背景色にすると枠ごと消えて見えるため、薄い灰色にとどめる
            if(this->timer_picker)
                this->timer_picker->setTextColor(this->timer_blink_on ? PICO_RED : PICO_LIGHTGREY);
        }
    }
}

void ClocksScene::onTimerStartPressed(){
    TimerFunctions::TimerStartPause();
    this->updateTimer();
}

void ClocksScene::onTimerResetPressed(){
    TimerFunctions::TimerReset();
    this->updateTimer();
}

// ---------------------------------------------------------------------------
// ストップウォッチ
// ---------------------------------------------------------------------------

// 間引いた間隔で呼ばれる。数字以外は触らないこと
void ClocksScene::refreshStopwatchDigits(){
    if(this->feature != Feature::Stopwatch) return;
    if(!this->sw_time) return;
    char buf[PICO_STR_S];
    TimerFunctions::FormatStopwatch(TimerFunctions::StopwatchMs(), buf, sizeof(buf));
    this->sw_time->setText(buf);
}

// 状態が変わったときだけ呼ぶ
void ClocksScene::refreshStopwatchControls(){
    if(this->feature != Feature::Stopwatch) return;

    const char* start_text  = "開始";
    const char* reset_text  = "リセット";
    const char* status_text = "開始を押すと計測";

    switch(TimerFunctions::StopwatchState()){
        case TimerFunctions::State::Running:
            start_text  = "停止";
            reset_text  = "ラップ";
            status_text = "計測中";
            break;
        case TimerFunctions::State::Paused:
            start_text  = "再開";
            status_text = "停止中";
            break;
        default:
            break;
    }

    if(this->sw_start)  this->sw_start->setText(start_text);
    if(this->sw_reset)  this->sw_reset->setText(reset_text);
    if(this->sw_status) this->sw_status->setText(status_text);
}

// ラップの一覧(新しいものを上に)。ラップが変わったときだけ作り直す
void ClocksScene::refreshLaps(){
    if(this->feature != Feature::Stopwatch || !this->sw_laps) return;
    const uint32_t rev = TimerFunctions::LapRevision();
    if(rev == this->shown_lap_rev) return;
    this->shown_lap_rev = rev;
    this->sw_laps->clear();
    const int n = TimerFunctions::LapCount();
    for(int i = n - 1; i >= 0; i--){
        const uint32_t total = TimerFunctions::LapTotalMs(i);
        const uint32_t prev = (i > 0) ? TimerFunctions::LapTotalMs(i - 1) : 0;
        //捨てた古いラップの続きなら、1つ前の通算は分からないので区間は出さない
        const bool has_prev = (i > 0) || TimerFunctions::LapNumber(0) == 1;
        char t[PICO_STR_S], d[PICO_STR_S], line[PICO_STR_M];
        TimerFunctions::FormatStopwatch(total, t, sizeof(t));
        TimerFunctions::FormatStopwatch(total - prev, d, sizeof(d));
        if(has_prev) snprintf(line, sizeof(line), "%2d  %s  +%s", TimerFunctions::LapNumber(i), t, d);
        else         snprintf(line, sizeof(line), "%2d  %s", TimerFunctions::LapNumber(i), t);
        ScrollListTools::Item item;
        item.text.assign(line);
        this->sw_laps->add(item);
    }
}

void ClocksScene::updateStopwatch(){
    const TimerFunctions::State st = TimerFunctions::StopwatchState();
    if(st != this->shown_sw_state){
        this->shown_sw_state = st;
        this->refreshStopwatch();
    }
    this->refreshLaps();
    if(st != TimerFunctions::State::Running) return;

    //1/100秒まで出すが、毎フレームsetText()するとLabelの再レイアウトが毎回走る。
    //読み取れる速さでもないので間引く
    const unsigned long now = millis();
    if(now - this->sw_last_draw_ms < SW_DRAW_INTERVAL_MS) return;
    this->sw_last_draw_ms = now;
    this->refreshStopwatchDigits();
}

void ClocksScene::onStopwatchStartPressed(){
    TimerFunctions::StopwatchStartStop();
    this->updateStopwatch();
}

void ClocksScene::onStopwatchResetPressed(){
    //動いている間は「ラップ」
    if(TimerFunctions::StopwatchState() == TimerFunctions::State::Running) TimerFunctions::StopwatchLap();
    else TimerFunctions::StopwatchReset();
    this->updateStopwatch();
    this->refreshStopwatchDigits();
}

// ---------------------------------------------------------------------------
// アラーム
// ---------------------------------------------------------------------------

void ClocksScene::refreshAlarmList(){
    if(this->feature != Feature::Alarm || !this->alarm_list) return;

    for(int i = 0; i < AlarmFunctions::kMaxAlarms; i++){
        ScrollListTools::Item* item = this->alarm_list->itemAt(i);
        if(!item) continue;
        const AlarmFunctions::Alarm& a = AlarmFunctions::Get(i);
        char buf[PICO_STR_M];
        snprintf(buf, sizeof(buf), "%d  %02d:%02d  %s  %s", i + 1, a.hour, a.minute,
                 AlarmFunctions::RepeatToStr(a.repeat), a.enabled ? "オン" : "オフ");
        item->text.assign(buf);
        //オフの行は薄くして、鳴る予定のものと見分ける
        item->color = a.enabled ? -1 : PICO_DARKGREY;
    }
    this->alarm_list->setSelectedIndex(this->alarm_sel);
}

void ClocksScene::refreshAlarmControls(){
    if(this->feature != Feature::Alarm) return;

    const AlarmFunctions::Alarm& a = AlarmFunctions::Get(this->alarm_sel);
    if(this->alarm_picker){
        this->alarm_picker->setEditable(true);
        this->alarm_picker->setTotalMs(((uint32_t)a.hour * 60u + a.minute) * 60000u);
    }
    if(this->alarm_toggle) this->alarm_toggle->setText(a.enabled ? "オン" : "オフ");
    if(this->alarm_repeat) this->alarm_repeat->setText(AlarmFunctions::RepeatToStr(a.repeat));
}

void ClocksScene::selectAlarm(int index){
    if(index < 0 || index >= AlarmFunctions::kMaxAlarms) return;
    this->flushAlarm();
    TimerFunctions::SetTimerShown(false);
    this->alarm_sel = index;
    this->refreshAlarmControls();
}

void ClocksScene::flushAlarm(){
    if(!this->alarm_dirty) return;
    this->alarm_dirty = false;
    AlarmFunctions::Save(this->alarm_sel);
}

void ClocksScene::updateAlarm(){
    if(this->alarm_dirty && millis() - this->alarm_changed_ms >= ALARM_SAVE_DELAY_MS){
        this->flushAlarm();
    }
}

void ClocksScene::onAlarmTimeChanged(uint32_t total_ms){
    AlarmFunctions::Alarm a = AlarmFunctions::Get(this->alarm_sel);
    const uint32_t minutes = total_ms / 60000u;
    a.hour   = (uint8_t)((minutes / 60u) % 24u);
    a.minute = (uint8_t)(minutes % 60u);
    //時刻を決めたら、そのアラームは有効にする(設定しただけで鳴らないと戸惑うため)
    a.enabled = true;

    AlarmFunctions::Set(this->alarm_sel, a, false);
    this->alarm_dirty = true;
    this->alarm_changed_ms = millis();

    //秒の桁はアラームでは使わないので0へ戻す(setTotalMs()はon_changedを呼ばない)
    if(this->alarm_picker) this->alarm_picker->setTotalMs(minutes * 60000u);
    this->refreshAlarmList();
    this->refreshAlarmControls();
}

void ClocksScene::onAlarmToggled(){
    AlarmFunctions::Alarm a = AlarmFunctions::Get(this->alarm_sel);
    a.enabled = !a.enabled;
    AlarmFunctions::Set(this->alarm_sel, a, false);
    this->alarm_dirty = true;
    this->alarm_changed_ms = millis();
    this->refreshAlarmList();
    this->refreshAlarmControls();
}

void ClocksScene::onAlarmRepeatPressed(){
    AlarmFunctions::Alarm a = AlarmFunctions::Get(this->alarm_sel);
    a.repeat = (AlarmFunctions::Repeat)(((int)a.repeat + 1) % AlarmFunctions::kRepeatCount);
    AlarmFunctions::Set(this->alarm_sel, a, false);
    this->alarm_dirty = true;
    this->alarm_changed_ms = millis();
    this->refreshAlarmList();
    this->refreshAlarmControls();
}

// ---------------------------------------------------------------------------
// 生成
// ---------------------------------------------------------------------------

void ClocksScene::onEnter(){
    const Rect content = Scene::contentRect();

    // タイマー/ストップウォッチはOS(TimerFunctions)が進めるので、別の画面にいた間も止まらない。
    // 表示は作り直したウィジェットへ入れ直す(下のapplyFeature())
    this->sw_last_draw_ms   = millis();
    //ランチャから開いたとき、動いているタイマー/ストップウォッチがあればそのタブから見せる
    if(!this->entered_once){
        this->entered_once = true;
        if(TimerFunctions::TimerState() != TimerFunctions::State::Idle) this->feature = Feature::Timer;
        else if(TimerFunctions::StopwatchState() != TimerFunctions::State::Idle) this->feature = Feature::Stopwatch;
    }
    this->shown_timer_state = TimerFunctions::TimerState();
    this->shown_sw_state    = TimerFunctions::StopwatchState();
    this->shown_lap_rev     = 0xFFFFFFFFu;

    // ---- 上部の行: [戻る][デジタル|アナログ] ----
    this->back_button = new Button(content.x + MARGIN, content.y + MARGIN, "戻る");
    this->back_button->setFontSize(FontFn::Small);
    this->back_button->setH(20 + Button::kFrameExtra);
    this->back_button->setOnPressEnd([](){
        SceneFunctions::Pop();
    });
    WidgetFunctions::Add(this->back_button);

    //Buttonの箱は文字の余白と立体ぶんが足された大きさになるので、実測して高さを揃える
    const Rect back_box = this->back_button->getLocalRect();
    this->top_row_h = back_box.h;

    const int tab_x = back_box.x + back_box.w + MARGIN;
    const int tab_w = content.x + content.w - MARGIN - tab_x;

    this->mode_tab = new TabBar(tab_x, content.y + MARGIN, tab_w, this->top_row_h);
    this->mode_tab->addTab("デジタル");
    this->mode_tab->addTab("アナログ");
    this->mode_tab->setSelected((int)this->mode);
    this->mode_tab->setOnChanged([this](int index){
        this->mode = (ClockMode)index;
        this->applyMode();
    });
    WidgetFunctions::Add(this->mode_tab);

    //mode_tabと同じ場所。時計以外ではタブの代わりに機能名を出す
    this->title_label = new Label<PICO_STR_M>(tab_x, content.y + MARGIN, "タイマー");
    //Normalだと「ストップウォッチ」(24px x 8文字)が幅を超えて2行になり、上部の行からはみ出す
    this->title_label->setFontSize(FontFn::Small);
    this->title_label->setMaxWidth(tab_w);
    this->title_label->setTextAlign(TextAlign::Center);
    this->title_label->setY(content.y + MARGIN + (this->top_row_h - this->title_label->getH()) / 2);
    WidgetFunctions::Add(this->title_label);

    // ---- 下部の行: [時計|タイマー|ストップウォッチ] ----
    this->feature_tab = new TabBar(
        content.x + MARGIN,
        content.y + content.h - MARGIN - FEATURE_TAB_H,
        content.w - MARGIN * 2,
        FEATURE_TAB_H
    );
    this->feature_tab->addTab("時計");
    this->feature_tab->addTab("ﾀｲﾏｰ");
    this->feature_tab->addTab("ｽﾄｯﾌﾟ");
    this->feature_tab->addTab("ｱﾗｰﾑ");
    this->feature_tab->setSelected((int)this->feature);
    this->feature_tab->setOnChanged([this](int index){
        this->feature = (Feature)index;
        this->applyFeature();
    });
    WidgetFunctions::Add(this->feature_tab);

    const Rect body = this->bodyRect();

    // ---- デジタル表示 ----
    const int date_time_height = 24 + MARGIN + 48;

    this->date_label = new Label<PICO_STR_M>(
        body.x, body.y + (body.h - date_time_height) / 2,
        "1月1日(月)"
    );
    this->date_label->setMaxWidth(body.w);
    this->date_label->setFontSize(FontFn::Normal);
    this->date_label->setTextAlign(TextAlign::Center);
    WidgetFunctions::Add(this->date_label);

    this->time_label = new Label<PICO_STR_M>(
        body.x, this->date_label->getScreenY() + this->date_label->getH() + MARGIN,
        "00:00:00"
    );
    this->time_label->setMaxWidth(body.w);
    this->time_label->setFontSize(FontFn::Bigger);
    this->time_label->setTextAlign(TextAlign::Center);
    WidgetFunctions::Add(this->time_label);

    // ---- アナログ表示 ----
    //本体領域の短辺いっぱいの正方形を中央に置く
    const int diameter = ((body.w < body.h) ? body.w : body.h) - MARGIN * 2;

    this->analog_clock = new AnalogClock(
        body.x + (body.w - diameter) / 2,
        body.y + (body.h - diameter) / 2,
        diameter
    );
    WidgetFunctions::Add(this->analog_clock);

    // ---- タイマー / ストップウォッチ ----
    // どちらも「大きな数字 + 状態の1行 + 操作ボタン2つ」で同じ組み方をする。
    // 位置は実測値から下詰めで決める(フォントを変えても崩れないように)
    this->timer_start = new Button(0, 0, "一時停止");
    this->timer_start->setFontSize(FontFn::Small);
    this->timer_start->setW(ACTION_BUTTON_W + Button::kFrameExtra);
    this->timer_start->setH(ACTION_BUTTON_H + Button::kFrameExtra);
    this->timer_start->setOnPressEnd([this](){ this->onTimerStartPressed(); });

    this->action_row_h = this->timer_start->getLocalRect().h;

    const int action_box_w = this->timer_start->getLocalRect().w;
    const int action_total = action_box_w * 2 + ACTION_BUTTON_GAP;
    const int action_left  = body.x + (body.w - action_total) / 2;
    const int action_y     = body.y + body.h - MARGIN - this->action_row_h;

    this->timer_start->setX(action_left);
    this->timer_start->setY(action_y);
    WidgetFunctions::Add(this->timer_start);

    this->timer_reset = new Button(action_left + action_box_w + ACTION_BUTTON_GAP, action_y, "リセット");
    this->timer_reset->setFontSize(FontFn::Small);
    this->timer_reset->setW(ACTION_BUTTON_W + Button::kFrameExtra);
    this->timer_reset->setH(ACTION_BUTTON_H + Button::kFrameExtra);
    this->timer_reset->setOnPressEnd([this](){ this->onTimerResetPressed(); });
    WidgetFunctions::Add(this->timer_reset);

    this->sw_start = new Button(action_left, action_y, "開始");
    this->sw_start->setFontSize(FontFn::Small);
    this->sw_start->setW(ACTION_BUTTON_W + Button::kFrameExtra);
    this->sw_start->setH(ACTION_BUTTON_H + Button::kFrameExtra);
    this->sw_start->setOnPressEnd([this](){ this->onStopwatchStartPressed(); });
    WidgetFunctions::Add(this->sw_start);

    this->sw_reset = new Button(action_left + action_box_w + ACTION_BUTTON_GAP, action_y, "リセット");
    this->sw_reset->setFontSize(FontFn::Small);
    this->sw_reset->setW(ACTION_BUTTON_W + Button::kFrameExtra);
    this->sw_reset->setH(ACTION_BUTTON_H + Button::kFrameExtra);
    this->sw_reset->setOnPressEnd([this](){ this->onStopwatchResetPressed(); });
    WidgetFunctions::Add(this->sw_reset);

    //状態の1行はボタンのすぐ上
    this->timer_status = new Label<PICO_STR_M>(body.x, 0, "▲▼で時間を設定");
    this->timer_status->setFontSize(FontFn::Small);
    this->timer_status->setMaxWidth(body.w);
    this->timer_status->setTextAlign(TextAlign::Center);

    const int status_h = this->timer_status->getH();
    const int status_y = action_y - MARGIN - status_h;

    this->timer_status->setY(status_y);
    WidgetFunctions::Add(this->timer_status);

    this->sw_status = new Label<PICO_STR_M>(body.x, status_y, "開始を押すと計測");
    this->sw_status->setFontSize(FontFn::Small);
    this->sw_status->setMaxWidth(body.w);
    this->sw_status->setTextAlign(TextAlign::Center);
    WidgetFunctions::Add(this->sw_status);

    // ---- アラーム ----
    // [一覧4行] / [時:分の▲▼] / [オン・オフ][繰り返し](下2つはタイマーの操作ボタンと同じ位置)
    const int alarm_list_h = AlarmFunctions::kMaxAlarms * (FontFn::GetFontSize(FontFn::Small) + 4) + 4;

    this->alarm_list = new ScrollList(body.x + MARGIN, body.y, body.w - MARGIN * 2, alarm_list_h, AlarmFunctions::kMaxAlarms);
    this->alarm_list->setFontSize(FontFn::Small);
    for(int i = 0; i < AlarmFunctions::kMaxAlarms; i++){
        ScrollListTools::Item item;
        item.text.assign("-");
        this->alarm_list->add(item);
    }
    this->alarm_list->setOnSelectItem([this](int index, bool){
        this->selectAlarm(index);
    });
    WidgetFunctions::Add(this->alarm_list);

    this->alarm_toggle = new Button(action_left, action_y, "オフ");
    this->alarm_toggle->setFontSize(FontFn::Small);
    this->alarm_toggle->setW(ACTION_BUTTON_W + Button::kFrameExtra);
    this->alarm_toggle->setH(ACTION_BUTTON_H + Button::kFrameExtra);
    this->alarm_toggle->setOnPressEnd([this](){ this->onAlarmToggled(); });
    WidgetFunctions::Add(this->alarm_toggle);

    this->alarm_repeat = new Button(action_left + action_box_w + ACTION_BUTTON_GAP, action_y, "1回");
    this->alarm_repeat->setFontSize(FontFn::Small);
    this->alarm_repeat->setW(ACTION_BUTTON_W + Button::kFrameExtra);
    this->alarm_repeat->setH(ACTION_BUTTON_H + Button::kFrameExtra);
    this->alarm_repeat->setOnPressEnd([this](){ this->onAlarmRepeatPressed(); });
    WidgetFunctions::Add(this->alarm_repeat);

    const int alarm_picker_y = body.y + alarm_list_h + MARGIN;
    this->alarm_picker = new DurationPicker(body.x, alarm_picker_y, body.w, action_y - MARGIN - alarm_picker_y);
    this->alarm_picker->setOnChanged([this](uint32_t ms){ this->onAlarmTimeChanged(ms); });
    WidgetFunctions::Add(this->alarm_picker);

    //残りが数字の置き場所。▲▼はこの矩形の中で数字の上下へ並ぶ
    const int digits_h = status_y - MARGIN - body.y;

    this->timer_picker = new DurationPicker(body.x, body.y, body.w, digits_h);
    this->timer_picker->setOnChanged([this](uint32_t ms){
        //編集できるのは停止中だけなので、ここへ来るのはIdleのときだけ
        TimerFunctions::TimerSet(ms);
        this->refreshTimer();
    });
    if(TimerFunctions::TimerState() == TimerFunctions::State::Finished) this->timer_picker->setTextColor(PICO_RED);
    WidgetFunctions::Add(this->timer_picker);

    //ストップウォッチは数字を上に寄せ、その下にラップの一覧
    this->sw_time = new Label<PICO_STR_M>(body.x, body.y + MARGIN, "00:00.00");
    this->sw_time->setMaxWidth(body.w);
    this->sw_time->setFontSize(FontFn::Bigger);
    this->sw_time->setTextAlign(TextAlign::Center);
    WidgetFunctions::Add(this->sw_time);

    const int laps_y = this->sw_time->getScreenY() + this->sw_time->getH() + MARGIN;
    const int laps_h = status_y - MARGIN - laps_y;
    this->sw_laps = new ScrollList(body.x + MARGIN, (int16_t)laps_y, body.w - MARGIN * 2, (int16_t)(laps_h > 20 ? laps_h : 20));
    this->sw_laps->setFontSize(FontFn::Small);
    WidgetFunctions::Add(this->sw_laps);

    //どれか1つだけを表示する。ウィジェットは全部作っておき、
    //切り替えは可視/不可視だけで済ませる(切り替えのたびにnew/deleteしない)
    this->applyFeature();
}

void ClocksScene::onUpdate(){
    //タイマーを見せている間は、鳴っても確認ダイアログを出さない(この画面で点滅して知らせる)
    TimerFunctions::SetTimerShown(this->feature == Feature::Timer);

    this->refresh_time();

    //見えていなくても時間は進める。タブを切り替えている間に止まってしまっては
    //タイマーにもストップウォッチにもならない
    this->updateTimer();
    this->updateStopwatch();
    this->updateAlarm();
}

void ClocksScene::onExit(){
    //書き残した編集があれば保存する(ウィジェットを手放す前に)
    this->flushAlarm();

    this->alarm_list   = nullptr;
    this->alarm_picker = nullptr;
    this->alarm_toggle = nullptr;
    this->alarm_repeat = nullptr;
    this->back_button = nullptr;
    this->mode_tab    = nullptr;
    this->feature_tab = nullptr;
    this->title_label = nullptr;

    this->date_label = nullptr;
    this->time_label = nullptr;

    this->analog_clock = nullptr;

    this->timer_picker = nullptr;
    this->timer_start  = nullptr;
    this->timer_reset  = nullptr;
    this->timer_status = nullptr;

    this->sw_time   = nullptr;
    this->sw_status = nullptr;
    this->sw_start  = nullptr;
    this->sw_reset  = nullptr;
    this->sw_laps   = nullptr;
}
