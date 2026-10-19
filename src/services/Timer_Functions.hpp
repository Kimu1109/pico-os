#pragma once
#include <cstdint>

// タイマーとストップウォッチ(ラップ付き)。時計アプリを閉じても動き続けるよう、状態はOS側に持ち
// main.cppのloop()で進める(アラームと同じ考え方。以前はClocksSceneのメンバで、アプリを離れると止まった)。
//
// - 経過は millis() の差で積む(NTP同期で時刻が飛んでもずれない。スリープでloop()が間引かれても進む)
// - タイマーが0になったら Finished にして、ビープ(AlarmFunctions::PlayBeepStep)を kRingMaxMs まで鳴らし、
//   その間 PowerFunctions::KeepAwake() で画面を起こす。時計アプリのタイマーを見ていなければ
//   「止める」の確認ダイアログを出す(画面を切り替えられて消えても、鳴っている間は出し直す)
// - 時計アプリはここの値を毎フレーム読んで表示し、ボタンでここの関数を呼ぶだけ
namespace TimerFunctions {

    enum class State : uint8_t { Idle, Running, Paused, Finished };

    constexpr int kMaxLaps = 30;
    constexpr uint32_t kMaxTimerMs = (23u * 3600u + 59u * 60u + 59u) * 1000u;

    void Setup();
    void Update();                    // loop()から毎フレーム
    void UpdateAt(unsigned long now_ms);

    // ---- タイマー ----
    State TimerState();
    uint32_t TimerSetMs();
    uint32_t TimerLeftMs();
    // 設定値を変える(動いていないときだけ。Idleなら残りも同じにする)
    void TimerSet(uint32_t ms);
    // 開始/一時停止/再開。Finishedならもう一度同じ時間で始める
    void TimerStartPause();
    void TimerReset();
    // 完了音を鳴らしているか(Finishedになってから kRingMaxMs の間)
    bool TimerRinging();
    // 時計アプリがタイマーを見せているか(見せている間は確認ダイアログを出さない)
    void SetTimerShown(bool shown);

    // ---- ストップウォッチ ----
    State StopwatchState();
    uint32_t StopwatchMs();
    void StopwatchStartStop();
    void StopwatchReset();
    // 今の経過をラップとして記録する(動いているときだけ)。溢れたら古いものから捨てる
    bool StopwatchLap();
    int LapCount();
    // i番目(0が最初)のラップを記録したときの通算時間
    uint32_t LapTotalMs(int i);
    // 記録したラップの通し番号(捨てたものも数えた1始まり)
    int LapNumber(int i);
    // ラップが変わるたびに進む(表示の作り直しの目安)
    uint32_t LapRevision();

    // 経過を "MM:SS.cc"(1時間以上は "H:MM:SS")で
    void FormatStopwatch(uint32_t ms, char* out, unsigned cap);
}
