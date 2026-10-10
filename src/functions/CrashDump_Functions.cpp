#include "functions/CrashDump_Functions.hpp"
#include <cstdlib>
#include "functions/Profiler_Functions.hpp"
#include "functions/Notification_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "OS_Data.hpp"
#include "consts.hpp"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <stddef.h>

#if defined(PICOOS_PC)
    #include <signal.h>
    #include <unistd.h>
    #include <string>
    #if !defined(__EMSCRIPTEN__)
        #include <atomic>
        #include <thread>
    #endif
#else
    #include "hardware/watchdog.h"
#endif

namespace {
    constexpr uint32_t kRecordMagic = 0x50435244; // "PCRD"
    constexpr uint32_t kCrumbsMagic = 0x50434342; // "PCCB"

    // ---- 消えないRAM(実機) ----
    // .uninitialized_data はpico-sdkのリンカスクリプトが「起動時に0クリアしない」領域。
    // 電源を入れた直後は中身がでたらめなので、magicとchecksumで確かめてから使う
#if defined(PICOOS_PC)
    CrashDumpFunctions::Record pending_record;
    CrashDumpFunctions::Crumbs crumbs;
#else
    __attribute__((section(".uninitialized_data.pico_os_crash")))
    CrashDumpFunctions::Record pending_record;
    __attribute__((section(".uninitialized_data.pico_os_crumbs")))
    CrashDumpFunctions::Crumbs crumbs;
#endif

    const char* last_scene_ptr = nullptr;
    const char* last_lua_ptr = nullptr;
    bool watchdog_on = false;
    uint32_t watchdog_ms = 0;
    int lua_dumps_this_boot = 0;
    int next_no = 1;
    char last_dump_path[48] = {};

    uint32_t Checksum(const CrashDumpFunctions::Record& r){
        //checksumの手前までのFNV-1a
        const uint8_t* p = (const uint8_t*)&r;
        const size_t n = offsetof(CrashDumpFunctions::Record, checksum);
        uint32_t h = 2166136261u;
        for(size_t i = 0; i < n; i++){ h ^= p[i]; h *= 16777619u; }
        return h;
    }

    void CopyStr(char* dst, size_t size, const char* src){
        if(!dst || size == 0) return;
        size_t i = 0;
        if(src){
            for(; i + 1 < size && src[i]; i++) dst[i] = src[i];
        }
        dst[i] = '\0';
    }

    const char* KindName(uint32_t kind){
        switch((CrashDumpFunctions::Kind)kind){
            case CrashDumpFunctions::Kind::HardFault: return "HardFault(不正なメモリアクセス・未定義命令など)";
            case CrashDumpFunctions::Kind::Watchdog:  return "ウォッチドッグ(処理が戻らず再起動)";
            case CrashDumpFunctions::Kind::Signal:    return "シグナル(PCビルド)";
            case CrashDumpFunctions::Kind::Test:      return "テスト(動作確認用)";
            default: return "不明";
        }
    }

    // 文字列を足していく小さな係(snprintfの戻り値の扱いを1か所にまとめる)
    struct Out {
        char* buf; size_t size; size_t len;
        void add(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
            if(len + 1 >= size) return;
            va_list ap; va_start(ap, fmt);
            const int n = vsnprintf(buf + len, size - len, fmt, ap);
            va_end(ap);
            if(n < 0) return;
            len += (size_t)n;
            if(len >= size) len = size - 1;
        }
    };

