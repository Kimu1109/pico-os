#pragma once

#include <stdint.h>
#include <stddef.h>

// 実機向けの簡易プロファイラ(フレーム時間と、loop()の区間ごとの内訳)。
//
// loop()の頭で BeginFrame()、各処理の直後に Mark(区間) を呼ぶ。直前のMark(またはBeginFrame)から
// 今までの時間をその区間へ足す(区間の「終わり」で呼ぶ形なので、呼び忘れた区間は次の区間に混ざるだけ)。
// IdleWait()の手前で EndWork() を呼ぶと、休んでいる時間を除いた「仕事の時間」も分かる。
//
// 集計は kWindowMs ごとの窓で行い、窓が閉じたら Last() の値が入れ替わる(画面の表示・ログはこれを読む)。
// 直近 kHistory フレームのフレーム時間はグラフ用に輪で持つ。全部固定長で、確保はしない。
//
// 無効の間(既定)は BeginFrame()/Mark() が先頭で戻るだけ。有効にするのは DevToolsFunctions
// (/sys/debug.cfg の perf-overlay / perf-log)。
//
// Mark()はついでにクラッシュダンプの「今どこを処理しているか」(CrashDumpFunctions::SetPhase。
// Mark(s)は区間sの終わりなので、書くのは次の区間)も書く。
// こちらはプロファイラが無効でも書く(ウォッチドッグで再起動したときにどこで固まったか分かるように)。
namespace ProfilerFunctions {
    // loop()の区間。並びは表示・ダンプの名前表(SectionName)と揃えること
    enum class Section : uint8_t {
        Input,      // タッチ・コントローラー・CardKB
        Power,      // 自動調光・スリープの判定
        Scene,      // シーン遷移の適用・打鍵の配布
        Widgets,    // WidgetFunctions::UpdateAll()(シーンのonUpdate()とLuaのloop()を含む)
        Flush,      // PICO_GFX::FlushDirty()(合成と液晶への転送)
        Tasks,      // Task/Log/Time
        Network,    // Wi-Fi
        Services,   // 音・電池・アラーム・通知
        Count
    };

    const char* SectionName(Section s);

    constexpr uint32_t kWindowMs = 500;
    constexpr int kHistory = 60;

    struct Stats {
        uint32_t frames = 0;           // 窓の中のフレーム数
        uint32_t fps_x10 = 0;          // 1秒あたりのフレーム数×10
        uint32_t frame_avg_us = 0;     // フレーム時間(loop()の頭から次の頭まで)の平均
        uint32_t frame_max_us = 0;     // 同・最大
        uint32_t work_avg_us = 0;      // 休み(IdleWait)を除いた時間の平均
        uint32_t section_avg_us[(int)Section::Count] = {};
        uint32_t lua_avg_us = 0;       // Luaの実行時間(1フレームあたり。Widgets等の内数)
    };

    void SetEnabled(bool enabled);
    bool Enabled();

    // 時刻(マイクロ秒)を渡す版。ホストテストはこちらを使う(micros()を進められないため)
    void BeginFrameAt(uint32_t now_us, uint32_t now_ms);
    void MarkAt(Section s, uint32_t now_us);
    void EndWorkAt(uint32_t now_us);

    void BeginFrame();
    void Mark(Section s);
    void EndWork();

    // 直近に閉じた窓の集計。windowSerial()は窓が閉じるたびに増える(表示の書き換えの判断用)
    const Stats& Last();
    uint32_t WindowSerial();

    // 直近のフレーム時間(マイクロ秒)。index 0 が一番古い。count は入っている数(最大 kHistory)
    int History(uint32_t* out, int max);

    // Last()の内容を1行でシリアル/ログへ出す
    void LogReport();

    // ---- Luaの実行時間(LuaEngineが外側のProtectedCall()ごとに足す) ----
    // LuaEngineをこのファイルの.cppへ依存させないため、ヘッダだけで完結させる
    inline uint32_t lua_us_in_frame = 0;
    inline void AddLuaMicros(uint32_t us){ lua_us_in_frame += us; }
}
