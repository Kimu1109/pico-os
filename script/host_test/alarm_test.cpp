// アラーム(AlarmFunctions)の検証。
//
// 時刻はUpdateAt()へ直接渡し、音はPlay()を記録するだけの偽物へ差し替える。
// 確かめること:
//   - 設定ファイルの読み込みと書式違いの読み飛ばし
//   - 時刻になると鳴り、確認ダイアログが1枚だけ出る(毎フレーム増えない)
//   - 同じ分に2回鳴らさない / 有効でなければ鳴らさない / NTP同期前は鳴らさない
//   - 繰り返し(1回・毎日・平日・土日)、1回だけは鳴ったらoffになって保存される
//   - 止める / 5分後(スヌーズ) / 放っておくとkRingMaxMsで止まる
//   - シーン遷移などでダイアログが消えても、鳴っている間は出し直す
#include "functions/Alarm_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/Power_Functions.hpp"
#include "functions/Sound_Functions.hpp"
#include "gui/widgets/dialogs/MsgDialog.hpp"
#include "storage/SD_Path.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstring>
#include <string>

// ---- 偽物 ----
void PICO_GFX::MarkDirty(const Rect&){}
void PICO_GFX::Setup(){}
void PICO_GFX::FlushDirty(){}
void PICO_GFX::DrawDialogBackground(){}
void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}

static int play_count = 0;
bool SoundFunctions::Play(uint8_t, const ChipSynth::Note&){ play_count++; return true; }

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

static struct tm At(int year, int mon, int mday, int hour, int min, int wday){
    struct tm t = {};
    t.tm_year = year - 1900; t.tm_mon = mon - 1; t.tm_mday = mday;
    t.tm_hour = hour; t.tm_min = min; t.tm_wday = wday;
    t.tm_yday = (mon - 1) * 31 + mday - 1; // 日ごとに違えばよい
    return t;
}

static int DialogCount(){ return (int)WidgetFunctions::dialog_roots.size(); }

static void Reset(const char* cfg){
    HostSd::files.clear();
    if(cfg) HostSd::files[PICO_Path::FILE::CFG::SYS_ALARM_CFG] = cfg;
    OSData::SD_usable = true;
    play_count = 0;
    AlarmFunctions::SetupAt(0);
}

