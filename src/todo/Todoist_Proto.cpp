#include "todo/Todoist_Proto.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
    // 数字をちょうど n 桁読む
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

    int64_t FloorDiv(int64_t a, int64_t b){
        return (a >= 0) ? a / b : -((-a + b - 1) / b);
    }

    bool KeyIs(const char* key, const char* name){
        return key && strcmp(key, name) == 0;
    }
}

// ---------------------------------------------------------------------------
// 日付
// ---------------------------------------------------------------------------

int32_t Todoist::DaysFromCivil(int y, int m, int d){
    y -= (m <= 2) ? 1 : 0;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (unsigned)(m + (m > 2 ? -3 : 9)) + 2) / 5 + (unsigned)d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int32_t)doe - 719468;
}

void Todoist::CivilFromDays(int32_t z, int& y, int& m, int& d){
    z += 719468;
    const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = (int)(doy - (153 * mp + 2) / 5 + 1);
    m = (int)(mp < 10 ? mp + 3 : mp - 9);
    y = (int)yoe + era * 400 + (m <= 2 ? 1 : 0);
}

int Todoist::Weekday(int32_t days){
    //1970-01-01 は木曜
    const int w = (int)((days + 4) % 7);
    return w < 0 ? w + 7 : w;
}

bool Todoist::ParseDueDate(const char* s, int32_t utc_offset_sec, Due& out){
    if(!s) return false;
    const char* p = s;
    int y, mo, d;
    if(!ReadDigits(p, 4, y) || *p++ != '-' || !ReadDigits(p, 2, mo) || *p++ != '-' || !ReadDigits(p, 2, d)) return false;
    if(mo < 1 || mo > 12 || d < 1 || d > 31) return false;

    int32_t day = DaysFromCivil(y, mo, d);
    if(*p == '\0'){
        out.has = true;
        out.has_time = false;
        out.day = day;
        out.sec = 0;
        return true;
    }
    if(*p++ != 'T') return false;

    int h, mi, sec = 0;
    if(!ReadDigits(p, 2, h) || *p++ != ':' || !ReadDigits(p, 2, mi)) return false;
    if(*p == ':'){
        p++;
        if(!ReadDigits(p, 2, sec)) return false;
    }
    if(*p == '.'){
        p++;
        while(*p >= '0' && *p <= '9') p++;
    }
    if(h > 23 || mi > 59 || sec > 60) return false;
    if(sec > 59) sec = 59;

    int64_t total = (int64_t)day * 86400 + h * 3600 + mi * 60 + sec;
    if(*p == 'Z'){
        p++;
        total += utc_offset_sec;
    }else if(*p == '+' || *p == '-'){
        //"+09:00" のように時差が書かれていれば、いったんUTCへ戻してから現地へ
        const int sign = (*p == '+') ? 1 : -1;
        p++;
        int oh, om = 0;
        if(!ReadDigits(p, 2, oh)) return false;
        if(*p == ':') p++;
        if(*p >= '0' && *p <= '9' && !ReadDigits(p, 2, om)) return false;
        total += utc_offset_sec - sign * (oh * 3600 + om * 60);
    }
    //それ以外(末尾に何も無い)は浮動時刻 = 現地時刻
    if(*p != '\0') return false;

    out.has = true;
    out.has_time = true;
    out.day = (int32_t)FloorDiv(total, 86400);
    out.sec = (int32_t)(total - (int64_t)out.day * 86400);
    return true;
}

bool Todoist::DueEpoch(const Due& due, int32_t utc_offset_sec, int64_t& out){
    if(!due.has || !due.has_time) return false;
    out = (int64_t)due.day * 86400 + due.sec - utc_offset_sec;
    return true;
}

void Todoist::FormatDue(const Due& due, int32_t today_day, char* out, size_t out_size){
    if(!out || out_size == 0) return;
    out[0] = '\0';
    if(!due.has) return;

    static const char* const kWeek[7] = {"日", "月", "火", "水", "木", "金", "土"};
    int y, m, d;
    CivilFromDays(due.day, y, m, d);

    int n;
    if(today_day < 0){
        n = snprintf(out, out_size, "%d/%d/%d", y, m, d);
    }else{
        const int32_t diff = due.day - today_day;
        int ty, tm, td;
        CivilFromDays(today_day, ty, tm, td);
        if(diff == 0)       n = snprintf(out, out_size, "今日");
        else if(diff == 1)  n = snprintf(out, out_size, "明日");
        else if(diff == -1) n = snprintf(out, out_size, "昨日");
        else if(y == ty)    n = snprintf(out, out_size, "%d/%d(%s)", m, d, kWeek[Weekday(due.day)]);
        else                n = snprintf(out, out_size, "%d/%d/%d", y, m, d);
    }
    if(n < 0 || (size_t)n >= out_size || !due.has_time) return;
    snprintf(out + n, out_size - n, " %d:%02d", due.sec / 3600, (due.sec / 60) % 60);
}

bool Todoist::IsOverdue(const Due& due, int32_t today_day, int32_t now_sec){
    if(!due.has || today_day < 0) return false;
    if(due.day < today_day) return true;
    if(due.day > today_day) return false;
    return due.has_time && due.sec < now_sec;
}

