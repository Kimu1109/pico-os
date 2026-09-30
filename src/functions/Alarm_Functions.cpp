#include "functions/Alarm_Functions.hpp"
#include "functions/Config_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/Power_Functions.hpp"
#include "functions/Sound_Functions.hpp"
#include "functions/Time_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "gui/widgets/WidgetRegistry.hpp"
#include "gui/widgets/dialogs/MsgDialog.hpp"
#include "storage/SD_Path.hpp"
#include "OS_Data.hpp"

#include <Arduino.h>
#include <cstdio>
#include <cstring>

namespace {
    using namespace AlarmFunctions;

    Alarm alarms[kMaxAlarms];

    // 最後に「この分は見た」と記録した通算分(日付込み)。同じ分の中で2回鳴らさないため
    long last_minute_key = -1;

    bool          ringing        = false;
    int           ring_index     = -1;     // 鳴っているアラーム(表示用)
    unsigned long ring_start_ms  = 0;
    long          ring_step      = -1;

    // 最後にUpdateAt()へ渡された時刻。ダイアログのボタンから呼ばれる側が使う(millis()を直に見ない)
    unsigned long last_now_ms    = 0;

    bool          snoozing       = false;
    int           snooze_index   = -1;
    unsigned long snooze_start_ms = 0;

    // 確認ダイアログ。シーンの遷移で破棄されうるのでポインタではなくIDで持ち、毎回引き直す
    WidgetId dialog_id = WidgetIdTools::Invalid();

    const char* const kRepeatNames[kRepeatCount] = { "once", "daily", "weekdays", "weekends" };

    bool ParseRepeat(const char* s, Repeat& out){
        for(int i = 0; i < kRepeatCount; i++){
            if(strcmp(s, kRepeatNames[i]) == 0){ out = (Repeat)i; return true; }
        }
        return false;
    }

    // "07:30,daily,on"
    bool ParseValue(const char* value, Alarm& out){
        int h = 0, m = 0;
        char rep[16] = {0};
        char on[8]   = {0};
        if(sscanf(value, "%d:%d , %15[^, ] , %7s", &h, &m, rep, on) != 4) return false;
        if(h < 0 || h > 23 || m < 0 || m > 59) return false;

        Repeat r = Repeat::Once;
        if(!ParseRepeat(rep, r)) return false;

        bool enabled = false;
        if(strcmp(on, "on") == 0)       enabled = true;
        else if(strcmp(on, "off") == 0) enabled = false;
        else return false;

        out.hour = (uint8_t)h;
        out.minute = (uint8_t)m;
        out.repeat = r;
        out.enabled = enabled;
        return true;
    }

    void FormatValue(const Alarm& a, char* out, size_t cap){
        snprintf(out, cap, "%02d:%02d,%s,%s", a.hour, a.minute,
                 kRepeatNames[(int)a.repeat], a.enabled ? "on" : "off");
    }

    bool SaveOne(int index){
        if(!OSData::SD_usable) return false;
        char key[24];
        char value[32];
        snprintf(key, sizeof(key), "alarm%d", index + 1);
        FormatValue(alarms[index], value, sizeof(value));
        return PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_ALARM_CFG, key, value);
    }

    void LoadConfig(){
        if(!OSData::SD_usable) return;
        //無いのが普通(全てoffで動く)。ParseFile()は開けないとFAILを出すので先に確かめる
        if(!OSData::SD.exists(PICO_Path::FILE::CFG::SYS_ALARM_CFG)) return;

        PICO_Config::ParseFile(PICO_Path::FILE::CFG::SYS_ALARM_CFG,
            [](const char* key, const char* value){
                int n = 0;
                if(sscanf(key, "alarm%d", &n) != 1 || n < 1 || n > kMaxAlarms) return;
                Alarm a;
                if(ParseValue(value, a)){
                    alarms[n - 1] = a;
                }else{
                    LOG_SYS_WARN("alarm.cfg: %s の書式は HH:MM,once|daily|weekdays|weekends,on|off です: %s", key, value);
                }
            }
        );
    }

    // ---- 確認ダイアログ ----

    void CloseDialog(){
        Widget* w = WidgetRegistry::Resolve(dialog_id);
        dialog_id = WidgetIdTools::Invalid();
        if(!w) return;
        MsgDialog* d = static_cast<MsgDialog*>(w);
        d->clearOnClosed();
        d->setVisible(false);
        WidgetFunctions::DestroyLater(d);
    }

    void StopAll(){
        ringing  = false;
        snoozing = false;
        ring_index = snooze_index = -1;
        CloseDialog();
    }

    void ShowDialog(){
        if(WidgetRegistry::Resolve(dialog_id)) return; // 出ている

        char text[PICO_STR_M];
        const Alarm& a = alarms[(ring_index >= 0) ? ring_index : 0];
        snprintf(text, sizeof(text), "アラーム %02d:%02d", a.hour, a.minute);

        MsgDialog* d = new MsgDialog(text, "5分後", "止める");
        if(!d) return;
        d->setVisibleIcon(true);
        d->setIconId(IconID::AlertTriangle);
        WidgetFunctions::AddDialog(d);
        d->setVisible(true);
        dialog_id = d->getId();

        d->setOnClosed([d](bool is_ok){
            //ダイアログ自身が閉じる。IDは先に無効化してCloseDialog()の二重処理を避ける
            dialog_id = WidgetIdTools::Invalid();
            WidgetFunctions::DestroyLater(d);

            if(is_ok){
                StopAll();
            }else{
                //5分後にもう一度。今の音は止める
                ringing = false;
                snoozing = true;
                snooze_index = ring_index;
                snooze_start_ms = last_now_ms;
            }
        });
    }

    void StartRinging(int index, unsigned long now_ms){
        ringing = true;
        snoozing = false;
        ring_index = index;
        ring_start_ms = now_ms;
        ring_step = -1;
        LOG_APP_MSG("Alarm: alarm%d が鳴りました", index + 1);
    }

    void RingStep(unsigned long now_ms){
        PowerFunctions::KeepAwake();
        ShowDialog();

        const long step = (long)((now_ms - ring_start_ms) / kBeepStepMs);
        if(step == ring_step) return;
        ring_step = step;

        AlarmFunctions::PlayBeepStep(step);
    }
}

