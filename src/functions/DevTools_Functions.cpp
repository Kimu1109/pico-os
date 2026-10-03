#include "functions/DevTools_Functions.hpp"
#include "functions/Profiler_Functions.hpp"
#include "functions/CrashDump_Functions.hpp"
#include "functions/Config_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "lua/LuaDebugger.hpp"
#include "storage/SD_Path.hpp"
#include "OS_Data.hpp"

#include <Arduino.h>
#include <string.h>

namespace {
    bool perf_overlay = false;
    bool perf_log = false;
    bool lua_debugger = false;
    bool watchdog = false;
    uint32_t watchdog_ms = CrashDumpFunctions::kDefaultWatchdogMs;
    uint32_t last_log_ms = 0;

    void ApplyProfiler(){
        ProfilerFunctions::SetEnabled(perf_overlay || perf_log);
        if(DevToolsFunctions::perf_overlay_visibility) DevToolsFunctions::perf_overlay_visibility(perf_overlay);
    }

    void Save(const char* key, bool value){
        if(!OSData::SD_usable) return;
        PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_DEBUG_CFG, key, PICO_Config::ConfigValue::FromBool(value));
    }

    void ReadBool(const char* key, const char* value, bool& out){
        bool v = false;
        if(PICO_Config::ConfigValue::AsBool(value, v)) out = v;
        else LOG_SYS_WARN("debug.cfg: %s は true/false です: %s", key, value);
    }
}

void DevToolsFunctions::Setup(){
    perf_overlay = perf_log = lua_debugger = watchdog = false;
    watchdog_ms = CrashDumpFunctions::kDefaultWatchdogMs;

    if(OSData::SD_usable && OSData::SD.exists(PICO_Path::FILE::CFG::SYS_DEBUG_CFG)){
        PICO_Config::ParseFile(PICO_Path::FILE::CFG::SYS_DEBUG_CFG, [](const char* key, const char* value){
            if(strcmp(key, "perf-overlay") == 0) ReadBool(key, value, perf_overlay);
            else if(strcmp(key, "perf-log") == 0) ReadBool(key, value, perf_log);
            else if(strcmp(key, "lua-debugger") == 0) ReadBool(key, value, lua_debugger);
            else if(strcmp(key, "watchdog") == 0) ReadBool(key, value, watchdog);
            else if(strcmp(key, "watchdog-ms") == 0){
                int v = 0;
                if(PICO_Config::ConfigValue::AsInt(value, v) && v >= 1000 && v <= 8300) watchdog_ms = (uint32_t)v;
                else LOG_SYS_WARN("debug.cfg: watchdog-ms は1000〜8300です: %s", value);
            }
        });
    }

    ApplyProfiler();
    ::LuaDebugger::SetGlobalEnabled(lua_debugger);
    if(watchdog) CrashDumpFunctions::EnableWatchdog(watchdog_ms);
    last_log_ms = millis();
}

void DevToolsFunctions::Update(){
    if(!perf_log) return;
    const uint32_t now = millis();
    if(now - last_log_ms < 5000) return;
    last_log_ms = now;
    ProfilerFunctions::LogReport();
}

bool DevToolsFunctions::PerfOverlay(){ return perf_overlay; }
bool DevToolsFunctions::PerfLog(){ return perf_log; }
bool DevToolsFunctions::LuaDebuggerEnabled(){ return lua_debugger; }
bool DevToolsFunctions::Watchdog(){ return watchdog; }

void DevToolsFunctions::SetPerfOverlay(bool on){
    perf_overlay = on;
    ApplyProfiler();
    Save("perf-overlay", on);
}

void DevToolsFunctions::SetPerfLog(bool on){
    perf_log = on;
    ApplyProfiler();
    Save("perf-log", on);
}

void DevToolsFunctions::SetLuaDebugger(bool on){
    lua_debugger = on;
    ::LuaDebugger::SetGlobalEnabled(on);
    Save("lua-debugger", on);
}

void DevToolsFunctions::SetWatchdog(bool on){
    watchdog = on;
    if(on) CrashDumpFunctions::EnableWatchdog(watchdog_ms);
    else CrashDumpFunctions::EnableWatchdog(0);
    Save("watchdog", on);
}
