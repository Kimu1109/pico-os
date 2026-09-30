#pragma once

#include "gui/scenes/Scene.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/ScrollList.hpp"
#include "gui/widgets/TabBar.hpp"
#include "gui/widgets/apps/AnalogClock.hpp"
#include "gui/widgets/apps/DurationPicker.hpp"

#include <cstdint>

class ClocksScene : public Scene {
    public:
        // 画面下部のタブとそのまま対応させる(index == (int)Feature)
        enum class Feature : uint8_t {
            Clock     = 0,
            Timer     = 1,
            Stopwatch = 2,
            Alarm     = 3
        };

        // 時計の表示形式。上部のTabBarのタブ順と対応(index == (int)ClockMode)
        enum class ClockMode : uint8_t {
            Digital = 0,
            Analog  = 1
        };

        // タイマー/ストップウォッチの共通の状態。
        // Finishedはタイマーだけが使う(ストップウォッチには終わりが無い)
        enum class RunState : uint8_t {
            Idle,
            Running,
            Paused,
            Finished
        };

    private:
        Button* back_button = nullptr;
        TabBar* mode_tab    = nullptr;  // デジタル/アナログ(時計のときだけ出す)
        TabBar* feature_tab = nullptr;  // 時計/タイマー/ストップウォッチ(画面下部・常に出す)

        // 時計以外では上部にmode_tabを出さないので、代わりに機能名を置く
        Label<PICO_STR_M>* title_label = nullptr;

        //デジタル表示
        Label<PICO_STR_M>* date_label = nullptr;
        Label<PICO_STR_M>* time_label = nullptr;

        //アナログ表示
        AnalogClock* analog_clock = nullptr;

        //タイマー
        DurationPicker*    timer_picker = nullptr;
        Button*            timer_start  = nullptr; // 開始/一時停止/再開
        Button*            timer_reset  = nullptr;
        Label<PICO_STR_M>* timer_status = nullptr;

        //ストップウォッチ
        Label<PICO_STR_M>* sw_time   = nullptr;
        Label<PICO_STR_M>* sw_status = nullptr;
        Button*            sw_start  = nullptr; // 開始/停止/再開
        Button*            sw_reset  = nullptr;

        //アラーム。鳴らす側はOSの AlarmFunctions が持ち(このシーンを閉じていても鳴る)、
        //ここは4件の一覧と、選んだ1件の編集だけ
        ScrollList*     alarm_list   = nullptr;
        DurationPicker* alarm_picker = nullptr; // 時:分(秒は使わない。0へ戻す)
        Button*         alarm_toggle = nullptr; // オン/オフ
        Button*         alarm_repeat = nullptr; // 1回→毎日→平日→土日
        int  alarm_sel = 0;
        // ▲▼の長押しで続けて変わる間はSDへ書かず、落ち着いてからまとめて1回書く
        bool alarm_dirty = false;
        unsigned long alarm_changed_ms = 0;

        int before_sec  = -1;
        int before_mday = -1;

        // 表示形式・タイマー・ストップウォッチの状態はシーンのメンバなのでonExit()を跨いで残る
        // (このシーンの上へ別のシーンをPush()して戻ってきた時に復元される)。
        // ただしランチャから開き直すとAppFunctions::Launch()がシーンを作り直すため
        // 既定値へ戻る。起動をまたいで覚えるなら Config_Functions で保存する話になる
        Feature   feature = Feature::Clock;
        ClockMode mode    = ClockMode::Digital;

        // ---- タイマーの状態 ----
        // 経過はmillis()の差分で積む。時計(TimeFunctions)は333msごとにしか更新されず、
        // そもそもNTP同期でいきなり飛ぶことがあるので計測には使えない
        RunState timer_state   = RunState::Idle;
        uint32_t timer_set_ms  = 0;  // ▲▼で設定した時間
        uint32_t timer_left_ms = 0;  // 残り時間
        unsigned long timer_last_tick_ms = 0;
        unsigned long timer_blink_ms     = 0;
        bool timer_blink_on = false;
        // 完了音。鳴り始めの時刻と、最後に鳴らした拍(-1=まだ)。kRingMaxMsを過ぎたら黙る(点滅は続く)
        unsigned long timer_ring_start_ms = 0;
        long          timer_ring_step     = -1;

