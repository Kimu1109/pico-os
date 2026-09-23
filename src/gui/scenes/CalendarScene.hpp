#pragma once

#include "gui/scenes/Scene.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/ScrollList.hpp"
#include "gui/widgets/apps/MonthGrid.hpp"
#include "gui/widgets/dialogs/EventDetailDialog.hpp"
#include "calendar/Ical.hpp"
#include "calendar/Calendar_Sync.hpp"

#include <cstdint>

// カレンダーアプリ(月表示 + 選んだ日の予定一覧)。
//
// SDの /calendar/ 直下にある *.ics を全部読み、1つの IcalCalendar へ重ねて表示する
// (Googleのカレンダーごとの非公開URLを1ファイルずつ置く想定)。
// /calendar/sources.cfg に取得元(iCalのURL)が書いてあれば、開いたときと「更新」を押したときに
// CalendarSync が取ってきて /calendar/<名前>.ics を差し替える(取れなければ前回のものを出す)。
//
// **読むのは表示中の月の格子(前後の月の空きマスを含む6週間)にかかる予定だけ**で、
// 月を移るたびに読み直す。Googleの非公開URLは過去の予定を全部返すので、
// 窓を切らないと IcalCalendar::kMaxEvents がすぐ昔の予定で埋まるため。
//
// **このシーンは約18KBある**(IcalCalendar 約16KB + 取得用の CalendarSync 約2.4KB をメンバに持つ)。MarkdownScene と同じく
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
        Button* sync_button = nullptr;            // 「更新」/「取得中」/「再試行」
        ScrollList* event_list = nullptr;
        EventDetailDialog* detail_dialog = nullptr; // 開いている間だけ

        // 表示中の月と選択中の日。onExit()を跨いで残る(上へPush()して戻ると復元される)
        int view_year = 0;
        int view_month = 0;
        int selected_day = 1;

        // 今日(TimeFunctionsから取る)。NTP同期前は year が 1970 年代なので「分からない」扱い
        int today_year = 0;
        int today_month = 0;
        int today_day = 0;

        IcalCalendar cal;
        MonthGrid::DayDots day_dots[32]; // [日] = 格子に描く点(数とカレンダーごとの色)
        int loaded_files = 0;     // 読めた .ics の数(0なら案内を出す)

        // 読んだ .ics のファイル名(名前順)。IcalEvent::file_index はこの添字で、
        // 色(kCalendarColors)とカレンダー名(詳細画面)と説明文の読み直し先を引く。
        // 名前順に並べ直すのは、SdFatの列挙順が「作った順」で、取得のたびに
        // ファイルを差し替えると入れ替わってしまう(=色が変わる)ため
        constexpr static int kMaxFiles = 8;
        FixedString<PICO_STR_M> file_names[kMaxFiles];
        int file_count = 0;

        // 一覧の行 → cal.events[] の添字(詳細を開くときに使う)
        uint8_t list_events[16] = {};
        int list_count = 0;

        // ---- 取得(sources.cfg に書かれたURLから) ----
        // 取得はフレームをまたいで進む。onExit()で打ち切る(onUpdateが来なくなるため)
        CalendarSync sync;
        int source_count = 0;
        // ランチャから開いたときに1回だけ自動で取りに行く。Push()から戻っただけでは取り直さない
        bool auto_synced = false;
        // 画面を1回描いてから取りに行く(TLSのハンドシェイクで画面が止まる前に、
        // 「取得中」を見せておくため)。onEnter()からのフレーム数
        int frames_since_enter = 0;
        bool last_sync_failed = false;

        // 1日に並べる予定の上限。EventsOn() の out の大きさ
        constexpr static int kMaxEventsPerDay = 16; // list_events の大きさと揃える

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

        // 一覧の index 行目の予定の詳細を開く
        void showDetail(int index);
        // その日の予定の時刻の欄(「終日」「09:30-10:30」等)
        void formatTime(const IcalEvent& ev, int32_t day, FixedString<PICO_STR_M>& out) const;
        // file_index 番目の .ics のSD上のパス
        bool filePath(int file_index, FixedString<PICO_PATH_LEN>& out) const;
        // 複数のカレンダーを重ねているときだけ色分けする(1つなら今までどおりの見た目)
        int8_t colorOf(const IcalEvent& ev) const;

        void startSync();
        void refreshSyncButton();

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