    void AddClock(Out& o){
        const time_t now = time(nullptr);
        struct tm tmv;
        if(localtime_r(&now, &tmv) && tmv.tm_year + 1900 >= 2020){
            o.add("記録した時刻: %04d-%02d-%02d %02d:%02d:%02d\n",
                tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
        }
    }

#if defined(PICOOS_PC)
    // シグナルハンドラから書く先(Setup時に組み立てておく。ハンドラの中でstd::stringを作らない)
    char pending_fs_path[512] = {};

    // <fcntl.h>のopen()は使わない(ホストテストのSdFatのスタブがO_CREAT等を自前で定義していて、
    // 翻訳単位ごとに値が食い違うとinlineのopen()が壊れるため。CLAUDE.md「テストは全て手動」参照)
    void WritePendingFile(const CrashDumpFunctions::Record& r){
        if(!pending_fs_path[0]) return;
        FILE* fp = fopen(pending_fs_path, "wb");
        if(!fp) return;
        fwrite(&r, 1, sizeof(r), fp);
        fclose(fp);
    }

    void SignalHandler(int sig){
        CrashDumpFunctions::Record r;
        CrashDumpFunctions::FillRecordFromCrumbs(r, CrashDumpFunctions::Kind::Signal);
        r.signal = (uint32_t)sig;
        CrashDumpFunctions::SealRecord(r);
        WritePendingFile(r);
        const char msg[] = "[CRASH] 落ちました。次の起動で /crash/ へダンプを書きます\n";
        ssize_t w = write(2, msg, sizeof(msg) - 1);
        (void)w;
        signal(sig, SIG_DFL);
        raise(sig);
    }

    #if !defined(__EMSCRIPTEN__)
    std::atomic<uint32_t> last_feed_ms{0};
    std::atomic<bool> watchdog_thread_started{false};

    void WatchdogThread(){
        for(;;){
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            if(!watchdog_on) continue;
            const uint32_t now = (uint32_t)millis();
            if(now - last_feed_ms.load() > watchdog_ms){
                CrashDumpFunctions::Record r;
                CrashDumpFunctions::FillRecordFromCrumbs(r, CrashDumpFunctions::Kind::Watchdog);
                CrashDumpFunctions::SealRecord(r);
                WritePendingFile(r);
                fprintf(stderr, "[CRASH] ウォッチドッグ: %lums応答がありません。終了します\n", (unsigned long)watchdog_ms);
                _exit(3);
            }
        }
    }
    #endif
#endif
}

// ---------------- 記録 ----------------

bool CrashDumpFunctions::RecordValid(const Record& r){
    return r.magic == kRecordMagic && r.kind != 0 && r.checksum == Checksum(r);
}

void CrashDumpFunctions::SealRecord(Record& r){
    r.magic = kRecordMagic;
    r.scene[sizeof(r.scene) - 1] = '\0';
    r.lua_app[sizeof(r.lua_app) - 1] = '\0';
    r.checksum = Checksum(r);
}

void CrashDumpFunctions::FillRecordFromCrumbs(Record& r, Kind kind){
    memset(&r, 0, sizeof(r));
    r.kind = (uint32_t)kind;
    r.phase = 0xFF;
    if(crumbs.magic == kCrumbsMagic){
        r.uptime_ms = crumbs.uptime_ms;
        r.frame_no = crumbs.frame_no;
        r.phase = crumbs.phase;
        r.in_lua = crumbs.in_lua;
        memcpy(r.scene, crumbs.scene, sizeof(r.scene));
        memcpy(r.lua_app, crumbs.lua_app, sizeof(r.lua_app));
        r.scene[sizeof(r.scene) - 1] = '\0';
        r.lua_app[sizeof(r.lua_app) - 1] = '\0';
    }
    //落ちた瞬間の時刻(パンくずはフレームの頭のもの)
    r.uptime_ms = (uint32_t)millis();
}

void CrashDumpFunctions::StorePendingRecord(const Record& r){
    pending_record = r;
#if defined(PICOOS_PC)
    WritePendingFile(r);
#endif
}

size_t CrashDumpFunctions::FormatRecord(const Record& r, char* out, size_t size){
    if(!out || size == 0) return 0;
    out[0] = '\0';
    Out o{out, size, 0};
    o.add("pico-os クラッシュダンプ\n");
    o.add("種類: %s\n", KindName(r.kind));
    if(r.kind == (uint32_t)Kind::Signal) o.add("シグナル番号: %lu\n", (unsigned long)r.signal);
    AddClock(o);
    o.add("起動からの時間: %lu.%03lu秒\n", (unsigned long)(r.uptime_ms / 1000), (unsigned long)(r.uptime_ms % 1000));
    o.add("フレーム: %lu\n", (unsigned long)r.frame_no);
    o.add("画面(そのフレームの頭の時点): %s\n", r.scene[0] ? r.scene : "(不明)");
    if(r.phase < (uint32_t)ProfilerFunctions::Section::Count){
        o.add("処理していたところ: %s\n", ProfilerFunctions::SectionName((ProfilerFunctions::Section)r.phase));
    }else if(r.phase == kPhaseIdle){
        o.add("処理していたところ: フレームの間(休み)\n");
    }else{
        o.add("処理していたところ: (不明)\n");
    }
    if(r.lua_app[0]){
        o.add("Luaアプリ: %s(%s)\n", r.lua_app, r.in_lua ? "実行中だった" : "直近に実行");
    }

    if(r.kind == (uint32_t)Kind::HardFault){
        o.add("\n--- レジスタ ---\n");
        o.add("pc =0x%08lx  lr =0x%08lx\n", (unsigned long)r.pc, (unsigned long)r.lr);
        o.add("sp =0x%08lx  xpsr=0x%08lx  exc_return=0x%08lx\n",
            (unsigned long)r.sp, (unsigned long)r.xpsr, (unsigned long)r.exc_return);
        o.add("r0 =0x%08lx  r1 =0x%08lx  r2 =0x%08lx  r3 =0x%08lx  r12=0x%08lx\n",
            (unsigned long)r.r0, (unsigned long)r.r1, (unsigned long)r.r2, (unsigned long)r.r3, (unsigned long)r.r12);
        o.add("CFSR=0x%08lx HFSR=0x%08lx MMFAR=0x%08lx BFAR=0x%08lx\n",
            (unsigned long)r.cfsr, (unsigned long)r.hfsr, (unsigned long)r.mmfar, (unsigned long)r.bfar);
        //CFSRの主なビット(ARMv8-M)
        if(r.cfsr & 0x0000FFFF){
            o.add("原因の手がかり:");
            if(r.cfsr & (1u << 0))  o.add(" 実行できない場所の命令を読んだ(IACCVIOL)");
            if(r.cfsr & (1u << 1))  o.add(" メモリ保護違反(DACCVIOL)");
            if(r.cfsr & (1u << 8))  o.add(" 命令の読み出しでバスエラー(IBUSERR)");
            if(r.cfsr & (1u << 9))  o.add(" データのバスエラー(PRECISERR)");
            if(r.cfsr & (1u << 10)) o.add(" データのバスエラー(IMPRECISERR)");
            if(r.cfsr & (1u << 12)) o.add(" スタックへ積むときのエラー(STKERR。スタックあふれの疑い)");
            o.add("\n");
        }
        if(r.cfsr & 0xFFFF0000){
            o.add("原因の手がかり(UsageFault):");
            if(r.cfsr & (1u << 16)) o.add(" 未定義命令(UNDEFINSTR)");
            if(r.cfsr & (1u << 17)) o.add(" 不正な状態(INVSTATE)");
            if(r.cfsr & (1u << 20)) o.add(" スタックあふれ(STKOF)");
            if(r.cfsr & (1u << 24)) o.add(" アライメント違反(UNALIGNED)");
            if(r.cfsr & (1u << 25)) o.add(" 0除算(DIVBYZERO)");
            o.add("\n");
        }
        if(r.stack_words > 0){
            o.add("\n--- スタック(例外フレームの直後から) ---\n");
            for(uint32_t i = 0; i < r.stack_words && i < (uint32_t)kStackWords; i++){
                o.add("%08lx%s", (unsigned long)r.stack[i], (i % 4 == 3) ? "\n" : " ");
            }
            if(r.stack_words % 4 != 0) o.add("\n");
        }
        o.add("\n--- 解析のしかた ---\n");
        o.add("arm-none-eabi-addr2line -f -C -e .pio/build/rpipico2w/firmware.elf 0x%08lx 0x%08lx\n",
            (unsigned long)r.pc, (unsigned long)r.lr);
        o.add("(スタックの値のうち 0x10000000〜0x10400000 の範囲はフラッシュ上のコードの番地の可能性がある)\n");
    }else if(r.kind == (uint32_t)Kind::Watchdog){
        o.add("\nloop()が決まった時間のうちに戻りませんでした(無限ループ・長いブロッキング処理の疑い)。\n");
        o.add("「処理していたところ」の区間の中で固まりました(「シーン」なら画面の切り替え=onEnter()の中も含む)。\n");
    }
    return o.len;
}

// ---------------- SDへの書き出し ----------------

bool CrashDumpFunctions::WriteDump(const char* prefix, const char* text, size_t len, char* out_path, size_t out_size){
    if(!OSData::SD_usable) return false;
    if(!OSData::SD.exists(kDir)) OSData::SD.mkdir(kDir);

    char path[48];
    for(;;){
        snprintf(path, sizeof(path), "%s/%s_%04d.txt", kDir, prefix, next_no);
        if(!OSData::SD.exists(path)) break;
        if(++next_no > 9999) return false;
    }

    FsFile f = OSData::SD.open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if(!f) return false;
    const size_t w = f.write((const uint8_t*)text, len);
    f.close();
    if(w != len){
        OSData::SD.remove(path);
        return false;
    }
    next_no++;
    if(out_path && out_size > 0) CopyStr(out_path, out_size, path);
    return true;
}

// ---------------- 起動時 ----------------

void CrashDumpFunctions::Setup(){
    last_dump_path[0] = '\0';
    lua_dumps_this_boot = 0;
    last_scene_ptr = nullptr;
    last_lua_ptr = nullptr;

    Record r;
    bool have = false;

#if defined(PICOOS_PC)
    if(OSData::SD_usable && OSData::SD.exists(kPendingPath)){
        FsFile f = OSData::SD.open(kPendingPath, O_RDONLY);
        if(f){
            memset(&r, 0, sizeof(r));
            const int got = f.read((uint8_t*)&r, sizeof(r));
            f.close();
            have = (got == (int)sizeof(r)) && RecordValid(r);
        }
        OSData::SD.remove(kPendingPath);
    }
    if(!have && RecordValid(pending_record)){ r = pending_record; have = true; }
#else
    if(RecordValid(pending_record)){
        r = pending_record;
        have = true;
    }else if(watchdog_enable_caused_reboot() && crumbs.magic == kCrumbsMagic){
        //ウォッチドッグは再起動の前に何も書けないので、最後のパンくずから組み立てる
        memset(&r, 0, sizeof(r));
        r.kind = (uint32_t)Kind::Watchdog;
        r.uptime_ms = crumbs.uptime_ms;
        r.frame_no = crumbs.frame_no;
        r.phase = crumbs.phase;
        r.in_lua = crumbs.in_lua;
        memcpy(r.scene, crumbs.scene, sizeof(r.scene));
        memcpy(r.lua_app, crumbs.lua_app, sizeof(r.lua_app));
        SealRecord(r);
        have = true;
    }
#endif
    memset(&pending_record, 0, sizeof(pending_record));

    //パンくずをまっさらにして使い始める
    memset(&crumbs, 0, sizeof(crumbs));
    crumbs.phase = 0xFF;
    crumbs.magic = kCrumbsMagic;

    if(!have) return;

    //文章は書き出す間だけ確保する(以前は2KBの静的なバッファを常に持っていた)。起動の直後なので確保できる見込み
    constexpr size_t kTextBytes = 2048;
    char* text = static_cast<char*>(malloc(kTextBytes));
    char path[48] = {};
    const bool written = text && WriteDump("crash", text, FormatRecord(r, text, kTextBytes), path, sizeof(path));
    free(text);
    if(written){
        CopyStr(last_dump_path, sizeof(last_dump_path), path);
        LOG_SYS_FAIL("前回の起動で異常終了しました。ダンプ: %s", path);
    }else{
        LOG_SYS_FAIL("前回の起動で異常終了しました(ダンプは書けませんでした): %s", KindName(r.kind));
    }
}

const char* CrashDumpFunctions::LastDumpPath(){
    return last_dump_path[0] ? last_dump_path : nullptr;
}

void CrashDumpFunctions::PostPendingNotice(){
    if(!last_dump_path[0]) return;
    NotificationFunctions::Content c;
    NotificationFunctions::Sanitize(c.title, "前回、異常終了しました");
    NotificationFunctions::Sanitize(c.body, last_dump_path);
    char target[sizeof(last_dump_path) + 8];
    snprintf(target, sizeof(target), "file:%s", last_dump_path);
    NotificationFunctions::Sanitize(c.app, target);
    c.sound = false;
    NotificationFunctions::Post(c);
}

// ---------------- 落ちたときに捕まえる ----------------

void CrashDumpFunctions::InstallHandlers(){
#if defined(PICOOS_PC)
    #if defined(PICOOS_SD_ROOT_DEFAULT)
    //pc/compat/SdFat.h のSDのルート(PICOOS_SD_ROOTで差し替え済み)
    snprintf(pending_fs_path, sizeof(pending_fs_path), "%s%s", PicoOsSdHost::root.c_str(), kPendingPath);
    #endif
    if(!getenv("PICOOS_NO_CRASH_HANDLER")){
        signal(SIGSEGV, SignalHandler);
        signal(SIGBUS, SignalHandler);
        signal(SIGFPE, SignalHandler);
        signal(SIGILL, SignalHandler);
        signal(SIGABRT, SignalHandler);
    }
#endif
    //実機: isr_hardfault(下)がリンクで差し替わるので何もしない
}

void CrashDumpFunctions::EnableWatchdog(uint32_t ms){
#if defined(PICOOS_PC)
    watchdog_ms = ms;
    watchdog_on = ms > 0;
    #if !defined(__EMSCRIPTEN__)
    last_feed_ms = (uint32_t)millis();
    if(watchdog_on && !watchdog_thread_started.exchange(true)){
        std::thread(WatchdogThread).detach();
    }
    #endif
#else
    if(ms == 0){
        //RP2350のウォッチドッグは一度動かしたら止められない(次の再起動まで)。設定だけ覚える
        if(watchdog_on) LOG_SYS_WARN("ウォッチドッグは再起動するまで止まりません");
        return;
    }
    //ハードウェアの上限(RP2040は約8.3秒、RP2350は約16.7秒)に収める
    if(ms > 8300) ms = 8300;
    if(ms < 1000) ms = 1000;
    watchdog_ms = ms;
    watchdog_enable(ms, true); //デバッガで止めている間は止まる
    watchdog_on = true;
#endif
    LOG_SYS_OK("ウォッチドッグ: %lums", (unsigned long)ms);
}

bool CrashDumpFunctions::WatchdogEnabled(){ return watchdog_on; }

void CrashDumpFunctions::Feed(){
    if(!watchdog_on) return;
#if defined(PICOOS_PC)
    #if !defined(__EMSCRIPTEN__)
    last_feed_ms = (uint32_t)millis();
    #endif
#else
    watchdog_update();
#endif
}

// ---------------- パンくず ----------------

void CrashDumpFunctions::BeginFrame(uint32_t frame_no){
    crumbs.frame_no = frame_no;
    crumbs.uptime_ms = (uint32_t)millis();
}

void CrashDumpFunctions::SetPhase(uint8_t phase){
    crumbs.phase = phase;
}

void CrashDumpFunctions::SetScene(const char* name){
    if(name == last_scene_ptr) return;
    last_scene_ptr = name;
    CopyStr(crumbs.scene, sizeof(crumbs.scene), name);
}

void CrashDumpFunctions::SetLua(bool in_lua, const char* app){
    crumbs.in_lua = in_lua ? 1 : 0;
    if(app && app != last_lua_ptr){
        last_lua_ptr = app;
        CopyStr(crumbs.lua_app, sizeof(crumbs.lua_app), app);
    }
}

// ---------------- Luaのエラー ----------------

bool CrashDumpFunctions::SaveLuaError(const char* app, const char* message, const char* trace){
    if(!OSData::SD_usable) return false;
    if(lua_dumps_this_boot >= kMaxLuaDumpsPerBoot) return false;

    //文章は書き出す間だけ確保する(以前は3KBの静的なバッファを常に持っていた)。
    //メモリ不足で落ちたLuaのエラーのときは確保できないことがあるが、そのときは保存を諦める(ログには出ている)
    constexpr size_t kTextBytes = 3072;
    char* text = static_cast<char*>(malloc(kTextBytes));
    if(!text) return false;
    text[0] = '\0';
    Out o{text, kTextBytes, 0};
    o.add("pico-os Luaエラー\n");
    o.add("アプリ: %s\n", (app && *app) ? app : "(不明)");
    AddClock(o);
    const uint32_t up = (uint32_t)millis();
    o.add("起動からの時間: %lu.%03lu秒\n", (unsigned long)(up / 1000), (unsigned long)(up % 1000));
    o.add("\nメッセージ:\n%s\n", message ? message : "(なし)");
    if(trace && *trace) o.add("\nスタックトレース:\n%s\n", trace);

    char path[48];
    const bool written = WriteDump("lua", text, o.len, path, sizeof(path));
    free(text);
    if(!written) return false;
    lua_dumps_this_boot++;
    LOG_APP_MSG("Luaのエラーを保存しました: %s", path);
    return true;
}

void CrashDumpFunctions::TriggerTestCrash(){
    Record r;
    FillRecordFromCrumbs(r, Kind::Test);
    SealRecord(r);
    StorePendingRecord(r);
}

// ---------------- HardFault(実機) ----------------
#if !defined(PICOOS_PC) && defined(__arm__)
extern "C" void __attribute__((used)) pico_os_hardfault_c(uint32_t* frame, uint32_t exc_return){
    CrashDumpFunctions::Record& r = pending_record;
    CrashDumpFunctions::FillRecordFromCrumbs(r, CrashDumpFunctions::Kind::HardFault);

    const uint32_t addr = (uint32_t)frame;
    //SRAM(0x20000000〜0x20082000)の中を指しているときだけ読む(壊れたSPを読んで二重に落ちないように)
    const bool sane = addr >= 0x20000000u && addr + 8 * 4 <= 0x20082000u && (addr & 3) == 0;
    if(sane){
        r.r0 = frame[0]; r.r1 = frame[1]; r.r2 = frame[2]; r.r3 = frame[3];
        r.r12 = frame[4]; r.lr = frame[5]; r.pc = frame[6]; r.xpsr = frame[7];
        uint32_t n = 0;
        for(; n < (uint32_t)CrashDumpFunctions::kStackWords; n++){
            const uint32_t a = addr + (8 + n) * 4;
            if(a + 4 > 0x20082000u) break;
            r.stack[n] = frame[8 + n];
        }
        r.stack_words = n;
    }
    r.sp = addr;
    r.exc_return = exc_return;
    r.cfsr  = *(volatile uint32_t*)0xE000ED28u;
    r.hfsr  = *(volatile uint32_t*)0xE000ED2Cu;
    r.mmfar = *(volatile uint32_t*)0xE000ED34u;
    r.bfar  = *(volatile uint32_t*)0xE000ED38u;
    CrashDumpFunctions::SealRecord(r);

    watchdog_reboot(0, 0, 0);
    for(;;){}
}

//pico-sdkのcrt0では弱いシンボル(既定はbkpt)。どちらのスタック(MSP/PSP)で落ちたかを見て、例外フレームの先頭を渡す
extern "C" void __attribute__((naked)) isr_hardfault(void){
    __asm volatile(
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "mov r1, lr\n"
        "b pico_os_hardfault_c\n"
    );
}
#endif
