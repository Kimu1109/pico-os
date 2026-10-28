#include "hal/Rtc_Functions.hpp"
#include "hal/I2C_Bus.hpp"
#include "system/Time_Functions.hpp"
#include "system/Log_Functions.hpp"

#include <Arduino.h>
#include <Wire.h>

// AONタイマー(RP2350のpowman)は実機だけ。PC/Web/ホストテストでは「無い」と答える
#if !defined(PICOOS_PC) && defined(PICO_RP2350) && __has_include("pico/aon_timer.h")
    #include "pico/aon_timer.h"
    #define PICOOS_RTC_AON 1
#else
    #define PICOOS_RTC_AON 0
#endif

namespace {
    bool ds_present = false;
    bool ds_valid   = false;      // 中身を信じてよい(OSFが立っていない・こちらで書いた)
    bool ds_failed  = false;      // 読み書きに失敗した(ログを1回だけ出す)
    RtcFunctions::Source restored = RtcFunctions::Source::None;

    bool          has_last = false;
    unsigned long last_ms  = 0;
    time_t        last_t   = 0;
    unsigned long last_check_ms = 0;

    uint8_t ToBcd(int v){ return (uint8_t)(((v / 10) << 4) | (v % 10)); }
    // BCDの1バイト。下の桁が9を超えるなら壊れた値として-1
    int FromBcd(uint8_t b){
        if((b & 0x0F) > 9 || (b >> 4) > 9) return -1;
        return (b >> 4) * 10 + (b & 0x0F);
    }

    bool ClockValid(time_t t){
        struct tm tm;
        gmtime_r(&t, &tm);
        return tm.tm_year + 1900 >= TimeFunctions::kValidYear;
    }

    // ---- DS3231 ----
    bool DsWrite(const uint8_t* data, size_t len){
        Wire.beginTransmission(RtcFunctions::kDs3231Address);
        for(size_t i = 0; i < len; i++) Wire.write(data[i]);
        return Wire.endTransmission() == 0;
    }
    bool DsRead(uint8_t reg, uint8_t* out, uint8_t len){
        if(!DsWrite(&reg, 1)) return false;
        if(Wire.requestFrom(RtcFunctions::kDs3231Address, len) < len) return false;
        for(uint8_t i = 0; i < len; i++){
            const int v = Wire.read();
            if(v < 0) return false;
            out[i] = (uint8_t)v;
        }
        return true;
    }
    void DsFail(const char* what){
        if(!ds_failed) LOG_SYS_WARN("RTC(DS3231): %sに失敗しました", what);
        ds_failed = true;
    }
    bool DsSave(time_t t){
        uint8_t buf[8];
        buf[0] = RtcFunctions::kRegTime;
        if(!RtcFunctions::EncodeDs3231(t, buf + 1)) return false;
        if(!DsWrite(buf, sizeof(buf))){ DsFail("時刻の書き込み"); return false; }
        //電池で発振を止める設定(EOSC)を外し、OSF(電池切れの印)を消す
        uint8_t ctrl[2];
        if(DsRead(RtcFunctions::kRegControl, ctrl, 2)){
            const uint8_t w1[2] = { RtcFunctions::kRegControl, (uint8_t)(ctrl[0] & ~RtcFunctions::kControlEosc) };
            const uint8_t w2[2] = { RtcFunctions::kRegStatus,  (uint8_t)(ctrl[1] & ~RtcFunctions::kStatusOsf) };
            if(!DsWrite(w1, 2) || !DsWrite(w2, 2)){ DsFail("状態の書き込み"); return false; }
        }else{
            DsFail("状態の読み出し");
            return false;
        }
        ds_valid = true;
        return true;
    }

    // ---- AONタイマー ----
    bool AonRunning(){
#if PICOOS_RTC_AON
        return aon_timer_is_running();
#else
        return false;
#endif
    }
    bool AonGet(time_t& out){
#if PICOOS_RTC_AON
        if(!aon_timer_is_running()) return false;
        struct timespec ts = {};
        aon_timer_get_time(&ts);
        out = ts.tv_sec;
        return true;
#else
        (void)out;
        return false;
#endif
    }
    bool AonSave(time_t t){
#if PICOOS_RTC_AON
        struct timespec ts = {};
        ts.tv_sec = t;
        if(aon_timer_is_running()){
            aon_timer_set_time(&ts);
            return true;
        }
        return aon_timer_start(&ts);
#else
        (void)t;
        return false;
#endif
    }
}

