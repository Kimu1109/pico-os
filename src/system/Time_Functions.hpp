#pragma once

#include "Arduino.h"
#include "time.h"
#include "system/Config_Functions.hpp"
#include "storage/SD_Path.hpp"
#if defined(PICOOS_PC)
    #include <PicoPcClock.h>
#else
    #include <sys/time.h>
#endif

namespace TimeFunctions {
    inline int year;
    inline int month;

    inline int before_hour = -1;
    inline int before_minute = -1;
    inline bool changed_HH_mm = false;

    inline struct tm timeinfo;

    inline unsigned long last_update;

    // POSIX TZ文字列を今すぐ反映する。Setup()と設定アプリ(タイムゾーン変更時)の
    // 両方から呼ぶための共通口
    inline void ApplyTimezone(const char* tz){
        setenv("TZ", tz, 1);
        tzset();
    }

    // 時計を合わせ済みとみなす年(これより前はNTP同期前の起動直後の時刻)。
    // アラーム・通知・TLSは時計が合うまで動かない(それぞれ2020年で見ている)
    constexpr int kValidYear = 2020;

    // 次のUpdate()で timeinfo を読み直させる(時計を変えた直後に表示を待たせない)
    inline void Refresh(){
        last_update = millis() - 1000;
        before_hour = -1;
    }

    // 本体の時計をエポック秒 t にする(Wi-Fi・NTPが無いときの手動設定。設定アプリの「時刻」)。
    // 実機は settimeofday()。PCは母艦の時計を変えずに「ずれ」を持つ(pc/compat/PicoPcClock.h)。
    // 後でNTPに同期できれば、そちらの時刻で上書きされる
    inline bool SetClock(time_t t){
#if defined(PICOOS_PC)
        const bool ok = PicoPcClock::Set((long long)t);
#else
        struct timeval tv;
        tv.tv_sec  = t;
        tv.tv_usec = 0;
        const bool ok = settimeofday(&tv, nullptr) == 0;
#endif
        if(ok) Refresh();
        return ok;
    }

    // 現地時刻(今のタイムゾーン)で合わせる。月は1〜12。範囲外や変換できない値ならfalse
    inline bool SetLocalDateTime(int y, int mon, int d, int h, int mi, int sec){
        if(y < 1970 || y > 2099 || mon < 1 || mon > 12 || d < 1 || d > 31
            || h < 0 || h > 23 || mi < 0 || mi > 59 || sec < 0 || sec > 59) return false;
        struct tm t = {};
        t.tm_year  = y - 1900;
        t.tm_mon   = mon - 1;
        t.tm_mday  = d;
        t.tm_hour  = h;
        t.tm_min   = mi;
        t.tm_sec   = sec;
        t.tm_isdst = -1;
        const time_t v = mktime(&t);
        if(v == (time_t)-1) return false;
        //31日の無い月の31日等は mktime が翌月へ繰り越すので、そのまま断る
        if(t.tm_mday != d || t.tm_mon != mon - 1) return false;
        return SetClock(v);
    }

    // 時計が合っているか(NTP同期・手動設定のどちらか)
    inline bool IsClockSet(){
        time_t now = time(nullptr);
        struct tm t;
        localtime_r(&now, &t);
        return t.tm_year + 1900 >= kValidYear;
    }

    inline void Setup(){
        last_update = millis();

        PICO_Config::ParseFile(PICO_Path::FILE::CFG::SYS_NETWORK_CFG,
            [&](const char* key, const char* value){
                if(strcmp(key, "timezone") == 0){
                    ApplyTimezone(value);
                }
            }
        );

        LOG_SYS_OK("Time Setup has succeeded!");
    };
    inline void Update(){
        changed_HH_mm = false;

        if(millis() - last_update > 333){
            time_t now = time(nullptr);
            localtime_r(&now, &timeinfo);

            year   = timeinfo.tm_year + 1900; // tm_yearは1900年からのオフセット
            month  = timeinfo.tm_mon + 1;     // tm_monは0始まり(0=1月)

            if(before_hour != timeinfo.tm_hour || before_minute != timeinfo.tm_min){
                changed_HH_mm = true;
                before_hour = timeinfo.tm_hour;
                before_minute = timeinfo.tm_min;
            }

            last_update = millis();
        };
    }
};