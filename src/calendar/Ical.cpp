#include "calendar/Ical.hpp"

#include "OS_Data.hpp"
#include "functions/Log_Functions.hpp"

#include <cstdlib>
#include <cstring>

namespace {

    // 複数日にまたがる繰り返し予定を OccursOn() で引くとき、何日前まで遡って探すか。
    // これより長い繰り返し予定は、始まってから kMaxSpanScan 日を過ぎた日に印が付かない
    constexpr int32_t kMaxSpanScan = 31;

    constexpr int32_t kSecPerDay = 86400;

    char ToUpper(char c){ return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c; }

    // プロパティ名・パラメータ名は大小を区別しない(RFC 5545 3.1)
    bool IEq(const char* a, const char* b){
        while(*a && *b){
            if(ToUpper(*a) != ToUpper(*b)) return false;
            a++; b++;
        }
        return *a == *b;
    }

    // 数字をちょうど n 桁読む。足りなければ false
    bool ReadDigits(const char*& p, int n, int& out){
        int v = 0;
        for(int i = 0; i < n; i++){
            if(p[i] < '0' || p[i] > '9') return false;
            v = v * 10 + (p[i] - '0');
        }
        p += n;
        out = v;
        return true;
    }

    int32_t FloorDiv(int64_t a, int64_t b){
        int64_t q = a / b;
        if((a % b != 0) && ((a < 0) != (b < 0))) q--;
        return (int32_t)q;
    }

    void ShiftSeconds(IcalTime& t, int64_t delta){
        if(t.isAllDay()) return;
        const int64_t total = (int64_t)t.day * kSecPerDay + t.sec + delta;
        t.day = FloorDiv(total, kSecPerDay);
        t.sec = (int32_t)(total - (int64_t)t.day * kSecPerDay);
    }

    // "20260923" / "20260923T090000" / "20260923T000000Z"。
    // force_date(VALUE=DATE)なら時刻部分があっても日付として読む。
    // UTCで書かれていれば utc_offset_sec だけずらして現地時刻にする
    bool ParseTime(const char* v, bool force_date, int32_t utc_offset_sec, IcalTime& out){
        while(*v == ' ') v++;
        const char* p = v;
        int y, mo, d;
        if(!ReadDigits(p, 4, y) || !ReadDigits(p, 2, mo) || !ReadDigits(p, 2, d)) return false;
        if(mo < 1 || mo > 12 || d < 1 || d > Ical::DaysInMonth(y, mo)) return false;

        out.day = Ical::DaysFromCivil(y, mo, d);
        out.sec = -1;
        if(force_date || *p != 'T') return true;

        p++;
        int h, mi, s;
        if(!ReadDigits(p, 2, h) || !ReadDigits(p, 2, mi) || !ReadDigits(p, 2, s)) return false;
        if(h > 23 || mi > 59 || s > 60) return false;
        out.sec = h * 3600 + mi * 60 + (s > 59 ? 59 : s); //うるう秒は丸める

        if(*p == 'Z') ShiftSeconds(out, utc_offset_sec);
        //TZID付き・浮動時刻は現地時刻とみなす(Ical.hpp参照)
        return true;
    }

    // "P1D" / "PT1H30M" / "-P1W" / "P1DT12H" を秒へ。読めなければ -1
    int32_t ParseDuration(const char* v){
        while(*v == ' ') v++;
        bool neg = false;
        if(*v == '+' || *v == '-'){ neg = (*v == '-'); v++; }
        if(*v != 'P') return -1;
        v++;

        int64_t total = 0;
        bool in_time = false;
        bool any = false;
        while(*v){
            if(*v == 'T'){ in_time = true; v++; continue; }
            if(*v < '0' || *v > '9') return -1;
            int64_t n = 0;
            while(*v >= '0' && *v <= '9'){
                n = n * 10 + (*v - '0');
                if(n > 100000) return -1; //数百年ぶんの長さは壊れたデータとみなす
                v++;
            }
            switch(*v){
                case 'W': if(in_time) return -1; total += n * 7 * kSecPerDay; break;
                case 'D': if(in_time) return -1; total += n * kSecPerDay; break;
                case 'H': if(!in_time) return -1; total += n * 3600; break;
                case 'M': if(!in_time) return -1; total += n * 60; break;
                case 'S': if(!in_time) return -1; total += n; break;
                default: return -1;
            }
            any = true;
            v++;
        }
        if(!any || neg) return -1; //負の長さは予定の終わりとして意味を成さない
        if(total > (int64_t)INT32_MAX) return -1;
        return (int32_t)total;
    }