        // ---- ストップウォッチの状態 ----
        RunState sw_state      = RunState::Idle;
        uint32_t sw_elapsed_ms = 0;
        unsigned long sw_last_tick_ms = 0;
        unsigned long sw_last_draw_ms = 0;

        constexpr static int MARGIN = 3;

        // 画面下部の機能切り替えタブ。表示領域の計算はbodyRect()の1箇所へ集約してある
        // 「ストップウォッチ」は1行に収まらずTabBarが2行へ折り返すので、16px x 2行ぶん確保する
        constexpr static int FEATURE_TAB_H   = 38;
        constexpr static int BOTTOM_RESERVED = MARGIN + FEATURE_TAB_H + MARGIN;

        // 1/100秒まで出すが、毎フレーム書き換えるとLabelの再レイアウトが重いので間引く。
        // 20fpsもあれば「速く回っている」ことは十分に伝わる
        constexpr static unsigned long SW_DRAW_INTERVAL_MS = 50;

        // アラームの編集が止まってからSDへ書くまでの間
        constexpr static unsigned long ALARM_SAVE_DELAY_MS = 600;

        // 「時間になりました」の点滅周期
        constexpr static unsigned long BLINK_INTERVAL_MS = 500;

        // 上部の行の高さ。Buttonの箱の高さ(文字の余白と立体ぶんが足される)を
        // onEnter()で実測して入れる。定数で持つとフォントを変えた時にずれる
        int top_row_h = 0;

        // 操作ボタンの行の高さ。こちらも実測値
        int action_row_h = 0;

        // 「戻る」+ タブが並ぶ上部の行と、下部の機能タブを除いた表示領域
        Rect bodyRect() const;

        // feature/modeから全ウィジェットの表示・非表示を決める唯一の場所
        void applyVisibility();

        // 機能を切り替えたときの後始末(表に出した側へ今の値を流し込む)
        void applyFeature();

        // modeに合わせて表示/非表示を切り替え、表示側へ今の時刻を入れ直す
        void applyMode();

        void refresh_time();

        // 毎ティック動かすもの(数字)と、状態が変わったときだけ動かすもの(ボタンの文字・
        // 状態の1行)は分けてある。まとめて毎ティック呼ぶと、文字が同じでも
        // Label/Buttonがdirty登録するぶんだけ画面の合成が走り続けてしまう
        //タイマー
        void refreshTimerDigits();
        void refreshTimerControls();
        void refreshTimer(){ this->refreshTimerDigits(); this->refreshTimerControls(); }
        void updateTimer();
        void onTimerStartPressed();
        void onTimerResetPressed();

        //ストップウォッチ
        void refreshStopwatchDigits();
        void refreshStopwatchControls();
        void refreshStopwatch(){ this->refreshStopwatchDigits(); this->refreshStopwatchControls(); }
        void updateStopwatch();
        void onStopwatchStartPressed();
        void onStopwatchResetPressed();

        //アラーム
        void refreshAlarmList();     // 一覧の4行
        void refreshAlarmControls(); // 選択中の1件をpicker/ボタンへ流し込む
        void selectAlarm(int index);
        void flushAlarm();           // 未保存の編集があれば書く
        void updateAlarm();
        void onAlarmToggled();
        void onAlarmRepeatPressed();
        void onAlarmTimeChanged(uint32_t total_ms);

    public:
        const char* getName() const override { return "Clocks"; }

        void onEnter() override;
        void onUpdate() override;
        void onExit() override;
};
