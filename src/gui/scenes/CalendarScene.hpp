#pragma once

#include "gui/scenes/Scene.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/ScrollList.hpp"
#include "gui/widgets/apps/MonthGrid.hpp"
#include "calendar/Ical.hpp"

#include <cstdint>

// カレンダーアプリ(月表示 + 選んだ日の予定一覧)。
//
// SDの /calendar/ 直下にある *.ics を全部読み、1つの IcalCalendar へ重ねて表示する
// (Googleのカレンダーごとの非公開URLを1ファイルずつ置く想定)。
// 取得(サーバ経由でiCal URLを引く)は次の段で、今は置かれたファイルを読むだけ。
//
// **読むのは表示中の月の格子(前後の月の空きマスを含む6週間)にかかる予定だけ**で、
// 月を移るたびに読み直す。Googleの非公開URLは過去の予定を全部返すので、
// 窓を切らないと IcalCalendar::kMaxEvents がすぐ昔の予定で埋まるため。
//
// **このシーンは約16KBある**(IcalCalendar をメンバに持つ)。MarkdownScene と同じく
// 「シーン本体は数十バイト」の例外で、上へ別のシーンをPush()している間も乗り続ける。
class CalendarScene : public Scene {
    private:
        Button* back_button  = nullptr;
        Button* prev_button  = nullptr;
        Button* next_button  = nullptr;
        Button* today_button = nullptr;
        Label<PICO_STR_S>* title_label = nullptr; // 「2026年9月」
        MonthGrid* grid = nullptr;
        Label<PICO_STR_M>* day_label = nullptr;   // 「9月23日(水) 2件」/ 読めなかった理由
        ScrollList* event_list = nullptr;

        // 表示中の月と選択中の日。onExit()を跨いで残る(上へPush()して戻ると復元される)
        int view_year = 0;
        int view_month = 0;
        int selected_day = 1;

        // 今日(TimeFunctionsから取る)。NTP同期前は year が 1970 年代なので「分からない」扱い
        int today_year = 0;
        int today_month = 0;
        int today_day = 0;

        IcalCalendar cal;
        uint8_t counts[32] = {};  // [日] = その日にかかる予定の件数
        int loaded_files = 0;     // 読めた .ics の数(0なら案内を出す)

        // 1日に並べる予定の上限。EventsOn() の out の大きさ
        constexpr static int kMaxEventsPerDay = 16;

        constexpr static int MARGIN = 3;

        // TimeFunctions の現在時刻から today_* を取り直す。変わったら true
        bool refreshToday();

        // 表示中の月の予定を /calendar/ から読み直し、格子の件数を入れ直す
        void reload();

        // 月を delta ヶ月動かして読み直す
        void moveMonth(int delta);
        void goToday();

        // タイトル・格子・一覧を今の状態へ合わせる
        void refreshView();
        void refreshDayList();

    public:
        const char* getName() const override { return "Calendar"; }

        void onEnter() override;
        void onUpdate() override;
        void onExit() override;

        // 端末のタイムゾーン(TZ環境変数)でのUTCからのずれ(秒)。
        // .ics の ...Z の時刻を現地時刻へ直すのに使う。newlibには tm_gmtoff が無いので、
        // localtime と gmtime の差から求める
        static int32_t LocalUtcOffsetSec();
};