    // "SU".."SA" -> 0..6。読めなければ -1
    int ParseWeekday(const char* p){
        static const char* const kNames[7] = {"SU", "MO", "TU", "WE", "TH", "FR", "SA"};
        for(int i = 0; i < 7; i++){
            if(ToUpper(p[0]) == kNames[i][0] && ToUpper(p[1]) == kNames[i][1] && p[2] == '\0') return i;
        }
        return -1;
    }

    // TEXT値のエスケープを戻す。改行は1行表示なので空白にする
    void Unescape(const char* src, char* dst, size_t dst_size){
        size_t n = 0;
        while(*src && n + 1 < dst_size){
            char c = *src++;
            if(c == '\\' && *src){
                const char e = *src++;
                c = (e == 'n' || e == 'N') ? ' ' : e;
            }
            dst[n++] = c;
        }
        dst[n] = '\0';
    }

    uint32_t HashUid(const char* s){
        uint32_t h = 2166136261u; //FNV-1a
        while(*s){
            h ^= (uint8_t)*s++;
            h *= 16777619u;
        }
        return h == 0 ? 1 : h; //0は「UID無し」の予約値
    }

    int PopCount7(uint8_t m){
        int n = 0;
        for(int i = 0; i < 7; i++) if(m & (1u << i)) n++;
        return n;
    }

    // その予定が(開始日から数えて)何日目までかかるか。0なら開始日だけ
    int32_t SpanDays(const IcalEvent& ev){
        int32_t last = ev.end.day;
        if(ev.end.isAllDay()){
            last = ev.end.day - 1;               //終日のDTENDは翌日を指す(排他的)
        }else if(ev.end.sec == 0 && ev.end.day > ev.start.day){
            last = ev.end.day - 1;               //ちょうど0時に終わる予定は前日まで
        }
        return (last > ev.start.day) ? last - ev.start.day : 0;
    }

    // 月の「第nth weekday」が存在すればその日(1始まり)、無ければ 0
    int NthWeekdayOfMonth(int y, int m, int weekday, int nth){
        const int dim = Ical::DaysInMonth(y, m);
        if(nth > 0){
            const int first_wd = Ical::Weekday(Ical::DaysFromCivil(y, m, 1));
            const int d = 1 + (weekday - first_wd + 7) % 7 + (nth - 1) * 7;
            return (d <= dim) ? d : 0;
        }
        //nth == -1(最終)
        const int last_wd = Ical::Weekday(Ical::DaysFromCivil(y, m, dim));
        return dim - (last_wd - weekday + 7) % 7;
    }

    int WeekdayOfMask(uint8_t mask){
        for(int i = 0; i < 7; i++) if(mask & (1u << i)) return i;
        return -1;
    }

    // 月単位/年単位の繰り返しで、m 番目の周期(0始まり)に回が存在するか
    bool MonthlyPeriodExists(const IcalEvent& ev, int y0, int m0, int d0, int period){
        const int idx = (m0 - 1) + period * ev.rule.interval;
        const int y = y0 + idx / 12;
        const int m = idx % 12 + 1;
        if(ev.rule.byday_nth != 0){
            return NthWeekdayOfMonth(y, m, WeekdayOfMask(ev.rule.byday_mask), ev.rule.byday_nth) != 0;
        }
        return d0 <= Ical::DaysInMonth(y, m);
    }

