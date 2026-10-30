#include "devtools/HwTest_Functions.hpp"
#include "apps/firmware/Fw_Version.hpp"
#include "devtools/DevTools_Functions.hpp"
#include "devtools/CrashDump_Functions.hpp"
#include "system/Mem_Functions.hpp"
#include "hal/Battery_Functions.hpp"
#include "hal/Network_Functions.hpp"
#include "hal/Sound_Functions.hpp"
#include "hal/Clock_Functions.hpp"
#include "hal/I2C_Bus.hpp"
#include "hal/CardKB_Functions.hpp"
#include "hal/Rtc_Functions.hpp"
#include "hal/WiiClassic_Functions.hpp"
#include "services/Power_Functions.hpp"
#include "system/Log_Functions.hpp"

#include <Arduino.h>
#include <Wire.h>
#include <string.h>
#include <stdlib.h>
#if !defined(PICOOS_PC)
#include <hardware/gpio.h>
#endif

namespace {
    void PrintHelp(){
        LOG_SYS_MSG("[HW] コマンド: hw info / hw boot / hw mem / hw clock [MHz] / hw i2c / hw sleep / hw trap / hw buswrite / hw hang");
    }

    const char* I2cName(uint8_t addr){
        if(addr == WiiClassicFunctions::kAddress) return "Wiiクラシック/ヌンチャク";
        if(addr == CardKbFunctions::kAddress)     return "CardKB2";
        if(addr == RtcFunctions::kDs3231Address)  return "DS3231(RTC)";
        return "";
    }

    // I2C0(GP0/GP1)のスキャン。つないだ機器が見えるか・プルアップが効いているかを確かめる。
    // 0x52が見えたら暗号化なしの初期化をして機器のIDを読む(クラシックとヌンチャクの見分け)
    void I2c(){
        I2cBus::Begin();
#if !defined(PICOOS_PC)
        const int sda = gpio_get(I2cBus::kSdaPin), scl = gpio_get(I2cBus::kSclPin);
        LOG_SYS_MSG("[HW] I2C0 SDA(GP%d)=%d SCL(GP%d)=%d(何もしていない時は両方1。0ならプルアップが無いかGNDへ短絡)",
                    I2cBus::kSdaPin, sda, I2cBus::kSclPin, scl);
#endif
        // 接触不良だと応答したりしなかったりするので、何回かやって数える
        constexpr int kPasses = 5;
        uint8_t hits[0x78] = {};
        for(int pass = 0; pass < kPasses; pass++){
            for(uint8_t addr = 0x08; addr <= 0x77; addr++){
                Wire.beginTransmission(addr);
                if(Wire.endTransmission() == 0) hits[addr]++;
            }
            delay(20);
        }
        int found = 0;
        bool wii = false;
        for(uint8_t addr = 0x08; addr <= 0x77; addr++){
            if(hits[addr] == 0) continue;
            found++;
            if(addr == WiiClassicFunctions::kAddress) wii = true;
            LOG_SYS_MSG("[HW] I2C 0x%02X 応答あり %d/%d回 %s", addr, hits[addr], kPasses, I2cName(addr));
        }
        if(found == 0){
            LOG_SYS_WARN("[HW] I2C 応答する機器がありません(電源3.3V・SDA/SCLの入れ替え・プルアップ・プラグの接触・断線を確認)");
            return;
        }
        if(!wii) return;

        // 0x52: 暗号化なしの初期化 → 0xFA から6バイトがID(クラシック=...A4 20 01 01、ヌンチャク=...A4 20 00 00)
        static const uint8_t kInit1[] = { 0xF0, 0x55 };
        static const uint8_t kInit2[] = { 0xFB, 0x00 };
        bool ok = true;
        Wire.beginTransmission(WiiClassicFunctions::kAddress);
        for(uint8_t b : kInit1) Wire.write(b);
        ok = ok && Wire.endTransmission() == 0;
        Wire.beginTransmission(WiiClassicFunctions::kAddress);
        for(uint8_t b : kInit2) Wire.write(b);
        ok = ok && Wire.endTransmission() == 0;
        if(!ok){
            LOG_SYS_WARN("[HW] 0x52 初期化の書き込みがNACKでした");
            return;
        }
        delay(5);
        Wire.beginTransmission(WiiClassicFunctions::kAddress);
        Wire.write((uint8_t)0xFA);
        Wire.endTransmission();
        delay(1);
        uint8_t id[6] = {};
        int n = 0;
        if(Wire.requestFrom(WiiClassicFunctions::kAddress, (uint8_t)6) == 6){
            for(; n < 6; n++){
                const int v = Wire.read();
                if(v < 0) break;
                id[n] = (uint8_t)v;
            }
        }
        if(n < 6){
            LOG_SYS_WARN("[HW] 0x52 IDが読めません(%dバイト)", n);
            return;
        }
        const char* kind = "不明";
        if(id[2] == 0xA4 && id[3] == 0x20){
            if(id[4] == 0x01 && id[5] == 0x01) kind = "Wiiクラシックコントローラー";
            else if(id[4] == 0x00 && id[5] == 0x00) kind = "ヌンチャク";
        }
        LOG_SYS_MSG("[HW] 0x52 ID=%02X %02X %02X %02X %02X %02X → %s", id[0], id[1], id[2], id[3], id[4], id[5], kind);

        // 状態も1回読む(ボタンは0=押している。何も押していなければ [4][5] は FF 近く)
        Wire.beginTransmission(WiiClassicFunctions::kAddress);
        Wire.write((uint8_t)0x00);
        Wire.endTransmission();
        delay(5);
        uint8_t d[6] = {};
        n = 0;
        if(Wire.requestFrom(WiiClassicFunctions::kAddress, (uint8_t)6) == 6){
            for(; n < 6; n++){
                const int v = Wire.read();
                if(v < 0) break;
                d[n] = (uint8_t)v;
            }
        }
        if(n == 6) LOG_SYS_MSG("[HW] 0x52 状態=%02X %02X %02X %02X %02X %02X", d[0], d[1], d[2], d[3], d[4], d[5]);
        else LOG_SYS_WARN("[HW] 0x52 状態が読めません(%dバイト)", n);
    }

