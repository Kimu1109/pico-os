#pragma once

#include "util/FixedString.hpp"
#include "consts.hpp"

#include <cstddef>
#include <cstdint>

// iCalendar(RFC 5545、いわゆる .ics)の読み取り。
//
// カレンダーアプリの第1段で、「SD上の .ics を読んで予定の配列にする」ところだけを受け持つ。
// Google Calendarの「iCal形式の非公開URL」が返すものもこれで読める(取得は別の段の仕事で、
// ここが相手にするのは常に**SDへ落ちた後のファイル**か、手元にあるバイト列だけ)。
//
// 設計方針:
//   - **ヒープを使わない。** 予定は固定長配列(`IcalCalendar::kMaxEvents`)へ詰め、
//     あふれた分は捨てて件数だけ数える(`dropped`)。
//   - **`Parser::feed()`はバイト列を好きな切れ目で受け取れる**(`HttpResponse`と同じ増分方式)。
//     行の折り返し(次の行が空白で始まれば続き)もここで畳むので、SDから256Bずつ読んでも、
//     1バイトずつ食わせても結果は同じ。ホストテストはSD無しで全経路を通せる。
//   - **時刻は全て「端末の現地時刻」へ揃えて持つ。** `...Z`(UTC)は`Options::utc_offset_sec`で
//     ずらし、`TZID=`付きの値と浮動時刻(Zも TZID も無い)はそのまま現地時刻として扱う。
//     VTIMEZONEは解釈しない(TZ定義を全部持つのは割に合わない)。**TZIDが端末と違う
//     タイムゾーンを指す予定は時刻がずれる**が、手元のカレンダーを見る用途ではまず起きない。
//     サマータイムも考えない(UTCからのずれは1つの定数)。
//   - **繰り返し(RRULE)は一部だけ対応する。** FREQ=DAILY/WEEKLY/MONTHLY/YEARLY +
//     INTERVAL/COUNT/UNTIL/BYDAY/WKST と、DTSTARTと食い違わないBYMONTHDAY/BYMONTH。
//     それ以外の部品(BYSETPOS等)を含むものは`rule.supported=false`になり、
//     **初回の1件だけを表示する**(黙って間違った日に出すより、1件だけの方がまし)。
//   - 繰り返しの例外は EXDATE と、RECURRENCE-ID付きの上書き予定(Googleで「この予定のみ」
//     変更したもの)の2つ。後者は`finish()`で親(同じUID)のEXDATEへ畳み込み、上書き側は
//     普通の単発予定として残す。UIDは文字列で持たず32bitハッシュだけを持つ。
//   - 読むプロパティは SUMMARY/LOCATION/DTSTART/DTEND/DURATION/RRULE/EXDATE/
//     RECURRENCE-ID/UID/STATUS だけ。DESCRIPTION等は読み飛ばす(前方互換)。
//     VEVENTの中に入れ子になった部品(VALARM等)の中身も読まない
//     (VALARMにもSUMMARYがあり、拾うと予定の名前が通知文で上書きされるため)。

// 日付は「1970-01-01からの通算日数」、時刻は「その日の0時からの秒」で持つ。
// 月表示で「この日に予定があるか」を引くのが主な使い道なので、日単位の比較が
// 整数1つで済む形にした(time_tだと毎回86400で割ることになる)。
struct IcalTime {
    int32_t day = 0;     // 1970-01-01 = 0
    int32_t sec = -1;    // その日の0時からの秒。-1なら日付だけ(終日)

    bool isAllDay() const { return sec < 0; }
};

// 繰り返しの規則。`freq == None`なら単発の予定
struct IcalRule {
    enum class Freq : uint8_t { None, Daily, Weekly, Monthly, Yearly };

    Freq     freq = Freq::None;
    bool     supported = true;  // falseなら初回だけを表示する
    uint16_t interval = 1;
    uint16_t count = 0;         // 0なら回数の制限なし
    bool     has_until = false;
    IcalTime until;             // 最後の回の開始がこれ以前(終日ならこの日以前)
    uint8_t  byday_mask = 0;    // bit0=日曜 … bit6=土曜(WEEKLYの曜日指定)
    int8_t   byday_nth = 0;     // MONTHLYの「第n何曜日」。-1は最終。0なら日付で繰り返す
    uint8_t  wkst = 1;          // 週の始まり(0=日曜)。既定は月曜(RFC 5545)
};