    bool IsLeap(int y){ return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

    // 規則だけから見た「d が何回目か」(0始まり、開始日以降のみ)。規則に合わなければ -1。
    // 存在しない日付(31日の無い月、2/29の無い年、第5週の無い月)は数えない(RFC 5545 3.3.10)
    int32_t RuleIndex(const IcalEvent& ev, int32_t d){
        const IcalRule& r = ev.rule;
        const int32_t s = ev.start.day;
        if(d < s) return -1;

        switch(r.freq){
            case IcalRule::Freq::Daily: {
                const int32_t diff = d - s;
                if(diff % r.interval != 0) return -1;
                return diff / r.interval;
            }
            case IcalRule::Freq::Weekly: {
                if(!(r.byday_mask & (1u << Ical::Weekday(d)))) return -1;
                auto pos = [&](int32_t x){ return (Ical::Weekday(x) - r.wkst + 7) % 7; };
                const int32_t w0 = s - pos(s);
                const int32_t wd = d - pos(d);
                const int32_t k = (wd - w0) / 7;
                if(k % r.interval != 0) return -1;
                const int32_t j = k / r.interval;

                //曜日の並びを「週の始まりから何日目か」のビットへ並べ替える
                uint8_t by_pos = 0;
                for(int wday = 0; wday < 7; wday++){
                    if(r.byday_mask & (1u << wday)) by_pos |= (uint8_t)(1u << ((wday - r.wkst + 7) % 7));
                }
                auto count_in = [&](int from, int to){ //[from, to) の日数
                    int n = 0;
                    for(int p = from; p < to; p++) if(by_pos & (1u << p)) n++;
                    return n;
                };
                if(j == 0) return count_in(pos(s), pos(d));
                return count_in(pos(s), 7) + (j - 1) * PopCount7(by_pos) + count_in(0, pos(d));
            }
            case IcalRule::Freq::Monthly: {
                int ys, ms, ds, yd, md, dd;
                Ical::CivilFromDays(s, ys, ms, ds);
                Ical::CivilFromDays(d, yd, md, dd);
                const int32_t months = (yd - ys) * 12 + (md - ms);
                if(months % r.interval != 0) return -1;
                if(r.byday_nth != 0){
                    const int wd = WeekdayOfMask(r.byday_mask);
                    if(NthWeekdayOfMonth(yd, md, wd, r.byday_nth) != dd) return -1;
                }else if(dd != ds){
                    return -1;
                }
                const int32_t periods = months / r.interval;
                //どの月にも必ず回がある場合は割り算で済む。そうでなければ欠けた月を数えて引く
                const bool always = (r.byday_nth == 0) ? (ds <= 28) : (r.byday_nth >= 1 && r.byday_nth <= 4) || r.byday_nth == -1;
                if(always || r.count == 0) return periods;
                int32_t idx = 0;
                for(int32_t p = 0; p < periods; p++){
                    if(MonthlyPeriodExists(ev, ys, ms, ds, p)) idx++;
                }
                return idx;
            }
            case IcalRule::Freq::Yearly: {
                int ys, ms, ds, yd, md, dd;
                Ical::CivilFromDays(s, ys, ms, ds);
                Ical::CivilFromDays(d, yd, md, dd);
                if(md != ms || dd != ds) return -1;
                const int years = yd - ys;
                if(years % r.interval != 0) return -1;
                const int32_t periods = years / r.interval;
                if(!(ms == 2 && ds == 29) || r.count == 0) return periods;
                int32_t idx = 0;
                for(int32_t p = 0; p < periods; p++){
                    if(IsLeap(ys + p * r.interval)) idx++;
                }
                return idx;
            }
            default:
                return -1;
        }
    }
}

// ---------------------------------------------------------------- 日付の計算

int32_t Ical::DaysFromCivil(int y, int m, int d){
    y -= (m <= 2);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = y - era * 400;
    const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

void Ical::CivilFromDays(int32_t z, int& y, int& m, int& d){
    z += 719468;
    const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int32_t doe = z - era * 146097;
    const int32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int32_t mp = (5 * doy + 2) / 153;
    d = (int)(doy - (153 * mp + 2) / 5 + 1);
    m = (int)(mp < 10 ? mp + 3 : mp - 9);
    y = (int)(yoe + era * 400 + (m <= 2));
}

int Ical::Weekday(int32_t days){
    //1970-01-01は木曜
    return (int)((days % 7 + 7 + 4) % 7);
}

int Ical::DaysInMonth(int y, int m){
    static const uint8_t kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if(m < 1 || m > 12) return 0;
    return (m == 2 && IsLeap(y)) ? 29 : kDays[m - 1];
}

// ---------------------------------------------------------------- パーサ

Ical::Parser::Parser(IcalCalendar& out, const Options& opt) : out_(out), opt_(opt){}

void Ical::Parser::feed(const char* data, size_t len){
    for(size_t i = 0; i < len; i++) pushByte(data[i]);
}

void Ical::Parser::pushByte(char c){
    if(after_newline_){
        after_newline_ = false;
        //改行の直後が空白なら前の行の続き(折り返し)。その空白1文字は捨てる
        if(c == ' ' || c == '\t') return;
        endLine();
    }
    if(c == '\r') return;
    if(c == '\n'){
        after_newline_ = true;
        return;
    }
    if(line_len_ + 1 < kMaxLineBytes){
        line_[line_len_++] = c;
    }else{
        line_overflow_ = true;
    }
}

void Ical::Parser::endLine(){
    line_[line_len_] = '\0';
    if(line_len_ > 0) handleLine(line_, line_len_);
    line_len_ = 0;
    line_overflow_ = false;
}

void Ical::Parser::finish(){
    after_newline_ = false;
    endLine();

    //上書き予定を親のEXDATEへ畳み込む
    for(int i = 0; i < override_count_; i++){
        const Override& o = overrides_[i];
        for(int e = 0; e < out_.count; e++){
            IcalEvent& ev = out_.events[e];
            if(ev.uid_hash != o.uid || ev.has_recurrence_id || ev.rule.freq == IcalRule::Freq::None) continue;
            if(ev.exdate_count < IcalEvent::kMaxExDates){
                ev.exdates[ev.exdate_count++] = o.day;
            }else{
                override_lost_++;
            }
            break;
        }
    }
    override_count_ = 0;
}

void Ical::Parser::handleLine(char* line, size_t len){
    if(first_line_){
        first_line_ = false;
        //UTF-8のBOM
        if(len >= 3 && (uint8_t)line[0] == 0xEF && (uint8_t)line[1] == 0xBB && (uint8_t)line[2] == 0xBF){
            line += 3;
        }
    }

    //NAME;PARAM=...;PARAM="a:b":VALUE。値の区切りのコロンは引用符の外にある最初のもの
    char* name = line;
    char* params = nullptr;
    char* value = nullptr;
    bool in_quote = false;
    for(char* p = line; *p; p++){
        if(*p == '"'){ in_quote = !in_quote; continue; }
        if(in_quote) continue;
        if(*p == ';' && !params && !value){
            *p = '\0';
            params = p + 1;
        }else if(*p == ':'){
            *p = '\0';
            value = p + 1;
            break;
        }
    }
    if(!value) return;
    if(!params) params = value - 1; //空文字列(直前をNULにしてある)

    if(IEq(name, "BEGIN")){
        if(line_overflow_) return;
        depth_++;
        if(event_depth_ == 0 && IEq(value, "VEVENT")){
            event_depth_ = depth_;
            cur_ = IcalEvent{};
            has_start_ = false;
            has_end_ = false;
            duration_sec_ = -1;
            rule_ = RuleScratch{};
        }
        return;
    }
    if(IEq(name, "END")){
        if(line_overflow_) return;
        if(event_depth_ != 0 && depth_ == event_depth_ && IEq(value, "VEVENT")){
            commitEvent();
            event_depth_ = 0;
        }
        if(depth_ > 0) depth_--;
        return;
    }

    //VEVENTの直下だけを読む(VALARM等の入れ子やVTIMEZONEは読まない)
    if(event_depth_ == 0 || depth_ != event_depth_) return;
    handleEventProperty(name, params, value, line_overflow_);
}

void Ical::Parser::handleEventProperty(const char* name, const char* params, char* value, bool truncated){
    const bool is_text = IEq(name, "SUMMARY") || IEq(name, "LOCATION");
    //切れた行は、表示専用の文字列以外は使わない(日付や規則を途中までで読むと別物になる)
    if(truncated && !is_text) return;

    if(is_text){
        char buf[kMaxLineBytes];
        Unescape(value, buf, sizeof(buf));
        if(IEq(name, "SUMMARY")) cur_.summary.assign(buf);
        else                     cur_.location.assign(buf);
        return;
    }

    //VALUE=DATE があるか(TZIDは現地時刻とみなすので見ない)
    bool value_date = false;
    {
        const char* p = params;
        while(*p){
            const char* seg = p;
            bool q = false;
            while(*p && (q || *p != ';')){ if(*p == '"') q = !q; p++; }
            const size_t n = (size_t)(p - seg);
            if(n == 10){
                char tmp[11];
                memcpy(tmp, seg, 10);
                tmp[10] = '\0';
                if(IEq(tmp, "VALUE=DATE")) value_date = true;
            }
            if(*p == ';') p++;
        }
    }

    const int32_t off = opt_.utc_offset_sec;

    if(IEq(name, "DTSTART")){
        has_start_ = ParseTime(value, value_date, off, cur_.start);
    }else if(IEq(name, "DTEND")){
        has_end_ = ParseTime(value, value_date, off, cur_.end);
    }else if(IEq(name, "DURATION")){
        duration_sec_ = ParseDuration(value);
    }else if(IEq(name, "RECURRENCE-ID")){
        cur_.has_recurrence_id = ParseTime(value, value_date, off, cur_.recurrence_id);
    }else if(IEq(name, "UID")){
        cur_.uid_hash = HashUid(value);
    }else if(IEq(name, "STATUS")){
        cur_.cancelled = IEq(value, "CANCELLED");
    }else if(IEq(name, "EXDATE")){
        //カンマ区切りで複数、行としても複数あり得る
        char* p = value;
        while(p && *p){
            char* comma = strchr(p, ',');
            if(comma) *comma = '\0';
            IcalTime t;
            if(ParseTime(p, value_date, off, t) && cur_.exdate_count < IcalEvent::kMaxExDates){
                cur_.exdates[cur_.exdate_count++] = t.day;
            }
            p = comma ? comma + 1 : nullptr;
        }
    }else if(IEq(name, "RRULE")){
        IcalRule& r = cur_.rule;
        char* p = value;
        while(p && *p){
            char* semi = strchr(p, ';');
            if(semi) *semi = '\0';
            char* eq = strchr(p, '=');
            if(!eq){ rule_.bad = true; p = semi ? semi + 1 : nullptr; continue; }
            *eq = '\0';
            const char* key = p;
            char* val = eq + 1;

            if(IEq(key, "FREQ")){
                if(IEq(val, "DAILY"))        r.freq = IcalRule::Freq::Daily;
                else if(IEq(val, "WEEKLY"))  r.freq = IcalRule::Freq::Weekly;
                else if(IEq(val, "MONTHLY")) r.freq = IcalRule::Freq::Monthly;
                else if(IEq(val, "YEARLY"))  r.freq = IcalRule::Freq::Yearly;
                else rule_.bad = true; //HOURLY等。カレンダーの月表示では意味が薄い
            }else if(IEq(key, "INTERVAL")){
                const long n = strtol(val, nullptr, 10);
                if(n < 1 || n > 65535) rule_.bad = true;
                else r.interval = (uint16_t)n;
            }else if(IEq(key, "COUNT")){
                const long n = strtol(val, nullptr, 10);
                if(n < 1 || n > 65535) rule_.bad = true;
                else r.count = (uint16_t)n;
            }else if(IEq(key, "UNTIL")){
                r.has_until = ParseTime(val, false, off, r.until);
                if(!r.has_until) rule_.bad = true;
            }else if(IEq(key, "WKST")){
                const int wd = ParseWeekday(val);
                if(wd < 0) rule_.bad = true;
                else r.wkst = (uint8_t)wd;
            }else if(IEq(key, "BYDAY")){
                char* q = val;
                while(q && *q){
                    char* c = strchr(q, ',');
                    if(c) *c = '\0';
                    char* wd_str = q;
                    int nth = 0;
                    if(*wd_str == '+' || *wd_str == '-' || (*wd_str >= '0' && *wd_str <= '9')){
                        nth = (int)strtol(wd_str, &wd_str, 10);
                        rule_.byday_has_nth = true;
                        rule_.byday_nth = nth;
                    }
                    const int wd = ParseWeekday(wd_str);
                    if(wd < 0) rule_.bad = true;
                    else r.byday_mask |= (uint8_t)(1u << wd);
                    rule_.byday_count++;
                    q = c ? c + 1 : nullptr;
                }
            }else if(IEq(key, "BYMONTHDAY")){
                if(strchr(val, ',')) rule_.bad = true;
                else rule_.bymonthday = (int)strtol(val, nullptr, 10);
            }else if(IEq(key, "BYMONTH")){
                if(strchr(val, ',')) rule_.bad = true;
                else rule_.bymonth = (int)strtol(val, nullptr, 10);
            }else{
                rule_.bad = true; //BYSETPOS/BYYEARDAY/BYWEEKNO/BYHOUR等
            }
            p = semi ? semi + 1 : nullptr;
        }
    }
    //それ以外(DESCRIPTION/X-*等)は無視する
}

void Ical::Parser::commitEvent(){
    if(!has_start_) return; //DTSTARTの無い予定はどこにも置けない

    //終わりの解決: DTEND > DURATION > 既定(終日なら1日、時刻付きなら長さ0)
    if(!has_end_){
        cur_.end = cur_.start;
        if(duration_sec_ >= 0){
            if(cur_.start.isAllDay()) cur_.end.day += duration_sec_ / kSecPerDay;
            else ShiftSeconds(cur_.end, duration_sec_);
        }else if(cur_.start.isAllDay()){
            cur_.end.day += 1;
        }
    }
    if(cur_.end.day < cur_.start.day ||
       (cur_.end.day == cur_.start.day && !cur_.end.isAllDay() && cur_.end.sec < cur_.start.sec)){
        cur_.end = cur_.start; //終わりが始まりより前の壊れた予定
    }

    //規則の後始末。部品の順序は自由なので、DTSTARTと突き合わせるのは読み終わってから
    IcalRule& r = cur_.rule;
    if(r.freq != IcalRule::Freq::None || rule_.bad){
        bool bad = rule_.bad;
        int ys, ms, ds;
        CivilFromDays(cur_.start.day, ys, ms, ds);
        switch(r.freq){
            case IcalRule::Freq::Daily:
                if(rule_.byday_count > 0 || rule_.bymonthday || rule_.bymonth) bad = true;
                break;
            case IcalRule::Freq::Weekly:
                if(rule_.byday_has_nth || rule_.bymonthday || rule_.bymonth) bad = true;
                if(r.byday_mask == 0) r.byday_mask = (uint8_t)(1u << Weekday(cur_.start.day));
                break;
            case IcalRule::Freq::Monthly:
                if(rule_.bymonth) bad = true;
                if(rule_.byday_count > 0){
                    const int n = rule_.byday_nth;
                    if(rule_.byday_count != 1 || !rule_.byday_has_nth || rule_.bymonthday ||
                       !((n >= 1 && n <= 5) || n == -1)){
                        bad = true;
                    }else{
                        r.byday_nth = (int8_t)n;
                    }
                }else if(rule_.bymonthday && rule_.bymonthday != ds){
                    bad = true;
                }
                break;
            case IcalRule::Freq::Yearly:
                if(rule_.byday_count > 0) bad = true;
                if(rule_.bymonth && rule_.bymonth != ms) bad = true;
                if(rule_.bymonthday && rule_.bymonthday != ds) bad = true;
                break;
            default:
                break;
        }
        if(bad){
            r = IcalRule{};
            r.supported = false;
            out_.unsupported_rules++;
        }
    }

    //上書き予定: 親の該当回を消す控えは、自分が読み捨てられても残す
    if(cur_.has_recurrence_id && cur_.uid_hash != 0){
        if(override_count_ < kMaxOverrides){
            overrides_[override_count_++] = Override{cur_.uid_hash, cur_.recurrence_id.day};
        }else{
            override_lost_++;
        }
    }
    if(cur_.cancelled) return;

    //窓の外の予定は読み捨てる
    const int32_t last = cur_.start.day + SpanDays(cur_);
    if(cur_.start.day >= opt_.window_to_day) return;
    if(r.freq == IcalRule::Freq::None){
        if(last < opt_.window_from_day) return;
    }else if(r.has_until){
        const int32_t last_start = r.until.day;
        if((int64_t)last_start + SpanDays(cur_) < opt_.window_from_day) return;
    }

    if(out_.count >= IcalCalendar::kMaxEvents){
        out_.dropped++;
        return;
    }
    out_.events[out_.count++] = cur_;
}

// ---------------------------------------------------------------- SDから読む

bool Ical::ParseFile(const char* path, IcalCalendar& out, const Options& opt){
    if(!path || path[0] == '\0') return false;

    FsFile f = OSData::SD.open(path, O_RDONLY);
    if(!f) return false;

    Parser parser(out, opt);
    char chunk[256];
    int n = 0;
    while((n = f.read(chunk, sizeof(chunk))) > 0){
        parser.feed(chunk, (size_t)n);
    }
    parser.finish();
    f.close();

    if(out.dropped > 0){
        LOG_APP_WARN("カレンダー: 予定が上限(%d件)を超えたため%d件を読み捨てました: %s",
                     IcalCalendar::kMaxEvents, out.dropped, path);
    }
    if(parser.lostExceptions() > 0){
        LOG_APP_WARN("カレンダー: 繰り返しの例外が上限を超えたため%d件を反映できませんでした: %s",
                     parser.lostExceptions(), path);
    }
    return true;
}

// ---------------------------------------------------------------- 引き当て

bool Ical::StartsOn(const IcalEvent& ev, int32_t day){
    if(day < ev.start.day) return false;

    const IcalRule& r = ev.rule;
    if(r.freq == IcalRule::Freq::None) return day == ev.start.day;

    for(int i = 0; i < ev.exdate_count; i++){
        if(ev.exdates[i] == day) return false;
    }

    //DTSTARTは規則に合っていなくても必ず1回目になる(RFC 5545 3.8.5.3)
    int32_t idx = 0;
    if(day != ev.start.day){
        idx = RuleIndex(ev, day);
        if(idx < 0) return false;
        if(RuleIndex(ev, ev.start.day) != 0) idx++;
    }

    if(r.count > 0 && idx >= r.count) return false;
    if(r.has_until){
        if(day > r.until.day) return false;
        if(day == r.until.day && !r.until.isAllDay() && !ev.start.isAllDay() && ev.start.sec > r.until.sec) return false;
    }
    return true;
}

bool Ical::OccursOn(const IcalEvent& ev, int32_t day){
    const int32_t span = SpanDays(ev);
    if(ev.rule.freq == IcalRule::Freq::None){
        return day >= ev.start.day && day <= ev.start.day + span;
    }
    const int32_t back = (span < kMaxSpanScan) ? span : kMaxSpanScan;
    for(int32_t x = day - back; x <= day; x++){
        if(StartsOn(ev, x)) return true;
    }
    return false;
}
