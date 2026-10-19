#pragma once

#include <cstdint>
#include <ctime>

// ホーム画面のウィジェットが使う「今日」と時差(Ical / Todoist と同じ数え方: 1970-01-01 からの通算日数、現地時刻)
namespace HomeTime {
    // NTP同期前(1970年付近)の時計は当てにならないので「今日」として扱わない
    constexpr int kMinValidYear = 2020;

    inline int32_t DaysFromCivil(int y, int m, int d){
        y -= m <= 2;
        const int32_t era = (y >= 0 ? y : y - 399) / 400;
        const int32_t yoe = y - era * 400;
        const int32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
        const int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + doe - 719468;
    }

    // 現地時刻の今日(時計が合っていなければ -1)と、その日の0時からの秒
    inline int32_t Today(int32_t* now_sec = nullptr){
        const time_t now = time(nullptr);
        struct tm l;
        localtime_r(&now, &l);
        if(l.tm_year + 1900 < kMinValidYear) return -1;
        if(now_sec) *now_sec = l.tm_hour * 3600 + l.tm_min * 60 + l.tm_sec;
        return DaysFromCivil(l.tm_year + 1900, l.tm_mon + 1, l.tm_mday);
    }

    // UTCからのずれ(秒)。newlib に tm_gmtoff が無いので localtime と gmtime の差から求める
    inline int32_t UtcOffsetSec(){
        const time_t now = time(nullptr);
        struct tm l, u;
        localtime_r(&now, &l);
        gmtime_r(&now, &u);
        auto secs = [](const struct tm& t) -> int64_t {
            return (int64_t)DaysFromCivil(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday) * 86400
                 + t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec;
        };
        return (int32_t)(secs(l) - secs(u));
    }
}