int main(){
    using namespace AlarmFunctions;

    // ---- 読み込み ----
    Reset("alarm1 = 07:30,daily,on\nalarm2 = 12:00,weekdays,off\nalarm3 = 25:00,once,on\nalarm9 = 01:00,once,on\n");
    check(Get(0).hour == 7 && Get(0).minute == 30 && Get(0).repeat == Repeat::Daily && Get(0).enabled, "alarm1を読める");
    check(Get(1).hour == 12 && Get(1).repeat == Repeat::Weekdays && !Get(1).enabled, "alarm2を読める");
    check(!Get(2).enabled && Get(2).hour == 7, "壊れた行(25:00)は読み飛ばして既定のまま");
    check(!Get(3).enabled, "範囲外のキー(alarm9)は無視");

    // ---- 鳴る ----
    unsigned long t = 1000;
    UpdateAt(t, At(2026, 9, 30, 7, 29, 3));
    check(!IsRinging() && DialogCount() == 0, "時刻前は鳴らない");
    UpdateAt(t += 16, At(2026, 9, 30, 7, 30, 3));
    check(IsRinging(), "時刻になると鳴る");
    check(DialogCount() == 1, "確認ダイアログが出る");
    check(play_count >= 1, "音を鳴らす");
    for(int i = 0; i < 20; i++) UpdateAt(t += 16, At(2026, 9, 30, 7, 30, 3));
    check(DialogCount() == 1, "毎フレームでダイアログは増えない");

    // ---- 止める ----
    MsgDialog* d = static_cast<MsgDialog*>(WidgetFunctions::dialog_roots.back());
    d->causeOnClosed(true);
    WidgetFunctions::ProcessPendingDeletes();
    check(!IsRinging() && DialogCount() == 0, "止めるで鳴り止み、ダイアログが消える");
    play_count = 0;
    UpdateAt(t += 16, At(2026, 9, 30, 7, 30, 40));
    check(!IsRinging(), "同じ分の中では鳴り直さない");
    check(Get(0).enabled, "毎日のアラームは鳴ってもonのまま");

    // ---- スヌーズ ----
    UpdateAt(t += 16, At(2026, 10, 1, 7, 30, 4));
    check(IsRinging(), "翌日も鳴る(毎日)");
    d = static_cast<MsgDialog*>(WidgetFunctions::dialog_roots.back());
    d->causeOnClosed(false); // 5分後
    WidgetFunctions::ProcessPendingDeletes();
    check(!IsRinging() && DialogCount() == 0, "5分後を選ぶといったん静かになる");
    UpdateAt(t += kSnoozeMs - 1000, At(2026, 10, 1, 7, 35, 4));
    check(!IsRinging(), "5分たつ前は鳴らない");
    UpdateAt(t += 2000, At(2026, 10, 1, 7, 35, 4));
    check(IsRinging() && DialogCount() == 1, "5分後にまた鳴る");

    // ---- 放っておくと止まる ----
    UpdateAt(t += kRingMaxMs + 1, At(2026, 10, 1, 7, 36, 4));
    WidgetFunctions::ProcessPendingDeletes();
    check(!IsRinging() && DialogCount() == 0, "止めないままでもkRingMaxMsで止まる");

    // ---- ダイアログが外から消えた ----
    Reset("alarm1 = 08:00,daily,on\n");
    UpdateAt(t = 5000, At(2026, 9, 30, 8, 0, 3));
    check(DialogCount() == 1, "前提: 鳴ってダイアログが出た");
    WidgetFunctions::ClearSceneWidgets(); // シーン遷移で全部消える想定
    check(DialogCount() == 0, "前提: 遷移でダイアログが消えた");
    UpdateAt(t += 16, At(2026, 9, 30, 8, 0, 3));
    check(IsRinging() && DialogCount() == 1, "鳴っている間は出し直す");
    Dismiss();
    WidgetFunctions::ProcessPendingDeletes();
    check(!IsRinging() && DialogCount() == 0, "Dismiss()で止まる");

    // ---- 鳴らさない条件 ----
    Reset("alarm1 = 09:00,daily,off\nalarm2 = 09:00,daily,on\n");
    UpdateAt(t = 0, At(1970, 1, 1, 9, 0, 4));
    check(!IsRinging(), "NTP同期前(1970年)は鳴らさない");
    Reset("alarm1 = 09:00,daily,off\n");
    UpdateAt(t = 0, At(2026, 9, 30, 9, 0, 3));
    check(!IsRinging(), "offのアラームは鳴らさない");

    // ---- 繰り返し ----
    Reset("alarm1 = 10:00,weekdays,on\nalarm2 = 10:00,weekends,on\n");
    UpdateAt(t = 0, At(2026, 9, 26, 10, 0, 6)); // 土
    check(IsRinging(), "土曜: 土日のアラームが鳴る");
    Dismiss(); WidgetFunctions::ProcessPendingDeletes();
    Reset("alarm1 = 10:00,weekdays,on\n");
    UpdateAt(t = 0, At(2026, 9, 26, 10, 0, 6));
    check(!IsRinging(), "土曜: 平日のアラームは鳴らない");
    UpdateAt(t += 16, At(2026, 9, 28, 10, 0, 1)); // 月
    check(IsRinging(), "月曜: 平日のアラームが鳴る");
    Dismiss(); WidgetFunctions::ProcessPendingDeletes();

    // ---- 1回だけ ----
    Reset("alarm1 = 11:00,once,on\n");
    UpdateAt(t = 0, At(2026, 9, 30, 11, 0, 3));
    check(IsRinging() && !Get(0).enabled, "1回のアラームは鳴った時点でoffになる");
    check(HostSd::files[PICO_Path::FILE::CFG::SYS_ALARM_CFG].find("alarm1=11:00,once,off") != std::string::npos,
          "offになったことがalarm.cfgへ保存される");
    Dismiss(); WidgetFunctions::ProcessPendingDeletes();
    UpdateAt(t += 16, At(2026, 10, 1, 11, 0, 4));
    check(!IsRinging(), "翌日は鳴らない");

    // ---- 設定 ----
    Reset(nullptr);
    Alarm a; a.hour = 6; a.minute = 5; a.repeat = Repeat::Weekends; a.enabled = true;
    check(Set(2, a), "Set()できる");
    check(HostSd::files[PICO_Path::FILE::CFG::SYS_ALARM_CFG].find("alarm3=06:05,weekends,on") != std::string::npos,
          "Set()はalarm.cfgへ書く");
    Alarm b = a; b.hour = 9;
    Set(2, b, false);
    check(HostSd::files[PICO_Path::FILE::CFG::SYS_ALARM_CFG].find("alarm3=06:05") != std::string::npos,
          "persist=falseでは書かない");
    Save(2);
    check(HostSd::files[PICO_Path::FILE::CFG::SYS_ALARM_CFG].find("alarm3=09:05,weekends,on") != std::string::npos,
          "Save()でまとめて書ける");
    check(!Set(4, a) && !Save(-1), "範囲外はfalse");
    // 書いたものを読み直せる
    SetupAt(0);
    check(Get(2).hour == 9 && Get(2).repeat == Repeat::Weekends && Get(2).enabled, "保存した内容を再起動後に読める");

    printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