int64_t RtcFunctions::DaysFromCivil(int y, int m, int d){
    //Howard Hinnant の days_from_civil
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

bool RtcFunctions::DecodeDs3231(const uint8_t r[7], time_t& out){
    const int sec = FromBcd(r[0] & 0x7F);
    const int min = FromBcd(r[1] & 0x7F);
    int hour;
    if(r[2] & 0x40){
        //12時間表示(bit5=PM)。他の機器が書いたときだけ。12AM=0時・12PM=12時
        const int h12 = FromBcd(r[2] & 0x1F);
        if(h12 < 1 || h12 > 12) return false;
        hour = (h12 % 12) + ((r[2] & 0x20) ? 12 : 0);
    }else{
        hour = FromBcd(r[2] & 0x3F);
    }
    const int day   = FromBcd(r[4] & 0x3F);
    const int month = FromBcd(r[5] & 0x1F);
    const int yy    = FromBcd(r[6]);
    if(sec < 0 || sec > 59 || min < 0 || min > 59 || hour < 0 || hour > 23
        || month < 1 || month > 12 || day < 1 || day > 31 || yy < 0) return false;
    //月の世紀ビット(bit7)は2100年以降。ここでは扱わない
    if(r[5] & 0x80) return false;
    const int year = 2000 + yy;
    //その月に無い日(2/30等)は断る
    static const int kDays[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    const int mdays = kDays[month - 1] + ((month == 2 && leap) ? 1 : 0);
    if(day > mdays) return false;
    out = (time_t)(DaysFromCivil(year, month, day) * 86400 + hour * 3600 + min * 60 + sec);
    return true;
}

bool RtcFunctions::EncodeDs3231(time_t t, uint8_t r[7]){
    struct tm tm;
    if(!gmtime_r(&t, &tm)) return false;
    const int year = tm.tm_year + 1900;
    if(year < 2000 || year > 2099) return false;
    r[0] = ToBcd(tm.tm_sec > 59 ? 59 : tm.tm_sec);
    r[1] = ToBcd(tm.tm_min);
    r[2] = ToBcd(tm.tm_hour);              //bit6=0: 24時間表示
    r[3] = (uint8_t)(tm.tm_wday + 1);      //曜日は1〜7(日曜=1。DS3231は値の意味を問わない)
    r[4] = ToBcd(tm.tm_mday);
    r[5] = ToBcd(tm.tm_mon + 1);
    r[6] = ToBcd(year - 2000);
    return true;
}

bool RtcFunctions::IsJump(unsigned long lms, time_t lt, unsigned long nms, time_t nt){
    //実機の millis() は32ビットで49日で一周するが、差を32ビットで取れば一周をまたいでも正しい
    //(ホストの unsigned long は64ビットなので、明示的に32ビットへ落とす)
    const uint32_t elapsed_ms = (uint32_t)(nms - lms);
    const int64_t expected = (int64_t)lt + (int64_t)(elapsed_ms / 1000);
    const int64_t diff = (int64_t)nt - expected;
    //time() は秒単位なので、飛んでいなくても経過の切り捨てで1秒ずれることがある
    return diff > kJumpThresholdSec || diff < -kJumpThresholdSec;
}

bool RtcFunctions::SaveNow(time_t now){
    if(!ClockValid(now)) return false;
    bool ok = false;
    if(ds_present && DsSave(now)) ok = true;
    if(AonSave(now)) ok = true;
    return ok;
}

void RtcFunctions::Setup(){
    restored = Source::None;
    has_last = false;
    ds_present = ds_valid = ds_failed = false;

    I2cBus::Begin();
    uint8_t status = 0;
    if(DsRead(kRegStatus, &status, 1)){
        ds_present = true;
        uint8_t regs[7];
        time_t t = 0;
        if(status & kStatusOsf){
            LOG_SYS_WARN("RTC(DS3231): 発振が止まっていました(電池切れ・初めての通電)。時刻が合うまで使いません");
        }else if(!DsRead(kRegTime, regs, sizeof(regs))){
            DsFail("時刻の読み出し");
        }else if(!DecodeDs3231(regs, t) || !ClockValid(t)){
            LOG_SYS_WARN("RTC(DS3231): 時刻が壊れています。時刻が合うまで使いません");
        }else{
            ds_valid = true;
            if(TimeFunctions::SetClock(t)){
                restored = Source::Ds3231;
                LOG_SYS_OK("RTC(DS3231)から時刻を戻しました");
            }
        }
    }

    //DS3231が無い・使えないときは、再起動の前から動いているAONタイマー
    if(restored == Source::None){
        time_t t = 0;
        if(AonGet(t) && ClockValid(t) && TimeFunctions::SetClock(t)){
            restored = Source::Aon;
            LOG_SYS_OK("AONタイマーから時刻を戻しました(再起動の前の時刻)");
        }
    }
    //DS3231から戻したときのAONタイマーへの写しは、次の Update() が「中身が無い」と見て書く
    if(restored == Source::None && !ds_present && !HasAon()){
        LOG_SYS_MSG("RTC: 時刻の控えがありません(NTPか手動設定で合わせます)");
    }
}

void RtcFunctions::UpdateAt(unsigned long now_ms, time_t now){
    if(has_last && now_ms - last_check_ms < kCheckIntervalMs) return;
    last_check_ms = now_ms;

    const bool jumped = has_last && IsJump(last_ms, last_t, now_ms, now);
    has_last = true;
    last_ms = now_ms;
    last_t  = now;
    if(!ClockValid(now)) return;

    //控えの中身が無ければ、飛ばなくても今の時刻を書く(DS3231のOSF・AONタイマーが止まっている)
    const bool ds_needs  = ds_present && !ds_valid && !ds_failed;
    const bool aon_needs = HasAon() && !AonRunning();
    if(!jumped && !ds_needs && !aon_needs) return;

    if(SaveNow(now)){
        LOG_SYS_MSG("RTC: 時刻を控えました%s", jumped ? "(時計が合わせ直された)" : "");
    }
}

void RtcFunctions::Update(){
    UpdateAt(millis(), time(nullptr));
}

bool RtcFunctions::HasDs3231(){ return ds_present; }
bool RtcFunctions::Ds3231Valid(){ return ds_present && ds_valid; }
bool RtcFunctions::HasAon(){ return PICOOS_RTC_AON != 0; }
RtcFunctions::Source RtcFunctions::RestoredFrom(){ return restored; }
