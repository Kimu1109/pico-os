// スリープ(PowerFunctions)の検証。
//
// 依存する他の窓口(Display/Network/Sound)は「呼ばれたことを記録するだけ」の偽物へ差し替え、
// タッチはOSData、コントローラーはstubs/Arduino.hのHostSerialへ流し込む。時刻はUpdateAt()へ直接渡す。
// 確かめること:
//   - 操作が無いまま sleep-timeout が過ぎるとスリープへ入り、画面/Wi-Fi/音の省電力が掛かる
//   - タッチで起き、その指を離すまではタッチが画面へ渡らない(離したら次のタッチは通る)
//   - コントローラーでも起きる(こちらは握りつぶさない)
//   - 操作のたびにタイマーが振り出しへ戻る
//   - KeepAwake() / 音が鳴っている / Wi-Fi接続中 は入らない、スリープ中にKeepAwake()が来たら起きる
//   - タイムアウト0は無効
#include "functions/Power_Functions.hpp"
#include "functions/Display_Functions.hpp"
#include "functions/Network_Functions.hpp"
#include "functions/Sound_Functions.hpp"
#include "functions/Pad_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "OS_Data.hpp"

#include <Arduino.h>
#include <cstdio>

// ---- 偽物 ----
namespace Fake {
    bool display_sleeping = false;
    bool wifi_low_power   = false;
    bool sound_power_save = false;
    bool playing          = false;
    bool music            = false;
    bool wav              = false;
    int  display_calls    = 0;
}
void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}
void DisplayFunctions::SetSleeping(bool s){ Fake::display_sleeping = s; Fake::display_calls++; }
bool DisplayFunctions::IsSleeping(){ return Fake::display_sleeping; }
void NetworkFunctions::SetLowPower(bool e){ Fake::wifi_low_power = e; }
void SoundFunctions::SetPowerSave(bool e){ Fake::sound_power_save = e; }
bool SoundFunctions::IsPlaying(){ return Fake::playing; }
bool SoundFunctions::MusicPlaying(){ return Fake::music; }
bool SoundFunctions::WavPlaying(){ return Fake::wav; }

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

static void Reset(){
    Fake::display_sleeping = Fake::wifi_low_power = Fake::sound_power_save = false;
    Fake::playing = Fake::music = Fake::wav = false;
    Fake::display_calls = 0;
    OSData::isTouched = OSData::isTouchStart = OSData::isTouchEnd = OSData::isTouchMove = false;
    NetworkFunctions::currentStatus = NetworkFunctions::NetStatus::SUCCESS;
    PowerFunctions::SetupAt(0);
}

// 1フレーム。Touch_Functionsと同じく、触れている間はisTouched/isTouchStartが立つ
static void Frame(unsigned long now, bool touch){
    OSData::isTouched    = touch;
    OSData::isTouchStart = touch;
    PadFunctions::UpdateAt(now);
    PowerFunctions::UpdateAt(now);
}

int main(){
    using namespace PowerFunctions;

    // ---- 既定値と入り方 ----
    Reset();
    check(GetSleepTimeoutMs() == kDefaultSleepTimeoutMs, "既定は120秒");
    Frame(1000, false);
    check(!IsSleeping(), "始めは起きている");
    Frame(kDefaultSleepTimeoutMs - 1, false);
    check(!IsSleeping(), "タイムアウトの直前は起きている");
    Frame(kDefaultSleepTimeoutMs, false);
    check(IsSleeping(), "タイムアウトでスリープへ入る");
    check(Fake::display_sleeping && Fake::wifi_low_power && Fake::sound_power_save, "画面・Wi-Fi・音の省電力が掛かる");
    const int calls = Fake::display_calls;
    Frame(kDefaultSleepTimeoutMs + 100, false);
    check(Fake::display_calls == calls, "スリープ中に毎フレームは呼び直さない");

    // ---- タッチで起きる。起こした指は握りつぶす ----
    Frame(200000, true);
    check(!IsSleeping(), "タッチで起きる");
    check(!Fake::display_sleeping && !Fake::wifi_low_power && !Fake::sound_power_save, "省電力が全て戻る");
    check(!OSData::isTouched && !OSData::isTouchStart, "起こしたタッチは画面へ渡さない");
    Frame(200016, true);
    check(!OSData::isTouched && !OSData::isTouchStart, "指を当て続けている間も渡さない");
    Frame(200032, false);
    check(!OSData::isTouched, "離した");
    Frame(200048, true);
    check(OSData::isTouched && OSData::isTouchStart, "次のタッチは通る");
    Frame(200064, false);

    // ---- 操作のたびにタイマーが戻る ----
    Reset();
    Frame(100000, true);   // 100秒でタッチ
    Frame(100016, false);
    Frame(219999, false);
    check(!IsSleeping(), "最後の操作から120秒経つまでは入らない");
    Frame(220016, false);
    check(IsSleeping(), "最後の操作から120秒でスリープ");

    // ---- コントローラーでも起きる(握りつぶさない) ----
    HostSerial::Feed("pad 10\n");
    Frame(230000, false);
    check(!IsSleeping(), "コントローラーのボタンで起きる");

    // ---- 入らない条件 ----
    Reset();
    for(unsigned long t = 0; t <= 400000; t += 1000){
        KeepAwake();
        Frame(t, false);
    }
    check(!IsSleeping(), "KeepAwake()を毎フレーム呼ぶ画面では入らない");
    Frame(401000, false);
    check(IsSleeping(), "KeepAwake()が止まれば入る");

    Reset();
    Fake::playing = true;
    Frame(kDefaultSleepTimeoutMs + 1, false);
    check(!IsSleeping(), "音が鳴っている間は入らない");
    Fake::playing = false; Fake::music = true;
    Frame(kDefaultSleepTimeoutMs + 2, false);
    check(!IsSleeping(), "曲が鳴っている間は入らない");
    Fake::music = false; Fake::wav = true;
    Frame(kDefaultSleepTimeoutMs + 2, false);
    check(!IsSleeping(), "WAVが鳴っている間は入らない");
    Fake::wav = false;
    NetworkFunctions::currentStatus = NetworkFunctions::NetStatus::TRYING_CONNECT;
    Frame(kDefaultSleepTimeoutMs + 3, false);
    check(!IsSleeping(), "Wi-Fiへ接続中は入らない");
    NetworkFunctions::currentStatus = NetworkFunctions::NetStatus::SUCCESS;
    Frame(kDefaultSleepTimeoutMs + 4, false);
    check(IsSleeping(), "全て止めば入る");

    // ---- スリープ中にKeepAwake()が来たら起きる(タイマーが鳴った等) ----
    KeepAwake();
    Frame(kDefaultSleepTimeoutMs + 5, false);
    check(!IsSleeping(), "スリープ中にKeepAwake()が来たら起きる");
    check(!Fake::display_sleeping, "画面も戻る");
    KeepAwake();
    Frame(kDefaultSleepTimeoutMs + 6, false);
    check(!IsSleeping(), "KeepAwake()が続く間は再び入らない");

    // ---- 無効/変更 ----
    Reset();
    SetSleepTimeoutMs(0);
    Frame(10000000, false);
    check(!IsSleeping(), "タイムアウト0は無効");
    SetSleepTimeoutMs(60000);
    Frame(10000001, false);
    check(IsSleeping(), "有効へ戻すと(とうに過ぎているので)すぐ入る");
    check(GetSleepTimeoutMs() == 60000, "取得できる");

    printf("\n%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
