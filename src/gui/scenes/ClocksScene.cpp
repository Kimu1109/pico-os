#include "gui/scenes/ClocksScene.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Time_Functions.hpp"

#include "Arduino.h"

static const char* const WDAY_JP[7] = { "日", "月", "火", "水", "木", "金", "土" };

// 操作ボタン(開始/リセット)1つぶんの文字領域の幅。
// 「一時停止」(16px x 4文字)が収まる大きさにしてあり、押すたびに文字が変わっても
// 箱の大きさが変わらないよう Button::setW() で固定する
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
}

void ClocksScene::applyFeature(){
    if(this->title_label && this->feature != Feature::Clock){
        this->title_label->setText(
            (this->feature == Feature::Timer) ? "タイマー" : "ストップウォッチ"
        );
    }

    this->applyVisibility();

    //隠れている間は表示を更新していないので、表に出した側を今の値で埋め直す
    this->before_sec  = -1;
    this->before_mday = -1;

    this->refresh_time();
    this->refreshTimer();
    this->refreshStopwatch();
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

    const bool idle = (this->timer_state == RunState::Idle);

    //設定できるのは動かしていない間だけ。動作中は▲▼を消して残り時間だけを見せる。
    //どちらもsetterの中で「変化が無ければ何もしない」ので、毎ティック呼んでよい
    //(DurationPickerは秒が変わらないミリ秒の刻みでは再描画を要求しない)
    this->timer_picker->setEditable(idle);
    this->timer_picker->setTotalMs(idle ? this->timer_set_ms : this->timer_left_ms);
}

// 状態(RunState)か設定値が変わったときだけ呼ぶ
void ClocksScene::refreshTimerControls(){
    if(this->feature != Feature::Timer) return;

    const char* start_text = "開始";
    const char* status_text = "▲▼で時間を設定";

    switch(this->timer_state){
        case RunState::Running:
            start_text  = "一時停止";
            status_text = "カウントダウン中";
            break;
        case RunState::Paused:
            start_text  = "再開";
            status_text = "一時停止中";
            break;
        case RunState::Finished:
            start_text  = "開始";
            status_text = "時間になりました";
            break;
        case RunState::Idle:
        default:
            if(this->timer_set_ms == 0) status_text = "▲▼で時間を設定";
            else                        status_text = "開始を押すとカウントダウン";
            break;
    }

    if(this->timer_start)  this->timer_start->setText(start_text);
    if(this->timer_status) this->timer_status->setText(status_text);
}

void ClocksScene::updateTimer(){
    if(this->timer_state == RunState::Running){
        const unsigned long now = millis();

        //符号なしの引き算なのでmillis()の一周(約49日)をまたいでも正しい差になる。
        //終了時刻を覚えて比較する方式だと、そのまたぎで一気に鳴ってしまう
        const unsigned long delta = now - this->timer_last_tick_ms;
        this->timer_last_tick_ms = now;

        if(delta >= (unsigned long)this->timer_left_ms){
            this->timer_left_ms  = 0;
            this->timer_state    = RunState::Finished;
            this->timer_blink_on = true;
            this->timer_blink_ms = now;

            //別の機能を見ている間に鳴り終わっても気づけるように、タイマーへ引き戻す
            if(this->feature != Feature::Timer){
                this->feature = Feature::Timer;
                if(this->feature_tab) this->feature_tab->setSelected((int)Feature::Timer);
                this->applyFeature();
            }

            if(this->timer_picker) this->timer_picker->setTextColor(PICO_RED);
            this->refreshTimer();
            return;
        }

        this->timer_left_ms -= (uint32_t)delta;

        //残り時間が変わっただけなのでボタンや状態表示は触らない
        this->refreshTimerDigits();
        return;
    }

    if(this->timer_state == RunState::Finished){
        const unsigned long now = millis();
        if(now - this->timer_blink_ms >= BLINK_INTERVAL_MS){
            this->timer_blink_ms = now;
            this->timer_blink_on = !this->timer_blink_on;

            //音が出せないので点滅で知らせる。
            //消す側を背景色にすると枠ごと消えて見えるため、薄い灰色にとどめる
            if(this->timer_picker)
                this->timer_picker->setTextColor(this->timer_blink_on ? PICO_RED : PICO_LIGHTGREY);
        }
    }
}

