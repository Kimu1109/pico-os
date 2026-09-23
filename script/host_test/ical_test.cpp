// iCalendar(.ics)の読み取り(src/calendar/Ical)のテスト。
//
// 一番効くのは繰り返し(RRULE)の引き当て。RFC 5545 の例をそのまま使って、
// 「何日に出るか」を日付単位で固定する。月表示の印はこれで決まるので、
// ここがずれると「予定があるのに印が付かない/無い日に付く」になる。
//
// もう1つは入力の揺れ(CRLF/LFの混在、行の折り返し、BOM、大文字小文字、
// 1バイトずつ届く場合)で結果が変わらないこと。
#include "calendar/Ical.hpp"
#include "functions/Log_Functions.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ---- モック ----
void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}
static void eq_str(const char* a, const char* e, const char* label){
    const bool ok = (strcmp(a, e) == 0);
    printf("%s %-46s 実測=%-24s 期待=%s\n", ok ? "[ OK ]" : "[FAIL]", label, a, e);
    if(!ok) failures++;
}
static void eq_int(long a, long e, const char* label){
    const bool ok = (a == e);
    printf("%s %-46s 実測=%ld 期待=%ld\n", ok ? "[ OK ]" : "[FAIL]", label, a, e);
    if(!ok) failures++;
}

static int32_t D(int y, int m, int d){ return Ical::DaysFromCivil(y, m, d); }

// ics を丸ごと食わせる。chunk>0 なら chunk バイトずつ
static IcalCalendar cal; // 約20KBあるのでスタックに置かない
static void parse(const std::string& ics, const Ical::Options& opt = Ical::Options{}, size_t chunk = 0){
    cal.clear();
    Ical::Parser p(cal, opt);
    if(chunk == 0){
        p.feed(ics.data(), ics.size());
    }else{
        for(size_t i = 0; i < ics.size(); i += chunk){
            const size_t n = (ics.size() - i < chunk) ? ics.size() - i : chunk;
            p.feed(ics.data() + i, n);
        }
    }
    p.finish();
}

static std::string wrap(const std::string& body){
    return "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//test//EN\r\n" + body + "END:VCALENDAR\r\n";
}
static std::string ev(const std::string& props){
    return "BEGIN:VEVENT\r\n" + props + "END:VEVENT\r\n";
}

// [from, to] の日のうち StartsOn が真になる日を "m/d" で並べる
static std::string starts(const IcalEvent& e, int32_t from, int32_t to){
    std::string s;
    for(int32_t d = from; d <= to; d++){
        if(!Ical::StartsOn(e, d)) continue;
        int y, m, dd;
        Ical::CivilFromDays(d, y, m, dd);
        char buf[16];
        snprintf(buf, sizeof(buf), "%s%d/%d", s.empty() ? "" : " ", m, dd);
        s += buf;
    }
    return s;
}

