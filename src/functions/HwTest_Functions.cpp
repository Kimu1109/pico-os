#include "functions/HwTest_Functions.hpp"
#include "functions/DevTools_Functions.hpp"
#include "functions/CrashDump_Functions.hpp"
#include "functions/Mem_Functions.hpp"
#include "functions/Battery_Functions.hpp"
#include "functions/Network_Functions.hpp"
#include "functions/Sound_Functions.hpp"
#include "functions/Log_Functions.hpp"

#include <Arduino.h>
#include <string.h>

namespace {
    void PrintHelp(){
        LOG_SYS_MSG("[HW] コマンド: hw info / hw boot / hw mem / hw trap / hw buswrite / hw hang");
    }

    void PrintInfo(){
        const uint32_t ms = millis();
        LOG_SYS_MSG("[HW] uptime=%lu.%03lus", (unsigned long)(ms / 1000), (unsigned long)(ms % 1000));
        if(BatteryFunctions::HasSample()){
            LOG_SYS_MSG("[HW] battery=%d%% usb=%s", BatteryFunctions::GetPercent(),
                        BatteryFunctions::IsExternallyPowered() ? "yes" : "no");
        }else{
            LOG_SYS_MSG("[HW] battery=(まだ読んでいない)");
        }
        LOG_SYS_MSG("[HW] wifi=%s ssid=%s", NetworkFunctions::IsConnected() ? "connected" : "not-connected",
                    NetworkFunctions::currentSSID.c_str());
        LOG_SYS_MSG("[HW] sound rate=%luHz", (unsigned long)SoundFunctions::SampleRate());
        LOG_SYS_MSG("[HW] watchdog=%s", CrashDumpFunctions::WatchdogEnabled() ? "on" : "off");
    }

    // 起動直後のログはUSBシリアルを開き直す前に流れて見えないので、後から見られるようにする
    void PrintBoot(){
        const CrashDumpFunctions::BootInfo& b = CrashDumpFunctions::GetBootInfo();
        LOG_SYS_MSG("[HW] reason=0x%lx watchdog_caused=%d watchdog_timeout=%d",
                    (unsigned long)b.reason, b.watchdog_caused ? 1 : 0, b.watchdog_enable_caused ? 1 : 0);
        LOG_SYS_MSG("[HW] RAMの記録 at 0x%08lx magic=0x%08lx valid=%d crumbs=0x%08lx",
                    (unsigned long)b.record_addr, (unsigned long)b.record_magic, b.record_valid ? 1 : 0,
                    (unsigned long)b.crumbs_magic);
        LOG_SYS_MSG("[HW] scratch=0x%08lx 0x%08lx 0x%08lx 0x%08lx",
                    (unsigned long)b.scratch[0], (unsigned long)b.scratch[1],
                    (unsigned long)b.scratch[2], (unsigned long)b.scratch[3]);
        const char* dump = CrashDumpFunctions::LastDumpPath();
        LOG_SYS_MSG("[HW] 前回の記録=%s ダンプ=%s", b.source ? b.source : "なし", dump ? dump : "(書いていない)");
    }

    void PrintMem(){
        const MemFunctions::Snapshot s = MemFunctions::Take(true);
        LOG_SYS_MSG("[HW] heap arena=%lu used=%lu free=%lu blocks=%lu",
                    (unsigned long)s.arena, (unsigned long)s.used, (unsigned long)s.free_total, (unsigned long)s.free_blocks);
        LOG_SYS_MSG("[HW] heap largest=%lu%s frag=%u%% headroom=%lu",
                    (unsigned long)s.largest_free, s.largest_free_capped ? "+" : "",
                    (unsigned)(MemFunctions::FragmentationPermil(s) / 10), (unsigned long)s.stack_headroom);
        const size_t room = MemFunctions::HeapTopRoom();
        if(room == MemFunctions::kRoomUnknown) LOG_SYS_MSG("[HW] heap top room=(不明)");
        else LOG_SYS_MSG("[HW] heap top room=%lu", (unsigned long)room);
    }

    [[noreturn]] void Trap(){
        LOG_SYS_MSG("[HW] 未定義命令を実行します(HardFault)");
        __builtin_trap();
    }

    void BusWrite(){
        LOG_SYS_MSG("[HW] 0xF0000000 へ書きます(HardFault)");
        *(volatile uint32_t*)0xF0000000u = 0x12345678u;
        LOG_SYS_WARN("[HW] 書き込みで落ちませんでした");
    }

    void Hang(){
        if(!CrashDumpFunctions::WatchdogEnabled()){
            LOG_SYS_WARN("[HW] ウォッチドッグが切れています(debug.cfg の watchdog = true にして再起動)");
            return;
        }
        LOG_SYS_MSG("[HW] loop()を止めます(ウォッチドッグで再起動するはず)");
        volatile uint32_t spin = 0;
        for(;;) spin = spin + 1;
    }
}

bool HwTestFunctions::FeedSerialLine(const char* line){
    if(!line || strncmp(line, "hw", 2) != 0 || (line[2] != ' ' && line[2] != '\0')) return false;
    if(!DevToolsFunctions::HwTest()){
        LOG_SYS_WARN("[HW] 切れています(/sys/debug.cfg に hw-test = true を書いて再起動)");
        return true;
    }
    const char* cmd = line + 2;
    while(*cmd == ' ') ++cmd;

    if(*cmd == '\0' || strcmp(cmd, "help") == 0) PrintHelp();
    else if(strcmp(cmd, "info") == 0) PrintInfo();
    else if(strcmp(cmd, "boot") == 0) PrintBoot();
    else if(strcmp(cmd, "mem") == 0) PrintMem();
    else if(strcmp(cmd, "trap") == 0) Trap();
    else if(strcmp(cmd, "buswrite") == 0) BusWrite();
    else if(strcmp(cmd, "hang") == 0) Hang();
    else{
        LOG_SYS_WARN("[HW] 知らないコマンドです: %s", cmd);
        PrintHelp();
    }
    return true;
}