struct IcalEvent {
    // 1件あたりの例外(EXDATE+上書き予定)の上限。あふれた分は元の回も表示される
    static constexpr int kMaxExDates = 8;

    FixedString<PICO_STR_L> summary;   // 表示専用なので切り詰まってよい
    FixedString<PICO_STR_M> location;
    IcalTime start;
    IcalTime end;                      // 排他的(終日なら翌日、時刻付きならその時刻ちょうど)
    IcalRule rule;
    uint32_t uid_hash = 0;             // 0 = UID無し
    bool     has_recurrence_id = false;
    IcalTime recurrence_id;            // 上書き予定なら、どの回を差し替えたか
    bool     cancelled = false;
    uint8_t  exdate_count = 0;
    int32_t  exdates[kMaxExDates];     // 除外する回の「日」。対応する繰り返しは1日1回までなので日で足りる
                                       // 読み込みの窓の近くにあるものだけを持つ(Parser参照)

    // どこから読んだか。詳細(DESCRIPTION)は持たず、見るときに ReadDescription() で読み直す
    uint8_t  file_index = 0;           // Options::file_index をそのまま写す(呼び出し側の数え方)
    uint16_t ordinal = 0;              // そのファイルの中で何番目のVEVENTか(0始まり、読み捨てた分も数える)
};

struct IcalCalendar {
    // 1件あたり約250B(ホストでsizeof 248B)なので、これで約16KB。シーンのメンバとして持つ想定
    static constexpr int kMaxEvents = 64;

    IcalEvent events[kMaxEvents];
    int count = 0;
    int dropped = 0;           // 上限に当たって捨てた件数
    int unsupported_rules = 0; // 初回だけ表示にした繰り返しの数(ログ用)

    void clear(){ count = 0; dropped = 0; unsupported_rules = 0; }
};

namespace Ical {

    struct Options {
        // UTC(...Z)で書かれた時刻をずらす量。日本なら 9*3600
        int32_t utc_offset_sec = 0;
        // この日付範囲 [from_day, to_day) に一度もかからない予定は読み捨てる。
        // Googleの非公開URLは**過去の予定を全部**返してくるので、窓を切らないと
        // kMaxEvents がすぐ昔の予定で埋まる。繰り返しは UNTIL で判断できるものだけ捨てる
        int32_t window_from_day = INT32_MIN;
        int32_t window_to_day   = INT32_MAX;
        // 読んだ予定の IcalEvent::file_index に入れる値(複数の .ics を重ねるときの目印)
        uint8_t file_index = 0;
    };

    // 詳細画面で見せる説明文(DESCRIPTION)の上限。1論理行の上限(Parser::kMaxLineBytes)で先に切れる
    using Description = FixedString<PICO_STR_512B>;

    // ---- 日付の計算(Howard Hinnantのdays_from_civil。グレゴリオ暦) ----
    int32_t DaysFromCivil(int year, int month, int day);
    void    CivilFromDays(int32_t days, int& year, int& month, int& day);
    int     Weekday(int32_t days);            // 0=日曜 … 6=土曜
    int     DaysInMonth(int year, int month);

    // バイト列を受け取って IcalCalendar へ**追記**する(clear()しない)。
    // 複数の .ics(Googleのカレンダーごとの非公開URL等)を1つの IcalCalendar へ
    // 続けて読めるようにするため。読み直すときは呼び出し側で clear() すること。
    // 行バッファ等で約1.1KBあるので、スタックに置くなら深い呼び出しの中は避ける
    class Parser {
    public:
        // 1論理行(折り返しを畳んだ後)の上限。超えた行は捨てる
        // (SUMMARY/LOCATIONだけは切り詰めて使う。表示専用なので)。
        // DESCRIPTIONは長くなりがちだが読まないので困らない
        static constexpr size_t kMaxLineBytes = 512;

