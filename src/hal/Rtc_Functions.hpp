#pragma once
#include <cstdint>
#include <ctime>

// 電源を切っても・再起動しても時刻を保つ(SUMMARY.md「電源を切っても時刻を保つ」)。
//
// 本体の時計(time()。TimeFunctions)は起動のたびに1970年から始まり、NTPか手動設定で合わせるまで
// アラーム・通知・TLSが止まる。そこで時刻を2か所へ控えておき、起動時にそこから戻す。
//
// ---- 控えの置き場所(起動時はこの順に見る) ----
// 1. 外付けRTC(DS3231。I2C0、アドレス 0x68。hal/I2C_Bus.hpp でCardKB2・Wiiクラシックと共有)
//    ボタン電池(CR2032等)で動き続けるので、**電源を切っても**時刻が残る。無いのが普通で、無ければ何も出さない。
//    DS3231の基板(ZS-042等)をVCC=3.3V・GND・SDA=GP0・SCL=GP1へ。基板のプルアップでバスのプルアップも足りる。
//    時刻はUTCで持つ(タイムゾーンを変えてもRTCを書き直さなくてよい)。
//    状態レジスタのOSF(発振が止まった=電池切れ・初めての通電)が立っていたら中身は信じない。
// 2. RP2350の常時動作タイマー(AONタイマー、powman)
//    本体に電気が来ている間は動き続け、**再起動(電源メニュー・クラッシュ・ファームの更新)では消えない**。
//    電源を切る(USBも電池も外す)と消える。部品を足さなくても使えるので、常に控える。
//
// ---- 控えを書くとき ----
// 本体の時計が「飛んだ」とき(NTPの同期・手動設定)に、Update() が見つけて両方へ書く。
// 時計を合わせる処理(TimeFunctions::SetClock()・NTP)の側は何もしなくてよい
// (NTPの同期は arduino-pico の中で起きるので、呼び出しを足す場所が無いため)。
// 飛びは「前の回の時刻 + millis() の経過」と今の time() の差が kJumpThresholdSec を超えたかで見る。
// 控えの側が「中身が無い」(OSF・AONが止まっている)ときは、時計が合っていれば飛ばなくても書く。
//
// 実機でしか動かない部分(DS3231の実物・AONタイマー)は docs/design/power-display.md「電源を切っても時刻を保つ」。
namespace RtcFunctions {

    constexpr uint8_t       kDs3231Address    = 0x68;
    constexpr uint8_t       kRegTime          = 0x00;   // 秒・分・時・曜日・日・月・年(BCD、7バイト)
    constexpr uint8_t       kRegControl       = 0x0E;   // bit7 EOSC(1=電池で発振を止める)
    constexpr uint8_t       kRegStatus        = 0x0F;   // bit7 OSF(1=発振が止まったことがある)
    constexpr uint8_t       kControlEosc      = 0x80;
    constexpr uint8_t       kStatusOsf        = 0x80;
    constexpr unsigned long kCheckIntervalMs  = 1000;
    constexpr long          kJumpThresholdSec = 2;

    // 起動時に本体の時計をどこから戻したか
    enum class Source : uint8_t { None, Ds3231, Aon };

    // TimeFunctions::Setup() の後。DS3231・AONタイマーを見て、時刻が残っていれば本体の時計を合わせる
    void Setup();
    // loop() で毎フレーム(TimeFunctions::Update() の近く)。kCheckIntervalMs ごとに時計の飛びを見る
    void Update();
    // テスト用: 時刻を外から与える版
    void UpdateAt(unsigned long now_ms, time_t now);

    bool   HasDs3231();       // DS3231が応答したか(起動時に1回だけ見る)
    bool   Ds3231Valid();     // DS3231の中身を信じてよいか(OSFが立っていない・書き直した)
    bool   HasAon();          // AONタイマーを使える(実機のRP2350)
    Source RestoredFrom();

    // 今の本体の時計(epoch秒)を控えへ書く。時計が合っていなければ何もせずfalse
    bool SaveNow(time_t now);

    // ---- 中身(ホストテスト用に公開) ----
    // 1970-01-01からの日数(グレゴリオ暦。timegm の代わり。タイムゾーンを読まない)
    int64_t DaysFromCivil(int y, int m, int d);
    // DS3231の7バイト(BCD)→ UTCのepoch秒。2000〜2099年・12時間表示も読む。壊れた値ならfalse
    bool DecodeDs3231(const uint8_t regs[7], time_t& out);
    // UTCのepoch秒 → DS3231の7バイト(24時間表示)。2000〜2099年の外ならfalse
    bool EncodeDs3231(time_t t, uint8_t regs[7]);
    // 前の回(last_ms, last)から今(now_ms, now)までに、本体の時計が経過とずれて飛んだか
    bool IsJump(unsigned long last_ms, time_t last, unsigned long now_ms, time_t now);
}
