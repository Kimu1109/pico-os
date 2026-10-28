// 電源を切っても時刻を保つ(RtcFunctions)の検証。
//
// I2Cの先のDS3231は stubs/Wire.h(= pc/compat/Wire.h)の PicoPcWire::devices に置く偽物で表す。
// AONタイマーはホストでは「無い」(HasAon()=false)。本体の時計はホストでは PicoPcClock のずれで確かめる。
// 確かめること:
//   - 日付の計算(1970/1/1=0日・うるう年)と、DS3231の7バイト(BCD)の読み書き・壊れた値を断る
//   - 時計の飛びの判定(秒の切り捨ての1秒は飛びではない・millis()の一周をまたいでも正しい)
//   - 起動時: DS3231の時刻で本体の時計を合わせる / OSFが立っていたら信じない / つながっていなければ静か
//   - 時計が合わせ直されたら(飛んだら)DS3231へ書き、OSFを消して電池で発振を止める設定(EOSC)も外す
//   - 飛ばなければ書かない
#include "hal/Rtc_Functions.hpp"
#include "system/Log_Functions.hpp"

#include <Arduino.h>
#include <Wire.h>
#include <PicoPcClock.h>
#include <cstdio>
#include <cstring>

void LogFunctions::Log(LogFunctions::LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}

using namespace RtcFunctions;

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

// 偽物のDS3231。レジスタ0x00〜0x12を持ち、[reg]を書くとそこから読み、[reg, v...]を書くと書き込む
struct FakeDs3231 : PicoPcWire::Device {
    uint8_t regs[0x13] = {};
    uint8_t ptr = 0;
    int time_writes = 0;
    bool onWrite(const uint8_t* d, size_t n) override {
        if(n == 0) return true;
        ptr = d[0];
        if(n > 1 && ptr == kRegTime) time_writes++;
        for(size_t i = 1; i < n; i++){
            if(ptr < sizeof(regs)) regs[ptr] = d[i];
            ptr++;
        }
        return true;
    }
    size_t onRead(uint8_t* d, size_t n) override {
        for(size_t i = 0; i < n; i++){
            d[i] = ptr < sizeof(regs) ? regs[ptr] : 0;
            ptr++;
        }
        return n;
    }
};

