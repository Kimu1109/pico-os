#pragma once

#include "gui/scenes/Scene.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/TabBar.hpp"
#include "gui/widgets/AnalogClock.hpp"

class ClocksScene : public Scene {
    public:
        // 表示形式。TabBarのタブ順とそのまま対応させる(index == (int)ClockMode)
        enum class ClockMode : uint8_t {
            Digital = 0,
            Analog  = 1
        };

    private:
        Button* back_button = nullptr;
        TabBar* mode_tab    = nullptr;

        //デジタル表示
        Label<PICO_STR_M>* date_label = nullptr;
        Label<PICO_STR_M>* time_label = nullptr;

        //アナログ表示
        AnalogClock* analog_clock = nullptr;

        int before_sec  = -1;
        int before_mday = -1;

        // 表示形式はシーンのメンバなのでonExit()を跨いで残る
        // (このシーンの上へ別のシーンをPush()して戻ってきた時に復元される)。
        // ただしランチャから開き直すとAppFunctions::Launch()がシーンを作り直すため
        // 既定値へ戻る。起動をまたいで覚えるなら Config_Functions で保存する話になる
        ClockMode mode = ClockMode::Digital;

        constexpr static int MARGIN = 3;

        // 画面下部はタイマー機能との切り替えを置く予定なので、
        // 表示領域の計算はこの1箇所に集約してある(下に行を足すならここだけ削る)
        constexpr static int BOTTOM_RESERVED = 0;

        // 上部の行の高さ。Buttonの箱の高さ(文字の余白と立体ぶんが足される)を
        // onEnter()で実測して入れる。定数で持つとフォントを変えた時にずれる
        int top_row_h = 0;

        // 「戻る」+ タブが並ぶ上部の行を除いた、時計本体の表示領域
        Rect bodyRect() const;

        // modeに合わせて表示/非表示を切り替え、表示側へ今の時刻を入れ直す
        void applyMode();

        void refresh_time();

    public:
        const char* getName() const override { return "Clocks"; }

        void onEnter() override;
        void onUpdate() override;
        void onExit() override;
};
