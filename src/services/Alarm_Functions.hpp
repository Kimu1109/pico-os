#pragma once
#include <cstdint>
#include <ctime>

// アラーム。時計アプリを閉じていても鳴らすため、シーンではなくOS側(main.cppのloop())で見張る。
//
// 設定は /sys/alarm.cfg(無くてよい)。1行1件で `alarm1 = 07:30,daily,on`
//   時刻 "HH:MM" , 繰り返し once|daily|weekdays|weekends , on|off
// 時計アプリの「アラーム」タブから編集でき、変えるたびにSetValue()で1件だけ書き換える。
//
// 鳴らし方: 時刻になったら(NTP同期済みのときだけ)
//   1. 確認ダイアログ(止める / 5分後)を出す。画面を切り替えられて消えても、鳴っている間は出し直す
//   2. ビープ音を繰り返す(アンプが刺さっていなければ音だけ出ない)
//   3. 毎フレームPowerFunctions::KeepAwake()を呼び、スリープ中でも画面を起こす
// 何もしなくてもkRingMaxMsで止まる。「1回だけ」のアラームは鳴った時点でoffにして保存する。
namespace AlarmFunctions {

    constexpr int kMaxAlarms = 4;

    enum class Repeat : uint8_t {
        Once = 0,   // 1回だけ(鳴ったらoff)
        Daily,      // 毎日
        Weekdays,   // 月〜金
        Weekends    // 土日
    };
    constexpr int kRepeatCount = 4;

    struct Alarm {
        uint8_t hour    = 7;
        uint8_t minute  = 0;
        Repeat  repeat  = Repeat::Once;
        bool    enabled = false;
    };

    constexpr unsigned long kRingMaxMs   = 60000;     // 鳴らし続ける上限
    constexpr unsigned long kSnoozeMs    = 5UL * 60UL * 1000UL;
    constexpr unsigned long kBeepStepMs  = 250;       // 4拍で1周期(3拍鳴らして1拍休む)
    // NTP同期前(1970年付近)は時刻が当てにならないので鳴らさない
    constexpr int           kMinValidYear = 2020;

    void Setup();   // alarm.cfgを読むのでSDより後
    void Update();  // loop()から毎フレーム。TimeFunctions::Update()より後

    const Alarm& Get(int index);
    // 内容を差し替える。persist=trueなら alarm.cfg へも書く。範囲外はfalse。
    // ▲▼の長押しのように続けて変わる場面では persist=false で流し、落ち着いてから Save() する
    // (SDへの書き込みは1回数十msかかる)
    bool Set(int index, const Alarm& alarm, bool persist = true);
    bool Save(int index);

    bool IsRinging();
    // 鳴っている(または5分後を待っている)アラームを止める
    void Dismiss();

    // 鳴らす音(ビープ)の1拍ぶん。step は鳴らし始めからの拍の通し番号(kBeepStepMsごとに1増える。
    // 同じstepで何度呼んでも1回分だけ鳴らすのは呼び出し側の仕事)。タイマーの完了音も同じ音を使う
    void PlayBeepStep(long step);

    // 一覧用の表記
    const char* RepeatToStr(Repeat r);   // "1回" "毎日" "平日" "土日"
    // 繰り返しが今日(wday: 0=日〜6=土)にあたるか
    bool RepeatMatches(Repeat r, int wday);

    // ===== テスト用(時刻を外から与える) =====
    void SetupAt(unsigned long now_ms);
    void UpdateAt(unsigned long now_ms, const struct tm& now);
}