    void PrintInfo(){
        const uint32_t ms = millis();
        LOG_SYS_MSG("[HW] uptime=%lu.%03lus", (unsigned long)(ms / 1000), (unsigned long)(ms % 1000));
        LOG_SYS_MSG("[HW] firmware=%s %s", FwVersion::Text(), FwVersion::Date());
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

    // スリープ中のクロック低減の様子。MHzを付けると sleep-cpu-mhz を今だけ変える(display.cfgへは書かない)
    void Clock(const char* arg){
        while(*arg == ' ') ++arg;
        if(*arg != '\0'){
            char* end = nullptr;
            const long v = strtol(arg, &end, 10);
            if(end == arg || *end != '\0' || v < 0 || v > 1000){
                LOG_SYS_WARN("[HW] hw clock の後は0(下げない)かMHzの整数です: %s", arg);
                return;
            }
            PowerFunctions::SetSleepCpuMhz((unsigned long)v);
        }
        const uint32_t div = ClockFunctions::DividerForMhz((uint32_t)PowerFunctions::GetSleepCpuMhz());
        LOG_SYS_MSG("[HW] clk_sys=%luHz normal=%luHz clk_peri=%luHz min=%luHz available=%s",
                    (unsigned long)ClockFunctions::CurrentHz(), (unsigned long)ClockFunctions::NormalHz(),
                    (unsigned long)ClockFunctions::PeriHz(), (unsigned long)ClockFunctions::MinHz(),
                    ClockFunctions::Available() ? "yes" : "no");
        LOG_SYS_MSG("[HW] sleep-cpu-mhz=%lu → スリープ中 %luHz(1/%lu) sleeping=%s",
                    (unsigned long)PowerFunctions::GetSleepCpuMhz(),
                    (unsigned long)(ClockFunctions::NormalHz() / div), (unsigned long)div,
                    PowerFunctions::IsSleeping() ? "yes" : "no");
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
    else if(strncmp(cmd, "clock", 5) == 0 && (cmd[5] == ' ' || cmd[5] == '\0')) Clock(cmd + 5);
    else if(strcmp(cmd, "i2c") == 0) I2c();
    else if(strcmp(cmd, "sleep") == 0){
        LOG_SYS_MSG("[HW] スリープへ入ります(タッチ・ボタン・打鍵で起きる)");
        PowerFunctions::SleepNow();
    }
    else if(strcmp(cmd, "trap") == 0) Trap();
    else if(strcmp(cmd, "buswrite") == 0) BusWrite();
    else if(strcmp(cmd, "hang") == 0) Hang();
    else{
        LOG_SYS_WARN("[HW] 知らないコマンドです: %s", cmd);
        PrintHelp();
    }
    return true;
}