        Parser(IcalCalendar& out, const Options& opt);
        // 予定を集めず、ordinal 番目のVEVENTの説明文だけを拾う(ReadDescription()用)
        Parser(const Options& opt, uint16_t ordinal, Description& description_out);

        void feed(const char* data, size_t len);
        // 最後の行を処理し、上書き予定を親へ畳み込む。必ず最後に1回呼ぶこと
        void finish();

        // 繰り返しの例外(EXDATE/上書き予定)が上限に当たって反映できなかった数
        int lostExceptions() const { return override_lost_; }
        // 説明文を拾う読み方で、目当てのVEVENTを読み終えた(以降は読まなくてよい)
        bool captureDone() const { return capture_done_; }

        // 上書き予定(RECURRENCE-ID)の控えを finish() まで貯めておく数。
        // 上書き予定そのものが窓の外で読み捨てられても、親の回は消さなければならないため別に持つ
        static constexpr int kMaxOverrides = 32;

    private:
        void pushByte(char c);
        void endLine();
        void handleLine(char* line, size_t len);
        void handleEventProperty(const char* name, const char* params, char* value, bool truncated);
        void commitEvent();
        // 窓の外の例外(EXDATE/上書き予定)は覚えない。Googleは何年分もの例外を全部書いてくるので、
        // 覚えると kMaxExDates がすぐ昔の分で埋まり、窓の中の例外が効かなくなる
        bool exceptionNearWindow(int32_t day) const;

        IcalCalendar* out_ = nullptr;   // nullptrなら予定を集めない(説明文を拾うだけ)
        Options opt_;

        uint16_t vevent_count_ = 0;     // これまでに始まったVEVENTの数(ordinalの採番)
        Description* capture_ = nullptr;
        uint16_t capture_ordinal_ = 0;
        bool capture_done_ = false;

        char   line_[kMaxLineBytes];
        size_t line_len_ = 0;
        bool   line_overflow_ = false;
        bool   after_newline_ = false; // 直前が改行(=次の1文字で「続きか新しい行か」が決まる)
        bool   first_line_ = true;     // 先頭のBOMを読み飛ばすため

        int  depth_ = 0;               // BEGIN/ENDの入れ子の深さ
        int  event_depth_ = 0;         // VEVENTに入った時点の深さ(0なら外)
        bool has_start_ = false;
        bool has_end_ = false;
        int32_t duration_sec_ = -1;    // DURATIONがあれば(DTENDより後に解決する)
        IcalEvent cur_;

        // RRULEの生の部品。部品の順序は自由なので、DTSTARTと突き合わせるのは commitEvent()
        struct RuleScratch {
            int  byday_count = 0;
            bool byday_has_nth = false;
            int  byday_nth = 0;
            int  bymonthday = 0;
            int  bymonth = 0;
            bool bad = false;
        };
        RuleScratch rule_;

        struct Override {
            uint32_t uid;
            int32_t  day;
        };
        Override overrides_[kMaxOverrides];
        int override_count_ = 0;
        int override_lost_ = 0;
    };

    // SD上の .ics を読んで out へ追記する。開けなければ false(中身が壊れていても読めた分は返す)
    bool ParseFile(const char* path, IcalCalendar& out, const Options& opt);

    // その日に始まる回があるか(繰り返しとEXDATEを考慮)
    bool StartsOn(const IcalEvent& ev, int32_t day);
    // その日にかかっているか(複数日にまたがる予定も含む)。月表示の印はこちら
    bool OccursOn(const IcalEvent& ev, int32_t day);

    // path の ordinal 番目のVEVENTの説明文(DESCRIPTION)を読む。エスケープは戻し、改行は改行のまま。
    // 見つからない/説明が無ければ false(out は空)
    bool ReadDescription(const char* path, uint16_t ordinal, Description& out);

    // その日にかかっている予定の添字(cal.events[])を out へ並べ、件数を返す。
    // 並びは「終日と前日からの続き」が先、残りは開始時刻順(同時刻は読んだ順)。
    // max_out を超えた分は数えない
    int EventsOn(const IcalCalendar& cal, int32_t day, uint8_t* out, int max_out);
}
