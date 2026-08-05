#pragma once

#include <Arduino.h>
#include <cstdarg>
#include <cstdio>

namespace LogFunctions{
    
    enum class LogType
    {
        SYS_OK,
        SYS_WARN,
        SYS_FAIL,
        SYS_DBG,
        SYS_MSG,

        APP_OK,
        APP_WARN,
        APP_FAIL,
        APP_DBG,
        APP_MSG,

        UNKNOWN
    };

    inline const char* getPrefix(LogType type)
    {
        switch (type)
        {
            case LogType::SYS_OK:       return "[SYS-OK] ";
            case LogType::SYS_WARN:     return "[SYS_WARN] ";
            case LogType::SYS_FAIL:     return "[SYS-FAIL] ";
            case LogType::SYS_DBG:      return "[SYS_DEBUG] ";
            case LogType::SYS_MSG:      return "[SYS_MSG] ";

            case LogType::APP_OK:       return "[APP-OK] ";
            case LogType::APP_WARN:     return "[APP-WARN] ";
            case LogType::APP_FAIL:     return "[APP-FAILED] ";
            case LogType::APP_DBG:      return "[APP_DBG] ";
            case LogType::APP_MSG:      return "[APP_MSG] ";

            case LogType::UNKNOWN:
            default:                    return "[UNKNOWN] ";
        }
    }

    inline void log(LogType type, const char* fmt, ...)
    {
        Serial.printf("%s", getPrefix(type));

        char buf[256];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);

        Serial.print(buf);
        Serial.println();
    }

    #define LOG_SYS_OK(...)         LogFunctions::log(LogFunctions::LogType::SYS_OK, __VA_ARGS__)
    #define LOG_SYS_WARN(...)       LogFunctions::log(LogFunctions::LogType::SYS_WARN, __VA_ARGS__)
    #define LOG_SYS_FAIL(...)       LogFunctions::log(LogFunctions::LogType::SYS_FAIL, __VA_ARGS__)
    #define LOG_SYS_DEBUG(...)      LogFunctions::log(LogFunctions::LogType::SYS_DBG, __VA_ARGS__)
    #define LOG_SYS_MSG(...)        LogFunctions::log(LogFunctions::LogType::SYS_MSG, __VA_ARGS__)

    #define LOG_APP_OK(...)         LogFunctions::log(LogFunctions::LogType::APP_OK, __VA_ARGS__)
    #define LOG_APP_WARN(...)       LogFunctions::log(LogFunctions::LogType::APP_WARN, __VA_ARGS__)
    #define LOG_APP_FAIL(...)       LogFunctions::log(LogFunctions::LogType::APP_FAIL, __VA_ARGS__)
    #define LOG_APP_DEBUG(...)      LogFunctions::log(LogFunctions::LogType::APP_DBG, __VA_ARGS__)
    #define LOG_APP_MSG(...)        LogFunctions::log(LogFunctions::LogType::APP_MSG, __VA_ARGS__)
}