void ClocksScene::onTimerStartPressed(){
    switch(this->timer_state){
        case RunState::Idle:
            //0秒のまま開始しても一瞬で鳴るだけなので、設定を促したまま何もしない
            if(this->timer_set_ms == 0) return;
            this->timer_left_ms      = this->timer_set_ms;
            this->timer_state        = RunState::Running;
            this->timer_last_tick_ms = millis();
            break;

        case RunState::Running:
            this->timer_state = RunState::Paused;
            break;

        case RunState::Paused:
            this->timer_state        = RunState::Running;
            this->timer_last_tick_ms = millis();
            break;

        case RunState::Finished:
            //同じ設定でもう一度。点滅で変えた文字色を戻しておく
            if(this->timer_picker) this->timer_picker->setTextColor(PICO_FORECOLOR);
            this->timer_left_ms      = this->timer_set_ms;
            this->timer_state        = RunState::Running;
            this->timer_last_tick_ms = millis();
            break;
    }

    this->refreshTimer();
}

void ClocksScene::onTimerResetPressed(){
    if(this->timer_picker) this->timer_picker->setTextColor(PICO_FORECOLOR);

    this->timer_state   = RunState::Idle;
    this->timer_left_ms = this->timer_set_ms;

    this->refreshTimer();
}

// ---------------------------------------------------------------------------
// ストップウォッチ
// ---------------------------------------------------------------------------

// 間引いた間隔で呼ばれる。数字以外は触らないこと
void ClocksScene::refreshStopwatchDigits(){
    if(this->feature != Feature::Stopwatch) return;
    if(!this->sw_time) return;

    const uint32_t ms  = this->sw_elapsed_ms;
    const uint32_t sec = ms / 1000u;

    char buf[PICO_STR_S];
    if(sec >= 3600u){
        //1時間を超えたら1/100秒を諦めて時を出す(桁数を増やすと画面幅に収まらない)
        snprintf(buf, sizeof(buf), "%u:%02u:%02u",
                 (unsigned)(sec / 3600u), (unsigned)((sec / 60u) % 60u), (unsigned)(sec % 60u));
    }else{
        snprintf(buf, sizeof(buf), "%02u:%02u.%02u",
                 (unsigned)(sec / 60u), (unsigned)(sec % 60u), (unsigned)((ms % 1000u) / 10u));
    }
    this->sw_time->setText(buf);
}

// 状態(RunState)が変わったときだけ呼ぶ
void ClocksScene::refreshStopwatchControls(){
    if(this->feature != Feature::Stopwatch) return;

    const char* start_text  = "開始";
    const char* status_text = "開始を押すと計測";

    switch(this->sw_state){
        case RunState::Running:
            start_text  = "停止";
            status_text = "計測中";
            break;
        case RunState::Paused:
            start_text  = "再開";
            status_text = "停止中";
            break;
        default:
            break;
    }

    if(this->sw_start)  this->sw_start->setText(start_text);
    if(this->sw_status) this->sw_status->setText(status_text);
}

void ClocksScene::updateStopwatch(){
    if(this->sw_state != RunState::Running) return;

    const unsigned long now = millis();
    this->sw_elapsed_ms += (uint32_t)(now - this->sw_last_tick_ms);
    this->sw_last_tick_ms = now;

    //1/100秒まで出すが、毎フレームsetText()するとLabelの再レイアウトが毎回走る。
    //読み取れる速さでもないので間引く
    if(now - this->sw_last_draw_ms < SW_DRAW_INTERVAL_MS) return;
    this->sw_last_draw_ms = now;

    //数字が進んだだけなのでボタンや状態表示は触らない
    this->refreshStopwatchDigits();
}

