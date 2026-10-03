#pragma once

#include <stdint.h>

// 開発者向けの道具(プロファイラの表示・Luaデバッガ・ウォッチドッグ)の設定の窓口。
// 置き場所は /sys/debug.cfg(無くてよい。全部既定で切れている):
//   perf-overlay = true|false   フレーム時間を画面の右下に出す(PerfOverlay)
//   perf-log     = true|false   0.5秒ごとの集計を5秒に1回シリアルへ出す
//   lua-debugger = true|false   Luaデバッガ(ブレークポイント・ステップ実行・エラーで止める)
//   watchdog     = true|false   loop()が watchdog-ms 戻らなければ再起動し、次の起動でダンプを書く
//   watchdog-ms  = 1000〜8300   既定8000
// 設定アプリの「その他」タブから切り替えられる(SetXxx()がその場で反映してdebug.cfgへ書く)。
namespace DevToolsFunctions {
    // SD・ログ・CrashDumpFunctions::Setup()より後で呼ぶ
    void Setup();
    // 毎フレーム(perf-logの出力)
    void Update();

    bool PerfOverlay();
    bool PerfLog();
    bool LuaDebuggerEnabled();
    bool Watchdog();

    void SetPerfOverlay(bool on);
    void SetPerfLog(bool on);
    void SetLuaDebugger(bool on);
    // ウォッチドッグは実機では一度動かすと止められないので、切るのは次の起動から
    void SetWatchdog(bool on);

    // PerfOverlayの表示を出し入れする係(main.cppが生成したウィジェットを登録する)
    inline void (*perf_overlay_visibility)(bool visible) = nullptr;
}