void AlarmFunctions::PlayBeepStep(long step){
    //4拍のうち最後の1拍は休む
    if(step % 4 == 3) return;
    ChipSynth::Note n;
    n.wave = ChipSynth::Wave::Pulse50;
    n.freq_x16 = 1047u * 16u; // C6
    n.volume = 15;
    n.length_ms = 120;
    SoundFunctions::Play(0, n);
}

// ---------------------------------------------------------------------------

const char* AlarmFunctions::RepeatToStr(Repeat r){
    switch(r){
        case Repeat::Once:     return "1回";
        case Repeat::Daily:    return "毎日";
        case Repeat::Weekdays: return "平日";
        case Repeat::Weekends: return "土日";
    }
    return "1回";
}

bool AlarmFunctions::RepeatMatches(Repeat r, int wday){
    const bool weekend = (wday == 0 || wday == 6);
    switch(r){
        case Repeat::Once:
        case Repeat::Daily:    return true;
        case Repeat::Weekdays: return !weekend;
        case Repeat::Weekends: return weekend;
    }
    return true;
}

void AlarmFunctions::Setup(){ SetupAt(millis()); }
void AlarmFunctions::Update(){ UpdateAt(millis(), TimeFunctions::timeinfo); }

void AlarmFunctions::SetupAt(unsigned long){
    for(int i = 0; i < kMaxAlarms; i++) alarms[i] = Alarm();
    last_minute_key = -1;
    ringing = snoozing = false;
    ring_index = snooze_index = -1;
    dialog_id = WidgetIdTools::Invalid();
    LoadConfig();
    LOG_SYS_OK("Alarm Setup has succeeded!");
}

const AlarmFunctions::Alarm& AlarmFunctions::Get(int index){
    if(index < 0 || index >= kMaxAlarms) index = 0;
    return alarms[index];
}

bool AlarmFunctions::Set(int index, const Alarm& alarm, bool persist){
    if(index < 0 || index >= kMaxAlarms) return false;
    alarms[index] = alarm;
    //時刻を変えたら、その分が既に過ぎていても今日は鳴らし直せるようにする
    last_minute_key = -1;
    if(persist) SaveOne(index);
    return true;
}

bool AlarmFunctions::Save(int index){
    if(index < 0 || index >= kMaxAlarms) return false;
    return SaveOne(index);
}

bool AlarmFunctions::IsRinging(){ return ringing; }

void AlarmFunctions::Dismiss(){ StopAll(); }

void AlarmFunctions::UpdateAt(unsigned long now_ms, const struct tm& now){
    last_now_ms = now_ms;
    if(ringing){
        if(now_ms - ring_start_ms >= kRingMaxMs){
            //誰も止めなかった。音と画面の呼び出しを打ち切る
            LOG_APP_MSG("Alarm: 止められないまま%lu秒経ったので止めます", kRingMaxMs / 1000UL);
            StopAll();
        }else{
            RingStep(now_ms);
        }
    }else if(snoozing && now_ms - snooze_start_ms >= kSnoozeMs){
        StartRinging(snooze_index, now_ms);
        RingStep(now_ms);
    }

    if(now.tm_year + 1900 < kMinValidYear) return;

    const long key = ((long)now.tm_year * 366L + now.tm_yday) * 1440L + now.tm_hour * 60L + now.tm_min;
    if(key == last_minute_key) return;
    last_minute_key = key;

    for(int i = 0; i < kMaxAlarms; i++){
        Alarm& a = alarms[i];
        if(!a.enabled) continue;
        if(a.hour != now.tm_hour || a.minute != now.tm_min) continue;
        if(!RepeatMatches(a.repeat, now.tm_wday)) continue;

        if(a.repeat == Repeat::Once){
            a.enabled = false;
            SaveOne(i);
        }
        StartRinging(i, now_ms);
        RingStep(now_ms);
        break; // 同じ分に複数あっても、鳴らすのは1つ(ダイアログが1つのため)
    }
}