void ClocksScene::onStopwatchStartPressed(){
    if(this->sw_state == RunState::Running){
        this->sw_state = RunState::Paused;
    }else{
        this->sw_state        = RunState::Running;
        this->sw_last_tick_ms = millis();
    }
    this->refreshStopwatch();
}

void ClocksScene::onStopwatchResetPressed(){
    this->sw_state       = RunState::Idle;
    this->sw_elapsed_ms  = 0;
    this->refreshStopwatch();
}

// ---------------------------------------------------------------------------
// 生成
// ---------------------------------------------------------------------------

void ClocksScene::onEnter(){
    const Rect content = Scene::contentRect();

    // Push()で別のシーンへ移っている間はonUpdate()が来ないので、その間の経過は積めない。
    // 復帰した時に止まっていた時間を一気に差し引かないよう、基準を今へ取り直す
    this->timer_last_tick_ms = millis();
    this->sw_last_tick_ms    = millis();
    this->sw_last_draw_ms    = millis();

    // ---- 上部の行: [戻る][デジタル|アナログ] ----
    this->back_button = new Button(content.x + MARGIN, content.y + MARGIN, "戻る");
    this->back_button->setFontSize(FontFn::Small);
    this->back_button->setH(20);
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
    this->feature_tab->addTab("タイマー");
    this->feature_tab->addTab("ストップウォッチ");
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
    this->timer_start->setW(ACTION_BUTTON_W);
    this->timer_start->setH(ACTION_BUTTON_H);
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
    this->timer_reset->setW(ACTION_BUTTON_W);
    this->timer_reset->setH(ACTION_BUTTON_H);
    this->timer_reset->setOnPressEnd([this](){ this->onTimerResetPressed(); });
    WidgetFunctions::Add(this->timer_reset);

    this->sw_start = new Button(action_left, action_y, "開始");
    this->sw_start->setFontSize(FontFn::Small);
    this->sw_start->setW(ACTION_BUTTON_W);
    this->sw_start->setH(ACTION_BUTTON_H);
    this->sw_start->setOnPressEnd([this](){ this->onStopwatchStartPressed(); });
    WidgetFunctions::Add(this->sw_start);

    this->sw_reset = new Button(action_left + action_box_w + ACTION_BUTTON_GAP, action_y, "リセット");
    this->sw_reset->setFontSize(FontFn::Small);
    this->sw_reset->setW(ACTION_BUTTON_W);
    this->sw_reset->setH(ACTION_BUTTON_H);
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

    //残りが数字の置き場所。▲▼はこの矩形の中で数字の上下へ並ぶ
    const int digits_h = status_y - MARGIN - body.y;

    this->timer_picker = new DurationPicker(body.x, body.y, body.w, digits_h);
    this->timer_picker->setOnChanged([this](uint32_t ms){
        //編集できるのは停止中だけなので、ここへ来るのはIdleのときだけ
        this->timer_set_ms  = ms;
        this->timer_left_ms = ms;
        this->refreshTimer();
    });
    WidgetFunctions::Add(this->timer_picker);

    this->sw_time = new Label<PICO_STR_M>(body.x, 0, "00:00.00");
    this->sw_time->setMaxWidth(body.w);
    this->sw_time->setFontSize(FontFn::Bigger);
    this->sw_time->setTextAlign(TextAlign::Center);
    this->sw_time->setY(body.y + (digits_h - this->sw_time->getH()) / 2);
    WidgetFunctions::Add(this->sw_time);

    //どれか1つだけを表示する。ウィジェットは全部作っておき、
    //切り替えは可視/不可視だけで済ませる(切り替えのたびにnew/deleteしない)
    this->applyFeature();
}

void ClocksScene::onUpdate(){
    this->refresh_time();

    //見えていなくても時間は進める。タブを切り替えている間に止まってしまっては
    //タイマーにもストップウォッチにもならない
    this->updateTimer();
    this->updateStopwatch();
}

void ClocksScene::onExit(){
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
}
