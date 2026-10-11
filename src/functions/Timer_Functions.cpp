#include "functions/Timer_Functions.hpp"
#include "functions/Alarm_Functions.hpp"
#include "functions/Power_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "gui/widgets/WidgetRegistry.hpp"
#include "gui/widgets/dialogs/MsgDialog.hpp"

#include "Arduino.h"
#include <cstdio>

namespace {
    using TimerFunctions::State;

    // タイマー
    State t_state = State::Idle;
    uint32_t t_set = 0;
    uint32_t t_left = 0;
    unsigned long t_last = 0;
    unsigned long t_ring_start = 0;
    long t_ring_step = -1;
    bool t_shown = false;
    WidgetId t_dialog = WidgetIdTools::Invalid();

    // ストップウォッチ
    State s_state = State::Idle;
    uint32_t s_elapsed = 0;
    unsigned long s_last = 0;
    uint32_t laps[TimerFunctions::kMaxLaps];
    int lap_count = 0;
    int lap_first_no = 1;   // laps[0] の通し番号
    uint32_t lap_rev = 0;

    unsigned long last_now = 0;

    void CloseDialog(){
        Widget* w = WidgetRegistry::Resolve(t_dialog);
        t_dialog = WidgetIdTools::Invalid();
        if(!w) return;
        MsgDialog* d = static_cast<MsgDialog*>(w);
        d->clearOnClosed();
        d->setVisible(false);
        WidgetFunctions::DestroyLater(d);
    }

    void ShowDialog(){
        if(WidgetRegistry::Resolve(t_dialog)) return;
        MsgDialog* d = new MsgDialog("タイマー: 時間になりました", "", "止める");
        if(!d) return;
        d->setVisibleIcon(true);
        d->setIconId(IconID::AlertTriangle);
        WidgetFunctions::AddDialog(d);
        d->setVisible(true);
        t_dialog = d->getId();
        d->setOnClosed([d](bool){
            t_dialog = WidgetIdTools::Invalid();
            WidgetFunctions::DestroyLater(d);
            TimerFunctions::TimerReset();
        });
    }
}

void TimerFunctions::Setup(){
    t_state = State::Idle; t_set = t_left = 0; t_ring_step = -1; t_shown = false;
    t_dialog = WidgetIdTools::Invalid();
    s_state = State::Idle; s_elapsed = 0; lap_count = 0; lap_first_no = 1; lap_rev++;
    last_now = millis();
}

void TimerFunctions::Update(){ UpdateAt(millis()); }

void TimerFunctions::UpdateAt(unsigned long now){
    last_now = now;
    if(t_state == State::Running){
        //符号なしの引き算なのでmillis()の一周(約49日)をまたいでも正しい差になる
        const unsigned long d = now - t_last;
        t_last = now;
        if(d >= (unsigned long)t_left){
            t_left = 0;
            t_state = State::Finished;
            t_ring_start = now;
            t_ring_step = -1;
            LOG_APP_MSG("タイマー: 時間になりました");
        }else{
            t_left -= (uint32_t)d;
        }
    }
    if(s_state == State::Running){
        s_elapsed += (uint32_t)(now - s_last);
        s_last = now;
    }

    if(t_state == State::Finished){
        const unsigned long ring = now - t_ring_start;
        if(ring < AlarmFunctions::kRingMaxMs){
            PowerFunctions::KeepAwake();
            const long step = (long)(ring / AlarmFunctions::kBeepStepMs);
            if(step != t_ring_step){
                t_ring_step = step;
                AlarmFunctions::PlayBeepStep(step);
            }
            if(t_shown) CloseDialog();
            else ShowDialog();
        }else{
            CloseDialog();   //誰も止めなかった。音と呼び出しを打ち切る(時計アプリでは点滅が残る)
        }
    }
}

// ---- タイマー ----
State TimerFunctions::TimerState(){ return t_state; }
uint32_t TimerFunctions::TimerSetMs(){ return t_set; }
uint32_t TimerFunctions::TimerLeftMs(){ return t_left; }

void TimerFunctions::TimerSet(uint32_t ms){
    if(t_state == State::Running || t_state == State::Paused) return;
    if(ms > kMaxTimerMs) ms = kMaxTimerMs;
    t_set = ms;
    if(t_state == State::Idle) t_left = ms;
}

void TimerFunctions::TimerStartPause(){
    switch(t_state){
        case State::Idle:
            //0秒のまま開始しても一瞬で鳴るだけなので何もしない
            if(t_set == 0) return;
            t_left = t_set;
            t_state = State::Running;
            t_last = millis();
            break;
        case State::Running:
            t_state = State::Paused;
            break;
        case State::Paused:
            t_state = State::Running;
            t_last = millis();
            break;
        case State::Finished:
            CloseDialog();
            t_left = t_set;
            t_state = (t_set > 0) ? State::Running : State::Idle;
            t_last = millis();
            break;
    }
}

void TimerFunctions::TimerReset(){
    CloseDialog();
    t_state = State::Idle;
    t_left = t_set;
}

bool TimerFunctions::TimerRinging(){
    return t_state == State::Finished && (last_now - t_ring_start) < AlarmFunctions::kRingMaxMs;
}

void TimerFunctions::SetTimerShown(bool shown){ t_shown = shown; }

// ---- ストップウォッチ ----
State TimerFunctions::StopwatchState(){ return s_state; }
uint32_t TimerFunctions::StopwatchMs(){
    //表示は今の瞬間まで(Update()の後に呼ばれても1フレームの遅れが出ないように)
    if(s_state == State::Running) return s_elapsed + (uint32_t)(millis() - s_last);
    return s_elapsed;
}

void TimerFunctions::StopwatchStartStop(){
    if(s_state == State::Running){
        s_elapsed += (uint32_t)(millis() - s_last);
        s_state = State::Paused;
    }else{
        s_state = State::Running;
        s_last = millis();
    }
}

void TimerFunctions::StopwatchReset(){
    s_state = State::Idle;
    s_elapsed = 0;
    lap_count = 0;
    lap_first_no = 1;
    lap_rev++;
}

bool TimerFunctions::StopwatchLap(){
    if(s_state != State::Running) return false;
    const uint32_t now_total = StopwatchMs();
    if(lap_count >= kMaxLaps){
        for(int i = 1; i < kMaxLaps; i++) laps[i - 1] = laps[i];
        lap_count--;
        lap_first_no++;
    }
    laps[lap_count++] = now_total;
    lap_rev++;
    return true;
}

int TimerFunctions::LapCount(){ return lap_count; }
uint32_t TimerFunctions::LapTotalMs(int i){ return (i >= 0 && i < lap_count) ? laps[i] : 0; }
int TimerFunctions::LapNumber(int i){ return lap_first_no + i; }
uint32_t TimerFunctions::LapRevision(){ return lap_rev; }

void TimerFunctions::FormatStopwatch(uint32_t ms, char* out, unsigned cap){
    const uint32_t sec = ms / 1000u;
    if(sec >= 3600u){
        //1時間を超えたら1/100秒を諦めて時を出す(桁数を増やすと画面幅に収まらない)
        snprintf(out, cap, "%u:%02u:%02u",
                 (unsigned)(sec / 3600u), (unsigned)((sec / 60u) % 60u), (unsigned)(sec % 60u));
    }else{
        snprintf(out, cap, "%02u:%02u.%02u",
                 (unsigned)(sec / 60u), (unsigned)(sec % 60u), (unsigned)((ms % 1000u) / 10u));
    }
}
