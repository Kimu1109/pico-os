// 開発者向けの道具のテスト: プロファイラ(ProfilerFunctions)・クラッシュダンプ(CrashDumpFunctions)・
// 設定(DevToolsFunctions、/sys/debug.cfg)。
//
//   プロファイラ  … 区間ごとの時間の割り当て(Markは区間の終わり)・休みを除いた仕事の時間・窓(0.5秒)での集計・
//                   最大フレーム時間・Luaの時間・グラフ用の履歴の順番・無効の間は何もしないこと
//   クラッシュダンプ … 落ちた記録(TriggerTestCrash)を次の起動で /crash/crash_NNNN.txt へ書くこと(パンくず: 画面・
//                   処理していた区間・Luaアプリ)、PCビルドの /sys/crash.pending(HardFaultのレジスタ・CFSRの読み解き)、
//                   壊れた記録は捨てること、通知(タップでダンプを開く)、Luaのエラーの保存と1回の起動の上限
//   設定          … debug.cfgの読み込みと、切り替えたときの反映・保存
#include "functions/Profiler_Functions.hpp"
#include "functions/CrashDump_Functions.hpp"
#include "functions/DevTools_Functions.hpp"
#include "functions/Notification_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "lua/LuaDebugger.hpp"
#include "OS_Data.hpp"
#include "SdFat.h"

#include <cstdio>
#include <cstring>
#include <string>

void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

static bool Contains(const std::string& s, const char* part){ return s.find(part) != std::string::npos; }

