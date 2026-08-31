#pragma once

#include "Arduino.h"
#include "time.h"
#include "functions/Config_Functions.hpp"
#include "storage/SD_Path.hpp"

namespace TimeFunctions {
    inline int year;
    inline int month;

    inline int before_hour = -1;
    inline int before_minute = -1;
    inline bool changed_HH_mm = false;

    inline struct tm timeinfo;

    inline unsigned long last_update;

    inline void Setup(){
        last_update = millis();

        PICO_Config::ParseFile(PICO_Path::FILE::SYS_NETWORK_CFG,
            [&](const char* key, const char* value){
                if(strcmp(key, "timezone") == 0){
                    setenv("TZ", value, 1);
                    tzset();
                }
            }
        );
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