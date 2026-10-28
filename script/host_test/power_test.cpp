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
//   - sleep-cpu-mhz: スリープ中だけクロックを下げるよう頼み、起きれば戻す。切り替え(Apply)は出力・マイクが
//     止まっている間だけ。下限(clk_peri)を割らない。CardKB2のUARTのときは頼まない
#include "services/Power_Functions.hpp"
#include "services/Display_Functions.hpp"
#include "hal/Network_Functions.hpp"
#include "hal/Sound_Functions.hpp"
#include "hal/Clock_Functions.hpp"
#include "system/Pad_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "system/Time_Functions.hpp"
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
    bool dimmed           = false;
}
void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}
void DisplayFunctions::SetSleeping(bool s){ Fake::display_sleeping = s; Fake::display_calls++; }
bool DisplayFunctions::IsSleeping(){ return Fake::display_sleeping; }
bool DisplayFunctions::IsDimmed(){ return Fake::dimmed; }
void NetworkFunctions::SetLowPower(bool e){ Fake::wifi_low_power = e; }
void SoundFunctions::SetPowerSave(bool e){ Fake::sound_power_save = e; }
bool SoundFunctions::IsPlaying(){ return Fake::playing; }
bool SoundFunctions::MusicPlaying(){ return Fake::music; }
bool SoundFunctions::WavPlaying(){ return Fake::wav; }
bool SoundFunctions::MusicPaused(){ return false; }
bool SoundFunctions::WavPaused(){ return false; }

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
    ClockFunctions::Setup();

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

    // ---- 暗くなっている間はループを約30fpsまで落とす ----
    {
        Reset();
        SetSleepTimeoutMs(0);
        PicoHostClock::now = 20000000;
        auto wait = [](){
            PicoHostClock::last_delay_ms = 0;
            IdleWait();
            return PicoHostClock::last_delay_ms;
        };
        Fake::dimmed = false;
        //平均fpsの窓を「速い」で閉じておく(遅いと判定された窓の間は休まない仕様のため)
        for(int i = 0; i < 100; i++) wait();
        PicoHostClock::now += kFpsWindowMs;
        wait();
        check(wait() == kMinFrameMs, "明るい間は100fpsの間隔");
        Fake::dimmed = true;
        check(wait() == kDimFrameMs, "暗い間は約30fpsの間隔");
        KeepAwake();
        check(wait() == kMinFrameMs, "KeepAwake()を呼ぶ画面では落とさない");
        Frame(PicoHostClock::now, false); //印を下ろす
        Fake::playing = true;
        check(wait() == kMinFrameMs, "音が鳴っている間は落とさない");
        Fake::playing = false;
        check(wait() == kDimFrameMs, "止めばまた落とす");
        Fake::dimmed = false;
        check(wait() == kMinFrameMs, "明るくなれば戻る");
    }

    // ---- 電源メニューのスリープ(SleepNow) ----
    {
        Reset();
        Frame(1000, true);
        PowerFunctions::SleepNow();
        Frame(1010, true);
        check(!IsSleeping(), "SleepNow: 指を離すまでは入らない");
        Frame(1020, false);
        check(IsSleeping(), "SleepNow: 離したら入る(タイムアウトを待たない)");
        KeepAwake();
        Frame(1030, false);
        check(IsSleeping(), "SleepNow: KeepAwake()の画面でも起きない");
        Fake::playing = true;
        Frame(1040, false);
        check(IsSleeping(), "SleepNow: 音が鳴っていても起きない");
        Fake::playing = false;
        Frame(1050, true);
        check(!IsSleeping(), "SleepNow: タッチで起きる");
        Frame(1060, false);
        KeepAwake();
        Frame(1070, false);
        check(!IsSleeping(), "起きた後は普段どおり");

        //Wi-Fiの接続中でも入る
        Reset();
        NetworkFunctions::currentStatus = NetworkFunctions::NetStatus::TRYING_CONNECT;
        PowerFunctions::SleepNow();
        Frame(10, false);
        check(IsSleeping(), "SleepNow: Wi-Fiの接続中でも入る");
        NetworkFunctions::currentStatus = NetworkFunctions::NetStatus::SUCCESS;
    }

    // ---- スリープ中のクロック低減(sleep-cpu-mhz) ----
    {
        using namespace ClockFunctions;
        check(NormalHz() == 150000000u && MinHz() == 48000000u, "クロック: 通常150MHz、下限はclk_peri(48MHz)");
        check(DividerForMhz(0) == 1, "クロック: 0は下げない");
        check(DividerForMhz(50) == 3, "クロック: 50MHz → 1/3");
        check(DividerForMhz(40) == 3 && DividerForMhz(12) == 3, "クロック: 下限を割る指定は50MHzへ丸める");
        check(DividerForMhz(75) == 2, "クロック: 75MHz → 1/2");
        check(DividerForMhz(100) == 1 && DividerForMhz(150) == 1 && DividerForMhz(1000) == 1, "クロック: 通常に届かない分周は下げない");

        Reset();
        check(GetSleepCpuMhz() == 0, "クロック: 既定は下げない");
        Frame(kDefaultSleepTimeoutMs, false);
        check(IsSleeping() && Requested() == 1, "クロック: 既定ではスリープ中も頼まない");

        Reset();
        SetSleepCpuMhz(50);
        Frame(1000, false);
        check(Requested() == 1, "クロック: 起きている間は頼まない");
        Frame(kDefaultSleepTimeoutMs, false);
        check(IsSleeping() && Requested() == 3, "クロック: スリープに入ると1/3を頼む");
        Apply(false);
        check(Divider() == 1, "クロック: 出力・マイクが動いている間は下げない");
        Apply(true);
        check(Divider() == 3 && CurrentHz() == 50000000u, "クロック: 止まっていれば50MHzへ");
        EnsureFull();
        check(Divider() == 1, "クロック: I2Sを始める前に戻す");
        Apply(true);
        check(Divider() == 3, "クロック: 止まればまた下げる");
        Frame(kDefaultSleepTimeoutMs + 1000, true);
        check(!IsSleeping() && Requested() == 1, "クロック: 起きれば戻すよう頼む");
        Apply(true);
        check(Divider() == 1, "クロック: 次の切り替えで通常へ");
        Frame(kDefaultSleepTimeoutMs + 1016, false);

        // スリープ中に設定を変えたら、すぐ頼み直す
        Reset();
        Frame(kDefaultSleepTimeoutMs, false);
        SetSleepCpuMhz(75);
        check(Requested() == 2, "クロック: スリープ中の変更はすぐ効く");
        SetSleepCpuMhz(0);
        check(Requested() == 1, "クロック: 0にすれば戻す");
        Apply(true);
        check(Divider() == 1, "クロック: 戻った");
        SetSleepCpuMhz(0);
    }

    // ---- 時刻の手動設定(TimeFunctions::SetLocalDateTime)。ホストでは母艦の時計を変えずにずれだけ持つ ----
    {
        TimeFunctions::ApplyTimezone("JST-9");
        check(!TimeFunctions::SetLocalDateTime(2026, 2, 30, 0, 0, 0), "時刻: 2/30は断る");
        check(!TimeFunctions::SetLocalDateTime(2026, 13, 1, 0, 0, 0), "時刻: 13月は断る");
        check(!TimeFunctions::SetLocalDateTime(2026, 1, 1, 24, 0, 0), "時刻: 24時は断る");
        check(TimeFunctions::SetLocalDateTime(2026, 3, 1, 9, 0, 0), "時刻: 2026/3/1 9:00(JST)");
        const long long diff = PicoPcClock::Now() - 1772323200LL; //2026-03-01T00:00:00Z
        check(diff >= 0 && diff <= 2, "時刻: 現地時刻からエポック秒へ(UTCの0時)");
        PicoPcClock::offset_sec = 0;
    }

    printf("\n%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
