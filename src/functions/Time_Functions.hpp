#pragma once

#include "Arduino.h"
#include "time.h"

namespace TimeFunctions {
    inline int year;
    inline int month;
    inline int day;
    inline int hour;
    inline int minute;
    inline int second;

    inline struct tm timeinfo;

    inline unsigned long last_update;

    inline void Setup(){
        last_update = millis();

        setenv("TZ", "JST-9", 1);
        tzset();
    };
    inline void Update(){
        if(millis() - last_update > 333){
            time_t now = time(nullptr);
            localtime_r(&now, &timeinfo);

            year   = timeinfo.tm_year + 1900; // tm_yearは1900年からのオフセット
            month  = timeinfo.tm_mon + 1;     // tm_monは0始まり(0=1月)
            day    = timeinfo.tm_mday;
            hour   = timeinfo.tm_hour;
            minute = timeinfo.tm_min;
            second = timeinfo.tm_sec;

            Serial.printf("%d年%d月%d日 %d時%d分%d秒\n", year, month, day, hour, minute, second);

            last_update = millis();
        };
    }
};