int main(){
    printf("sizeof(IcalEvent)=%zu sizeof(IcalCalendar)=%zu sizeof(Parser)=%zu\n",
           sizeof(IcalEvent), sizeof(IcalCalendar), sizeof(Ical::Parser));

    // ---- 日付の計算 ----
    {
        eq_int(D(1970, 1, 1), 0, "1970-01-01 = 0");
        eq_int(D(2000, 3, 1), 11017, "2000-03-01");
        eq_int(D(1969, 12, 31), -1, "1969-12-31 = -1");
        int y, m, d;
        Ical::CivilFromDays(D(2026, 9, 23), y, m, d);
        check(y == 2026 && m == 9 && d == 23, "通算日数から年月日へ戻せる");
        eq_int(Ical::Weekday(D(2026, 9, 23)), 3, "2026-09-23は水曜");
        eq_int(Ical::Weekday(D(1969, 12, 28)), 0, "1969-12-28は日曜(負の日数)");
        eq_int(Ical::DaysInMonth(2024, 2), 29, "2024年2月");
        eq_int(Ical::DaysInMonth(1900, 2), 28, "1900年2月");
        eq_int(Ical::DaysInMonth(2000, 2), 29, "2000年2月");
    }

    // ---- 基本: 終日と時刻付き、エスケープ、入れ子の無視 ----
    const std::string basic = wrap(
        "BEGIN:VTIMEZONE\r\nTZID:Asia/Tokyo\r\nBEGIN:STANDARD\r\nDTSTART:19700101T000000\r\n"
        "TZOFFSETFROM:+0900\r\nTZOFFSETTO:+0900\r\nEND:STANDARD\r\nEND:VTIMEZONE\r\n" +
        ev("DTSTART;VALUE=DATE:20260923\r\nDTEND;VALUE=DATE:20260924\r\n"
           "SUMMARY:秋分の日\r\nUID:a@example\r\n") +
        ev("DTSTART;TZID=Asia/Tokyo:20260925T093000\r\nDTEND;TZID=Asia/Tokyo:20260925T103000\r\n"
           "SUMMARY:打ち合わせ\\, 定例\\;A\\nB\\\\C\r\nLOCATION:会議室\r\n"
           "DESCRIPTION:" + std::string(2000, 'x') + "\r\n"
           "BEGIN:VALARM\r\nACTION:DISPLAY\r\nSUMMARY:通知の文\r\nTRIGGER:-PT10M\r\nEND:VALARM\r\n"));
    {
        parse(basic);
        eq_int(cal.count, 2, "2件読める(VTIMEZONEのDTSTARTは拾わない)");
        const IcalEvent& a = cal.events[0];
        eq_str(a.summary.c_str(), "秋分の日", "終日: SUMMARY");
        check(a.start.isAllDay() && a.start.day == D(2026, 9, 23), "終日: 開始日");
        check(Ical::OccursOn(a, D(2026, 9, 23)) && !Ical::OccursOn(a, D(2026, 9, 24)),
              "終日: DTENDは排他的(翌日には出ない)");

        const IcalEvent& b = cal.events[1];
        eq_str(b.summary.c_str(), "打ち合わせ, 定例;A B\\C", "エスケープを戻す(VALARMのSUMMARYで上書きされない)");
        eq_str(b.location.c_str(), "会議室", "LOCATION");
        eq_int(b.start.sec, 9 * 3600 + 30 * 60, "TZID付きは現地時刻のまま");
        eq_int(b.end.sec, 10 * 3600 + 30 * 60, "DTEND");
    }

    // ---- 入力の揺れで結果が変わらない ----
    {
        parse(basic, Ical::Options{}, 1);
        const bool ok1 = cal.count == 2 && strcmp(cal.events[1].summary.c_str(), "打ち合わせ, 定例;A B\\C") == 0;
        check(ok1, "1バイトずつ食わせても同じ");

        // LFだけ・折り返し(日本語の途中で折る)・小文字・BOM
        std::string s = "\xEF\xBB\xBF" "begin:vcalendar\n"
                        "begin:vevent\n"
                        "dtstart:20260101T120000\n"
                        "summary:あいう\n"
                        " えお\n"               // 行頭の空白1つは捨てて前へ繋ぐ
                        "\tかき\n"
                        "end:vevent\n"
                        "end:vcalendar\n";
        // 「い」の途中で折り返す(RFCは75オクテットで折るので文字の途中もあり得る)
        const std::string mid = "SUMMARY:\xE3\x81\x82\xE3\x81\r\n \x84\r\n";
        s += wrap(ev("DTSTART:20260102\r\n" + mid));
        parse(s, Ical::Options{}, 7);
        eq_int(cal.count, 2, "BOM/LF/小文字でも読める");
        eq_str(cal.events[0].summary.c_str(), "あいうえおかき", "折り返しを畳む");
        eq_str(cal.events[1].summary.c_str(), "あい", "文字の途中の折り返しも繋がる");
    }

    // ---- UTCの時刻は現地時刻へずらす ----
    {
        Ical::Options opt;
        opt.utc_offset_sec = 9 * 3600;
        parse(wrap(ev("DTSTART:20260923T150000Z\r\nDTEND:20260923T160000Z\r\nSUMMARY:x\r\n")), opt);
        const IcalEvent& e = cal.events[0];
        check(e.start.day == D(2026, 9, 24) && e.start.sec == 0, "15:00Z は JST で翌日0:00");
        check(Ical::OccursOn(e, D(2026, 9, 24)) && !Ical::OccursOn(e, D(2026, 9, 23)), "日付が繰り上がる");
    }

    // ---- DTEND が無い場合 ----
    {
        parse(wrap(ev("DTSTART:20260910T100000\r\nDURATION:PT1H30M\r\n") +
                   ev("DTSTART;VALUE=DATE:20260911\r\n") +
                   ev("DTSTART:20260912T100000\r\n") +
                   ev("DTSTART;VALUE=DATE:20260913\r\nDURATION:P3D\r\n") +
                   ev("SUMMARY:DTSTART無し\r\n")));
        eq_int(cal.count, 4, "DTSTARTの無い予定は捨てる");
        eq_int(cal.events[0].end.sec, 11 * 3600 + 30 * 60, "DURATION(時刻付き)");
        eq_int(cal.events[1].end.day, D(2026, 9, 12), "終日の既定は1日");
        eq_int(cal.events[2].end.sec, 10 * 3600, "時刻付きの既定は長さ0");
        const IcalEvent& e = cal.events[3];
        check(Ical::OccursOn(e, D(2026, 9, 15)) && !Ical::OccursOn(e, D(2026, 9, 16)), "DURATION(終日3日)");
    }

    // ---- 日をまたぐ時刻付き予定 ----
    {
        parse(wrap(ev("DTSTART:20260930T220000\r\nDTEND:20261001T020000\r\n") +
                   ev("DTSTART:20260930T220000\r\nDTEND:20261001T000000\r\n")));
        check(Ical::OccursOn(cal.events[0], D(2026, 10, 1)), "翌2時まで → 翌日にも出る");
        check(!Ical::OccursOn(cal.events[1], D(2026, 10, 1)), "ちょうど0時に終わる → 翌日には出ない");
    }

    // ---- 繰り返し: 毎週(曜日指定 + COUNT) ----
    {
        parse(wrap(ev("DTSTART:20260921T090000\r\nRRULE:FREQ=WEEKLY;BYDAY=MO,WE,FR;COUNT=5\r\n")));
        eq_str(starts(cal.events[0], D(2026, 9, 1), D(2026, 10, 31)).c_str(),
               "9/21 9/23 9/25 9/28 9/30", "毎週 月水金 5回");
    }
    // RFC 5545 の例: WKSTで結果が変わる
    {
        parse(wrap(ev("DTSTART:19970805T090000\r\nRRULE:FREQ=WEEKLY;INTERVAL=2;COUNT=4;BYDAY=TU,SU;WKST=MO\r\n") +
                   ev("DTSTART:19970805T090000\r\nRRULE:FREQ=WEEKLY;INTERVAL=2;COUNT=4;BYDAY=TU,SU;WKST=SU\r\n")));
        eq_str(starts(cal.events[0], D(1997, 8, 1), D(1997, 9, 30)).c_str(),
               "8/5 8/10 8/19 8/24", "RFC例 WKST=MO");
        eq_str(starts(cal.events[1], D(1997, 8, 1), D(1997, 9, 30)).c_str(),
               "8/5 8/17 8/19 8/31", "RFC例 WKST=SU");
    }
    // BYDAY省略はDTSTARTの曜日、DTSTARTが規則に合わなくても1回目になる
    {
        parse(wrap(ev("DTSTART;VALUE=DATE:20260922\r\nRRULE:FREQ=WEEKLY;UNTIL=20261006\r\n") +
                   ev("DTSTART;VALUE=DATE:20260922\r\nRRULE:FREQ=WEEKLY;BYDAY=MO;COUNT=2\r\n")));
        eq_str(starts(cal.events[0], D(2026, 9, 1), D(2026, 10, 31)).c_str(),
               "9/22 9/29 10/6", "BYDAY省略 + UNTIL(その日を含む)");
        eq_str(starts(cal.events[1], D(2026, 9, 1), D(2026, 10, 31)).c_str(),
               "9/22 9/28", "規則外のDTSTARTも1回目に数える");
    }

    // ---- 毎日 ----
    {
        Ical::Options opt;
        opt.utc_offset_sec = 9 * 3600;
        // UNTILはUTC。JSTで 9/25 08:59 まで → 9/25 09:00 の回は含まない
        parse(wrap(ev("DTSTART:20260922T090000\r\nRRULE:FREQ=DAILY;INTERVAL=1;UNTIL=20260924T235900Z\r\n") +
                   ev("DTSTART:20260922T090000\r\nRRULE:FREQ=DAILY;INTERVAL=3;COUNT=3\r\n")), opt);
        eq_str(starts(cal.events[0], D(2026, 9, 1), D(2026, 9, 30)).c_str(),
               "9/22 9/23 9/24", "毎日 UNTIL(UTC)は時刻まで比べる");
        eq_str(starts(cal.events[1], D(2026, 9, 1), D(2026, 10, 31)).c_str(),
               "9/22 9/25 9/28", "3日おき 3回");
    }

    // ---- 毎月 ----
    {
        parse(wrap(ev("DTSTART;VALUE=DATE:20260131\r\nRRULE:FREQ=MONTHLY;COUNT=4\r\n") +
                   ev("DTSTART;VALUE=DATE:20260914\r\nRRULE:FREQ=MONTHLY;BYDAY=2MO;COUNT=3\r\n") +
                   ev("DTSTART;VALUE=DATE:20260925\r\nRRULE:FREQ=MONTHLY;BYDAY=-1FR\r\n") +
                   ev("DTSTART;VALUE=DATE:20260929\r\nRRULE:FREQ=MONTHLY;BYDAY=5TU;COUNT=2\r\n")));
        eq_str(starts(cal.events[0], D(2026, 1, 1), D(2026, 12, 31)).c_str(),
               "1/31 3/31 5/31 7/31", "31日: 31日の無い月は飛ばし、COUNTにも数えない");
        eq_str(starts(cal.events[1], D(2026, 9, 1), D(2027, 1, 31)).c_str(),
               "9/14 10/12 11/9", "第2月曜 3回");
        eq_str(starts(cal.events[2], D(2026, 9, 1), D(2026, 12, 31)).c_str(),
               "9/25 10/30 11/27 12/25", "最終金曜");
        eq_str(starts(cal.events[3], D(2026, 9, 1), D(2027, 3, 31)).c_str(),
               "9/29 12/29", "第5火曜: 無い月は数えない");
    }

    // ---- 毎年 ----
    {
        parse(wrap(ev("DTSTART;VALUE=DATE:20240229\r\nRRULE:FREQ=YEARLY;COUNT=2\r\n") +
                   ev("DTSTART;VALUE=DATE:20260923\r\nRRULE:FREQ=YEARLY;BYMONTH=9;BYMONTHDAY=23\r\n")));
        check(Ical::StartsOn(cal.events[0], D(2028, 2, 29)) && !Ical::StartsOn(cal.events[0], D(2032, 2, 29)),
              "2/29: うるう年だけ数えて2回");
        check(!Ical::StartsOn(cal.events[0], D(2025, 2, 28)), "2/29: 平年に2/28へずらさない");
        check(cal.events[1].rule.supported && Ical::StartsOn(cal.events[1], D(2030, 9, 23)),
              "DTSTARTと同じBYMONTH/BYMONTHDAYは対応扱い");
    }

    // ---- 例外: EXDATE と 上書き予定 ----
    {
        parse(wrap(
            // 上書き予定が親より先に来る(Googleの書き出しでも順序は保証されない)
            ev("UID:rep@x\r\nRECURRENCE-ID:20260930T090000\r\nDTSTART:20261001T140000\r\n"
               "DTEND:20261001T150000\r\nSUMMARY:移動した回\r\n") +
            ev("UID:rep@x\r\nRECURRENCE-ID:20261007T090000\r\nDTSTART:20261007T090000\r\n"
               "STATUS:CANCELLED\r\n") +
            ev("UID:rep@x\r\nDTSTART:20260923T090000\r\nRRULE:FREQ=WEEKLY\r\n"
               "EXDATE:20261014T090000,20261021T090000\r\nEXDATE:20261028T090000\r\nSUMMARY:週次\r\n")));
        eq_int(cal.count, 2, "取り消された上書き予定は残さない");
        const IcalEvent* master = nullptr;
        for(int i = 0; i < cal.count; i++) if(cal.events[i].rule.freq != IcalRule::Freq::None) master = &cal.events[i];
        check(master != nullptr, "親が読める");
        if(master){
            eq_str(starts(*master, D(2026, 9, 1), D(2026, 11, 10)).c_str(),
                   "9/23 11/4", "EXDATE(複数/複数行)と上書き/取り消しの回が消える");
        }
        eq_str(cal.events[0].summary.c_str(), "移動した回", "上書き予定は単発として残る");
        check(Ical::OccursOn(cal.events[0], D(2026, 10, 1)), "上書き予定は移動先に出る");
    }

    // ---- 対応していない規則は初回だけ ----
    {
        parse(wrap(ev("DTSTART;VALUE=DATE:20260930\r\nRRULE:FREQ=MONTHLY;BYDAY=MO,TU,WE,TH,FR;BYSETPOS=-1\r\n") +
                   ev("DTSTART:20260930T090000\r\nRRULE:FREQ=HOURLY\r\n") +
                   ev("DTSTART;VALUE=DATE:20260930\r\nRRULE:FREQ=MONTHLY;BYMONTHDAY=1\r\n")));
        eq_int(cal.unsupported_rules, 3, "未対応として数える");
        bool ok = true;
        for(int i = 0; i < cal.count; i++){
            ok = ok && !cal.events[i].rule.supported &&
                 starts(cal.events[i], D(2026, 9, 1), D(2027, 12, 31)) == "9/30";
        }
        check(ok && cal.count == 3, "初回だけ表示する");
    }

    // ---- 窓と件数の上限 ----
    {
        Ical::Options opt;
        opt.window_from_day = D(2026, 9, 1);
        opt.window_to_day = D(2026, 12, 1);
        parse(wrap(ev("DTSTART;VALUE=DATE:20200101\r\nSUMMARY:昔\r\n") +
                   ev("DTSTART;VALUE=DATE:20270101\r\nSUMMARY:先\r\n") +
                   ev("DTSTART;VALUE=DATE:20260831\r\nDTEND;VALUE=DATE:20260903\r\nSUMMARY:窓にかかる\r\n") +
                   ev("DTSTART;VALUE=DATE:20200101\r\nRRULE:FREQ=YEARLY;UNTIL=20250101\r\nSUMMARY:終わった繰り返し\r\n") +
                   ev("DTSTART;VALUE=DATE:20200101\r\nRRULE:FREQ=YEARLY\r\nSUMMARY:続く繰り返し\r\n")), opt);
        eq_int(cal.count, 2, "窓の外は読み捨てる");
        eq_str(cal.events[0].summary.c_str(), "窓にかかる", "窓にかかる予定は残る");
        eq_str(cal.events[1].summary.c_str(), "続く繰り返し", "終わりの無い繰り返しは残る");

        std::string many;
        for(int i = 0; i < IcalCalendar::kMaxEvents + 5; i++) many += ev("DTSTART;VALUE=DATE:20260923\r\n");
        parse(wrap(many));
        eq_int(cal.count, IcalCalendar::kMaxEvents, "上限まで詰める");
        eq_int(cal.dropped, 5, "あふれた件数を数える");
    }

    // ---- 長すぎる行 ----
    {
        std::string longsum = "SUMMARY:" + std::string(600, 'a') + "\r\n";
        std::string longstart = "DTSTART;X-PAD=" + std::string(600, 'p') + ":20260923\r\n";
        parse(wrap(ev("DTSTART;VALUE=DATE:20260923\r\n" + longsum) + ev(longstart)));
        eq_int(cal.count, 1, "切れたDTSTARTは使わない");
        eq_int((long)cal.events[0].summary.length(), PICO_STR_L - 1, "長いSUMMARYは切り詰めて使う");
    }

    // ---- その日の予定の並び(一覧の表示順) ----
    {
        parse(wrap(ev("DTSTART:20260923T150000\r\nSUMMARY:午後\r\n") +
                   ev("DTSTART:20260923T090000\r\nSUMMARY:朝\r\n") +
                   ev("DTSTART;VALUE=DATE:20260923\r\nSUMMARY:終日\r\n") +
                   ev("DTSTART:20260922T220000\r\nDTEND:20260923T020000\r\nSUMMARY:前夜から\r\n") +
                   ev("DTSTART:20260924T090000\r\nSUMMARY:翌日\r\n") +
                   ev("DTSTART:20260902T120000\r\nRRULE:FREQ=WEEKLY\r\nSUMMARY:昼の繰り返し\r\n")));
        uint8_t idx[8];
        const int n = Ical::EventsOn(cal, D(2026, 9, 23), idx, 8);
        std::string order;
        for(int i = 0; i < n; i++){
            if(i) order += ",";
            order += cal.events[idx[i]].summary.c_str();
        }
        eq_str(order.c_str(), "終日,前夜から,朝,昼の繰り返し,午後", "終日と続きが先、残りは開始時刻順");
        eq_int(Ical::EventsOn(cal, D(2026, 9, 23), idx, 2), 2, "max_outで打ち切る");
    }

    // ---- SDから読む ----
    {
        HostSd::files["/cal/test.ics"] = basic;
        cal.clear();
        check(Ical::ParseFile("/cal/test.ics", cal, Ical::Options{}), "ParseFileで開ける");
        eq_int(cal.count, 2, "ParseFile: 件数");
        check(!Ical::ParseFile("/cal/none.ics", cal, Ical::Options{}), "無いファイルはfalse");
        check(Ical::ParseFile("/cal/test.ics", cal, Ical::Options{}) && cal.count == 4,
              "clear()しなければ追記される(複数カレンダーの合成)");
    }

    // ---- 壊れた入力で落ちない ----
    {
        parse("END:VEVENT\r\nEND:VCALENDAR\r\nBEGIN:VEVENT\r\nDTSTART:2026\r\nRRULE:FREQ=\r\n:::\r\n;;;\r\n\"\r\n");
        eq_int(cal.count, 0, "壊れた入力は0件(クラッシュしない)");
        parse(wrap(ev("DTSTART:20260231\r\n") + ev("DTSTART:20260923T250000\r\n")));
        eq_int(cal.count, 0, "存在しない日付/時刻は捨てる");
    }

    printf("\n%s (失敗 %d件)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
