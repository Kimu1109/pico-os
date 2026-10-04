#pragma once
#include <cstdint>

// スリープ(省電力)。操作が一定時間無ければ、画面を暗くする(DisplayFunctionsの自動調光)より
// さらに一段深く休ませる。
//
//   Active → (kIdleTimeoutMs) → Dim(DisplayFunctions) → (sleep-timeout) → Sleep
//
// Sleepに入ると次を行い、タッチ/コントローラーの操作で全て元へ戻す:
//   1. バックライトを消し、液晶パネルもスリープコマンドで休ませる(DisplayFunctions::SetSleeping)
//   2. Wi-Fiチップを積極的な省電力へ(NetworkFunctions::SetLowPower。接続は保つ)
//   3. 何も鳴っていなければI2Sとアンプを止め、2コア目をゆっくり回す(SoundFunctions::SetPowerSave)
//   4. 1コア目のloop()を間引く(IdleWait()。CPUがほぼ寝る。タッチ検出は最大kSleepLoopDelayMsの遅れ)
//
// **スリープへ入らない条件**(どれか1つでもあれば入らない/起きる):
//   - 各画面が毎フレームKeepAwake()を呼んでいる(ゲームボーイ・SSH・チャット・通信中・計測中など、
//     操作が無くても動き続ける画面)。前のフレームに呼ばれたかで判断するので、呼び忘れても
//     「スリープに入る」だけで壊れない(戻し忘れの心配が無い)
//   - 音・曲が鳴っている(SoundFunctions)
//   - Wi-Fiの接続を試みている最中
// **スリープから起こしたタッチは画面へ渡さない**(暗い画面の見えないボタンを押してしまわないよう、
// 指を離すまで無かったことにする)。コントローラーのボタンは渡す。
//
// 設定は /sys/display.cfg の `sleep-timeout = 秒`(0で無効。既定120秒)。SettingsSceneが編集する。
// タイムアウトは最後の操作からの通算(自動調光が切れていてもスリープは別に効く)。
namespace PowerFunctions {

    constexpr unsigned long kDefaultSleepTimeoutMs = 120000;
    // スリープ中の1コア目のloop()の休み(ms)。タッチで起きるまでの最大の遅れになる
    constexpr unsigned long kSleepLoopDelayMs = 30;
    //通常時の1フレームの最短時間(10ms = 100fps以下に抑える)
    constexpr unsigned long kMinFrameMs = 10;

    void Setup();      // display.cfgを読むのでSDより後
    // loop()の頭、タッチ/パッドの更新とDisplayFunctions::Update()の後に呼ぶ
    void Update();
    // loop()の最後に呼ぶ。スリープ中だけ少し休んでCPUを寝かせる
    void IdleWait();

    bool IsSleeping();

    // 0で無効。今だけ変える(display.cfgへは書かない。呼び出し側がPICO_Config::SetValue()する)
    unsigned long GetSleepTimeoutMs();
    void SetSleepTimeoutMs(unsigned long ms);

    namespace detail {
        // KeepAwake()が立てる印。Update()が読んで下ろす
        inline bool keep_awake = false;
    }
    // この画面は操作が無くても動き続けるので、スリープへ入れない(毎フレーム呼ぶ。onUpdate()から)。
    // 定義をここに置いているのは、画面のホストテストにPower_Functions.cppをリンクさせないため
    inline void KeepAwake(){ detail::keep_awake = true; }

    // ===== テスト用(時刻を外から与える) =====
    void SetupAt(unsigned long now_ms);
    void UpdateAt(unsigned long now_ms);
}