namespace {
    // a を b より前に並べるか
    bool Before(const Todoist::Task& a, const Todoist::Task& b){
        if(a.due.has != b.due.has) return a.due.has;
        if(a.due.has){
            if(a.due.day != b.due.day) return a.due.day < b.due.day;
            if(a.due.has_time != b.due.has_time) return !a.due.has_time;
            if(a.due.has_time && a.due.sec != b.due.sec) return a.due.sec < b.due.sec;
        }
        return a.priority > b.priority;
    }
}

void Todoist::SortTasks(Task* tasks, int count){
    //件数は高々数十なので挿入ソート(安定)
    for(int i = 1; i < count; i++){
        if(!Before(tasks[i], tasks[i - 1])) continue;
        Task t = tasks[i];
        int j = i;
        while(j > 0 && Before(t, tasks[j - 1])){
            tasks[j] = tasks[j - 1];
            j--;
        }
        tasks[j] = t;
    }
}

// ---------------------------------------------------------------------------
// タスクの読み取り
// ---------------------------------------------------------------------------

void Todoist::TaskParser::begin(Task* out, int max, int start_count, int32_t utc_offset_sec){
    out_ = out;
    max_ = max;
    count_ = start_count;
    overflowed_ = false;
    utc_offset_ = utc_offset_sec;
    cursor_.clear();
    for(auto& w : where_) w = Where::Other;
    cur_ = Task();
    cur_skip_ = false;
    cur_has_id_ = false;
    reader_.reset(this);
}

void Todoist::TaskParser::onBegin(const char* key, bool is_array, int depth){
    if(depth > JsonReader::kMaxDepth) return;
    const Where parent = where_[depth - 1];
    Where w = Where::Other;

    if(depth == 1){
        if(!is_array) w = Where::Root;
    }else if(parent == Where::Root && is_array && KeyIs(key, "results")){
        w = Where::Results;
    }else if(parent == Where::Results && !is_array){
        w = Where::Task;
    }else if(parent == Where::Task && !is_array && KeyIs(key, "due")){
        w = Where::Due;
    }

    if(w == Where::Task){
        cur_ = Task();
        cur_skip_ = false;
        cur_has_id_ = false;
    }
    where_[depth] = w;
}

void Todoist::TaskParser::onEnd(bool, int depth){
    if(depth > JsonReader::kMaxDepth) return;
    if(where_[depth] == Where::Task) finishTask();
    where_[depth] = Where::Other;
}

void Todoist::TaskParser::finishTask(){
    if(!cur_has_id_ || cur_skip_) return;
    if(count_ >= max_){
        overflowed_ = true;
        return;
    }
    out_[count_++] = cur_;
}

void Todoist::TaskParser::onValue(const char* key, JsonReader::Type type, const char* text, size_t,
                                  bool truncated, int depth){
    if(depth > JsonReader::kMaxDepth) return;
    using T = JsonReader::Type;
    switch(where_[depth]){
        case Where::Root:
            if(KeyIs(key, "next_cursor")){
                //切り詰まった目印は別物を指すので使わない(続きは取らない)
                if(type == T::String && !truncated) cursor_.assign(text);
                else cursor_.clear();
            }
            break;

        case Where::Task:
            if(KeyIs(key, "id")){
                if((type == T::String || type == T::Number) && !truncated && *text){
                    cur_has_id_ = cur_.id.assign(text) ? true : false;
                }
            }else if(KeyIs(key, "content")){
                if(type == T::String) cur_.content.assign(text);
            }else if(KeyIs(key, "priority")){
                if(type == T::Number){
                    int p = atoi(text);
                    if(p < 1) p = 1;
                    if(p > 4) p = 4;
                    cur_.priority = (uint8_t)p;
                }
            }else if(KeyIs(key, "checked") || KeyIs(key, "is_deleted")){
                if(type == T::True) cur_skip_ = true;
            }else if(KeyIs(key, "parent_id")){
                cur_.subtask = (type == T::String && *text) || type == T::Number;
            }
            break;

        case Where::Due:
            if(KeyIs(key, "date")){
                if(type == T::String && !ParseDueDate(text, utc_offset_, cur_.due)) cur_.due.has = false;
            }else if(KeyIs(key, "string")){
                if(type == T::String) cur_.due.text.assign(text);
            }else if(KeyIs(key, "is_recurring")){
                cur_.due.recurring = (type == T::True);
            }
            break;

        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// 失敗の応答
// ---------------------------------------------------------------------------

void Todoist::ErrorParser::begin(){
    msg_.clear();
    first_line_.clear();
    first_line_done_ = false;
    got_error_ = false;
    reader_.reset(this);
}

void Todoist::ErrorParser::feed(const void* data, size_t len){
    const char* p = (const char*)data;
    for(size_t i = 0; i < len && !first_line_done_; i++){
        if(p[i] == '\n' || p[i] == '\r'){
            first_line_done_ = true;
            break;
        }
        if(!first_line_.append(p[i])) first_line_done_ = true;
    }
    if(!reader_.failed()) reader_.feed(data, len);
}

const char* Todoist::ErrorParser::message() const {
    if(got_error_) return msg_.c_str();
    //JSONでない本文(素のテキスト)なら1行目。JSONとして読めかけて理由が無ければ空
    if(!reader_.failed()) return "";
    return first_line_.c_str();
}

void Todoist::ErrorParser::onValue(const char* key, JsonReader::Type type, const char* text, size_t,
                                   bool, int depth){
    if(depth == 1 && type == JsonReader::Type::String && !got_error_ && KeyIs(key, "error")){
        msg_.assign(text);
        got_error_ = true;
    }
}