int main(){
    // ---- 日付の計算 ----
    check(DaysFromCivil(1970, 1, 1) == 0, "日数: 1970/1/1 は0日");
    check(DaysFromCivil(2000, 1, 1) == 10957, "日数: 2000/1/1 は10957日");
    check(DaysFromCivil(2024, 3, 1) - DaysFromCivil(2024, 2, 28) == 2, "日数: 2024年はうるう年");
    check(DaysFromCivil(2100, 3, 1) - DaysFromCivil(2100, 2, 28) == 1, "日数: 2100年はうるう年でない");

    // ---- DS3231の7バイト ----
    {
        const time_t t = 1793150710; // 2026-10-28T01:25:10Z(水曜)
        uint8_t r[7];
        check(EncodeDs3231(t, r), "書く: 2026年");
        const uint8_t want[7] = { 0x10, 0x25, 0x01, 0x04, 0x28, 0x10, 0x26 };
        check(memcmp(r, want, 7) == 0, "書く: BCD・24時間表示・曜日(日曜=1)");
        time_t back = 0;
        check(DecodeDs3231(r, back) && back == t, "読む: 書いた値に戻る");

        //12時間表示(他の機器が書いた): 1AM = 1時、11PM = 23時、12AM = 0時
        uint8_t r12[7] = { 0x10, 0x25, 0x40 | 0x01, 0x04, 0x28, 0x10, 0x26 };
        check(DecodeDs3231(r12, back) && back == t, "読む: 12時間表示の1AM");
        r12[2] = 0x40 | 0x20 | 0x11;
        check(DecodeDs3231(r12, back) && back == t + 22 * 3600, "読む: 12時間表示の11PM");
        uint8_t r0[7] = { 0x00, 0x00, 0x40 | 0x12, 0x01, 0x01, 0x01, 0x26 };
        time_t midnight = 0;
        check(DecodeDs3231(r0, midnight) && midnight % 86400 == 0, "読む: 12時間表示の12AMは0時");

        uint8_t bad[7] = { 0x1A, 0x25, 0x23, 0x04, 0x28, 0x10, 0x26 };
        check(!DecodeDs3231(bad, back), "読む: BCDでない値は断る");
        uint8_t feb29[7] = { 0, 0, 0, 1, 0x29, 0x02, 0x25 };
        check(!DecodeDs3231(feb29, back), "読む: 2025/2/29 は断る");
        feb29[6] = 0x28;
        check(DecodeDs3231(feb29, back), "読む: 2028/2/29 は読む");
        uint8_t century[7] = { 0, 0, 0, 1, 0x01, 0x81, 0x00 };
        check(!DecodeDs3231(century, back), "読む: 世紀ビット(2100年〜)は断る");
        uint8_t h24[7] = { 0, 0, 0x24, 1, 0x01, 0x01, 0x26 };
        check(!DecodeDs3231(h24, back), "読む: 24時は断る");

        check(!EncodeDs3231(946684799, r), "書く: 1999年は断る");
        check(!EncodeDs3231(0, r), "書く: 1970年は断る");
    }

    // ---- 時計の飛び ----
    check(!IsJump(1000, 1000000, 2000, 1000001), "飛び: 1秒経って1秒進んだ");
    check(!IsJump(1000, 1000000, 1999, 1000001), "飛び: 切り捨てで1秒ずれても飛びではない");
    check(!IsJump(1000, 1000000, 61000, 1000062), "飛び: 2秒のずれは飛びではない");
    check(IsJump(1000, 1000000, 2000, 1000010), "飛び: 進んだ");
    check(IsJump(1000, 1000000, 2000, 999990), "飛び: 戻った");
    check(IsJump(1000, 0, 2000, 1793150710), "飛び: 1970年からNTPで合った");
    check(!IsJump(0xFFFFFC18UL, 1000000, 1000, 1000002), "飛び: millis()の一周をまたいでも正しい");

    check(!HasAon(), "AON: ホストには無い");

    // ---- つながっていない ----
    {
        PicoPcWire::devices.clear();
        PicoPcClock::offset_sec.store(0);
        Setup();
        check(!HasDs3231(), "無し: DS3231は見つからない");
        check(RestoredFrom() == Source::None, "無し: 時刻は戻さない");
        check(PicoPcClock::offset_sec.load() == 0, "無し: 本体の時計は変えない");
        UpdateAt(0, 1793150710);
        UpdateAt(1000, 1793160710);
        check(!SaveNow(1793150710), "無し: 控える先が無ければfalse");
    }

    // ---- 起動時にDS3231から戻す ----
    {
        FakeDs3231 ds;
        PicoPcWire::devices.clear();
        PicoPcWire::devices[kDs3231Address] = &ds;
        const time_t t = 1793150710;
        EncodeDs3231(t, ds.regs);
        ds.regs[kRegControl] = 0x1C;
        ds.regs[kRegStatus]  = 0x08;
        PicoPcClock::offset_sec.store(0);
        Setup();
        check(HasDs3231() && Ds3231Valid(), "戻す: DS3231が見つかり中身を信じる");
        check(RestoredFrom() == Source::Ds3231, "戻す: DS3231から戻した");
        const long long diff = PicoPcClock::Now() - (long long)t;
        check(diff >= 0 && diff <= 2, "戻す: 本体の時計がDS3231の時刻になる");

        //飛ばなければ書かない
        UpdateAt(10000, t);
        UpdateAt(11000, t + 1);
        UpdateAt(12000, t + 2);
        check(ds.time_writes == 0, "書かない: 時計が飛ばなければDS3231へ書かない");
        //1秒経たずに呼ばれても見ない
        UpdateAt(12500, t + 100);
        check(ds.time_writes == 0, "書かない: kCheckIntervalMs より短い間は見ない");

        //NTP・手動で合わせ直された(飛んだ)
        const time_t t2 = t + 3600;
        UpdateAt(13000, t2);
        check(ds.time_writes == 1, "書く: 時計が飛んだらDS3231へ書く");
        time_t back = 0;
        check(DecodeDs3231(ds.regs, back) && back == t2, "書く: 書いたのは今の時刻");
        UpdateAt(14000, t2 + 1);
        check(ds.time_writes == 1, "書く: 1回だけ");
    }

    // ---- OSF(電池切れ)が立っていたら信じず、時計が合ったら書いて消す ----
    {
        FakeDs3231 ds;
        PicoPcWire::devices.clear();
        PicoPcWire::devices[kDs3231Address] = &ds;
        EncodeDs3231(1793150710, ds.regs);
        ds.regs[kRegControl] = 0x9C;   //EOSCも立っている
        ds.regs[kRegStatus]  = 0x88;   //OSF
        PicoPcClock::offset_sec.store(0);
        Setup();
        check(HasDs3231() && !Ds3231Valid(), "OSF: 見つかるが中身は信じない");
        check(RestoredFrom() == Source::None, "OSF: 時刻は戻さない");
        check(PicoPcClock::offset_sec.load() == 0, "OSF: 本体の時計は変えない");

        //時計が合っていない間は書かない
        UpdateAt(1000, 5);
        UpdateAt(2000, 6);
        check(ds.time_writes == 0, "OSF: 時計が合うまで書かない");
        //合った(飛ばなくても、中身が無いので書く)
        const time_t now = 1793200000;
        UpdateAt(3000, now);
        check(ds.time_writes == 1, "OSF: 時計が合ったら書く");
        check((ds.regs[kRegStatus] & kStatusOsf) == 0, "OSF: 書いたらOSFを消す");
        check((ds.regs[kRegStatus] & 0x08) == 0x08, "OSF: 他のビットは残す");
        check((ds.regs[kRegControl] & kControlEosc) == 0, "OSF: EOSCを外す");
        check((ds.regs[kRegControl] & 0x1C) == 0x1C, "OSF: 他のビットは残す(制御)");
        check(Ds3231Valid(), "OSF: 書いた後は信じる");
        UpdateAt(4000, now + 1);
        check(ds.time_writes == 1, "OSF: 続けては書かない");
    }

    // ---- 時刻が壊れていたら信じない ----
    {
        FakeDs3231 ds;
        PicoPcWire::devices.clear();
        PicoPcWire::devices[kDs3231Address] = &ds;
        const uint8_t junk[7] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
        memcpy(ds.regs, junk, 7);
        PicoPcClock::offset_sec.store(0);
        Setup();
        check(HasDs3231() && !Ds3231Valid(), "壊れた: 中身は信じない");
        check(RestoredFrom() == Source::None, "壊れた: 時刻は戻さない");
    }

    PicoPcWire::devices.clear();
    printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