int main(){
    setvbuf(stdout, nullptr, _IONBF, 0);
    OSData::SD_usable = true;
    using PF = ProfilerFunctions::Section;

    // =================== プロファイラ ===================
    {
        ProfilerFunctions::SetEnabled(false);
        const uint32_t serial0 = ProfilerFunctions::WindowSerial();
        ProfilerFunctions::BeginFrameAt(0, 0);
        ProfilerFunctions::MarkAt(PF::Input, 100);
        ProfilerFunctions::BeginFrameAt(1000, 1000);
        check(ProfilerFunctions::WindowSerial() == serial0, "プロファイラ: 無効の間は集計しない");

        ProfilerFunctions::SetEnabled(true);
        // 1フレーム = 10ms(10000us)。入力1ms、画面更新3ms、描画4ms、仕事は8.5msで終わり、残りは休み
        uint32_t t = 0, ms = 0;
        for(int i = 0; i < 60; i++){
            ProfilerFunctions::BeginFrameAt(t, ms);
            ProfilerFunctions::MarkAt(PF::Input, t + 1000);
            ProfilerFunctions::lua_us_in_frame = 0;
            ProfilerFunctions::AddLuaMicros(500);
            ProfilerFunctions::MarkAt(PF::Widgets, t + 4000);
            ProfilerFunctions::MarkAt(PF::Flush, t + 8000);
            ProfilerFunctions::MarkAt(PF::Services, t + 8500);
            ProfilerFunctions::EndWorkAt(t + 8500);
            //1フレームだけ遅い(30ms)
            const uint32_t len = (i == 20) ? 30000 : 10000;
            t += len;
            ms += len / 1000;
        }
        ProfilerFunctions::BeginFrameAt(t, ms);
        const ProfilerFunctions::Stats& s = ProfilerFunctions::Last();
        printf("       frames=%u avg=%u max=%u work=%u fps_x10=%u lua=%u\n",
            (unsigned)s.frames, (unsigned)s.frame_avg_us, (unsigned)s.frame_max_us, (unsigned)s.work_avg_us,
            (unsigned)s.fps_x10, (unsigned)s.lua_avg_us);
        check(ProfilerFunctions::WindowSerial() > serial0, "プロファイラ: 0.5秒で窓が閉じる");
        check(s.frames > 0 && s.section_avg_us[(int)PF::Input] == 1000, "プロファイラ: 入力の区間は1ms");
        check(s.section_avg_us[(int)PF::Widgets] == 3000, "プロファイラ: 画面更新の区間は3ms(前のMarkからの差)");
        check(s.section_avg_us[(int)PF::Flush] == 4000, "プロファイラ: 描画の区間は4ms");
        check(s.work_avg_us == 8500, "プロファイラ: 仕事の時間は休みを除いた8.5ms");
        check(s.lua_avg_us == 500, "プロファイラ: Luaの時間(1フレームあたり)");
        check(s.frame_max_us == 30000, "プロファイラ: 最大フレーム時間は遅かったフレーム(30ms)");
        check(s.frame_avg_us >= 10000 && s.frame_avg_us <= 11000, "プロファイラ: 平均フレーム時間");
        check(s.fps_x10 >= 900 && s.fps_x10 <= 1000, "プロファイラ: fps(約100)");

        uint32_t hist[ProfilerFunctions::kHistory];
        const int n = ProfilerFunctions::History(hist, ProfilerFunctions::kHistory);
        check(n == ProfilerFunctions::kHistory, "プロファイラ: 履歴は60フレームぶん");
        // 有効にしてから60フレーム閉じたので、古い順に i=0..59。i=20 のフレームが30ms
        check(n == 60 && hist[20] == 30000 && hist[19] == 10000 && hist[59] == 10000, "プロファイラ: 履歴は古い順");
        check(strcmp(ProfilerFunctions::SectionName(PF::Flush), "描画") == 0, "プロファイラ: 区間の名前");
        ProfilerFunctions::SetEnabled(false);
    }

    // =================== クラッシュダンプ ===================
    {
        HostSd::files.clear();
        NotificationFunctions::SetupAt(0);

        // 記録が無ければ何も書かない
        CrashDumpFunctions::Setup();
        check(CrashDumpFunctions::LastDumpPath() == nullptr, "ダンプ: 記録が無ければ書かない");

        // パンくずを積んで「落ちた」ことにする
        CrashDumpFunctions::BeginFrame(1234);
        CrashDumpFunctions::SetScene("LuaScene");
        CrashDumpFunctions::SetLua(true, "/lua/apps/テトリス");
        ProfilerFunctions::Mark(PF::Scene); // シーンの区間が終わった = 今は画面更新
        CrashDumpFunctions::TriggerTestCrash();

        // 次の起動
        CrashDumpFunctions::Setup();
        const char* path = CrashDumpFunctions::LastDumpPath();
        check(path && strcmp(path, "/crash/crash_0001.txt") == 0, "ダンプ: 次の起動で /crash/crash_0001.txt へ書く");
        const std::string text = path ? HostSd::files[path] : "";
        printf("------\n%s------\n", text.c_str());
        check(Contains(text, "LuaScene"), "ダンプ: 画面の名前");
        check(Contains(text, "処理していたところ: 画面更新"), "ダンプ: 処理していた区間");
        check(Contains(text, "/lua/apps/テトリス") && Contains(text, "実行中だった"), "ダンプ: Luaアプリと実行中だったこと");
        check(Contains(text, "フレーム: 1234"), "ダンプ: フレームの番号");

        // 通知(タップでダンプを開く)
        const int before = NotificationFunctions::HistoryCount();
        CrashDumpFunctions::PostPendingNotice();
        check(NotificationFunctions::HistoryCount() == before + 1, "ダンプ: 前回のクラッシュを通知する");
        const NotificationFunctions::Entry* e = NotificationFunctions::HistoryAt(0);
        check(e && strcmp(e->content.app.c_str(), "file:/crash/crash_0001.txt") == 0, "ダンプ: 通知のタップでダンプを開く");

        // 同じ記録を2回書かない
        CrashDumpFunctions::Setup();
        check(CrashDumpFunctions::LastDumpPath() == nullptr, "ダンプ: 書いた記録は消える(次の起動では書かない)");

        // PCビルドの /sys/crash.pending(HardFault相当の記録をファイルで渡す)
        CrashDumpFunctions::Record r;
        CrashDumpFunctions::FillRecordFromCrumbs(r, CrashDumpFunctions::Kind::HardFault);
        r.pc = 0x10001234; r.lr = 0x10005679; r.sp = 0x20040000;
        r.cfsr = (1u << 25) | (1u << 1);
        r.stack_words = 2; r.stack[0] = 0xdeadbeef; r.stack[1] = 0x10002000;
        CrashDumpFunctions::SealRecord(r);
        HostSd::files[CrashDumpFunctions::kPendingPath] = std::string((const char*)&r, sizeof(r));
        CrashDumpFunctions::Setup();
        path = CrashDumpFunctions::LastDumpPath();
        const std::string text2 = path ? HostSd::files[path] : "";
        check(path && strcmp(path, "/crash/crash_0002.txt") == 0, "ダンプ: crash.pendingから2つ目を書く");
        check(Contains(text2, "pc =0x10001234") && Contains(text2, "lr =0x10005679"), "ダンプ: レジスタ");
        check(Contains(text2, "0除算") && Contains(text2, "DACCVIOL"), "ダンプ: CFSRを読み解く");
        check(Contains(text2, "deadbeef") && Contains(text2, "addr2line"), "ダンプ: スタックと解析のしかた");
        check(HostSd::files.count(CrashDumpFunctions::kPendingPath) == 0, "ダンプ: crash.pendingは消す");

        // 壊れた記録は捨てる
        r.pc = 1; // checksumと合わなくなる
        HostSd::files[CrashDumpFunctions::kPendingPath] = std::string((const char*)&r, sizeof(r));
        CrashDumpFunctions::Setup();
        check(CrashDumpFunctions::LastDumpPath() == nullptr && HostSd::files.count("/crash/crash_0003.txt") == 0,
              "ダンプ: 壊れた記録は書かない");
        HostSd::files[CrashDumpFunctions::kPendingPath] = "short";
        CrashDumpFunctions::Setup();
        check(CrashDumpFunctions::LastDumpPath() == nullptr, "ダンプ: 短すぎる記録も書かない");

        // Luaのエラー
        check(CrashDumpFunctions::SaveLuaError("/lua/apps/x", "main.lua:3: boom", "  main.lua:3 f\n  main.lua:9 (メイン)"),
              "Luaのエラー: 保存できる");
        const std::string lua = HostSd::files["/crash/lua_0003.txt"];
        check(Contains(lua, "アプリ: /lua/apps/x") && Contains(lua, "main.lua:3: boom") && Contains(lua, "main.lua:9 (メイン)"),
              "Luaのエラー: アプリ・メッセージ・トレース");
        int saved = 1;
        while(CrashDumpFunctions::SaveLuaError("/lua/apps/x", "again", "")) saved++;
        check(saved == CrashDumpFunctions::kMaxLuaDumpsPerBoot, "Luaのエラー: 1回の起動で保存するのは上限まで");

        // SDが無ければ何もしない
        OSData::SD_usable = false;
        CrashDumpFunctions::TriggerTestCrash();
        CrashDumpFunctions::Setup();
        check(CrashDumpFunctions::LastDumpPath() == nullptr, "ダンプ: SDが無ければ書かない(落ちない)");
        OSData::SD_usable = true;
    }

    // =================== 設定(debug.cfg) ===================
    {
        HostSd::files["/sys/debug.cfg"] = "perf-overlay = true\nlua-debugger = true\nwatchdog-ms = 99999\n";
        bool overlay_visible = false;
        static bool* vis = &overlay_visible;
        DevToolsFunctions::perf_overlay_visibility = [](bool v){ *vis = v; };
        DevToolsFunctions::Setup();
        check(DevToolsFunctions::PerfOverlay() && overlay_visible, "設定: perf-overlay = true で表示が出る");
        check(ProfilerFunctions::Enabled(), "設定: 表示が出ている間はプロファイラが動く");
        check(DevToolsFunctions::LuaDebuggerEnabled() && LuaDebugger::GlobalEnabled(), "設定: lua-debugger = true でデバッガが有効");
        check(!DevToolsFunctions::Watchdog(), "設定: watchdog は既定で切れている");

        DevToolsFunctions::SetPerfOverlay(false);
        check(!overlay_visible && !ProfilerFunctions::Enabled(), "設定: 切ると表示もプロファイラも止まる");
        printf("       debug.cfg: %s\n", HostSd::files["/sys/debug.cfg"].c_str());
        check(Contains(HostSd::files["/sys/debug.cfg"], "perf-overlay") && Contains(HostSd::files["/sys/debug.cfg"], "false"),
              "設定: 切り替えはdebug.cfgへ書く");
        DevToolsFunctions::SetPerfLog(true);
        check(ProfilerFunctions::Enabled() && !overlay_visible, "設定: perf-logだけでもプロファイラは動く(表示は出ない)");
        DevToolsFunctions::SetLuaDebugger(false);
        check(!LuaDebugger::GlobalEnabled(), "設定: デバッガを切る");

        // 書き戻したものを読み直す
        DevToolsFunctions::Setup();
        check(!DevToolsFunctions::PerfOverlay() && DevToolsFunctions::PerfLog() && !DevToolsFunctions::LuaDebuggerEnabled(),
              "設定: 保存した値で起動する");
        DevToolsFunctions::SetPerfLog(false);
    }

    printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
