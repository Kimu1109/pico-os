#pragma once

#include "util/Json_Reader.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

#include <cstddef>
#include <cstdint>

// Todoist API v1(https://developer.todoist.com/api/v1/)の応答を読む部品と、日付の計算。
//
// ソケットを持たない(Chat_Proto と同じ考え方)。応答のJSONは JsonReader で流しながら読み、
// タスク1件ごとに必要なキー(id / content / priority / due / parent_id / checked / is_deleted)だけを拾う。
// 1件あたり約800バイトある応答の大半(user_id、added_at、order_key 等)は読み捨てる。
// ホストテストは script/host_test/todoist_proto_test.cpp。
namespace Todoist {

    using TaskId  = FixedString<PICO_STR_S>;    // "6XGgmFVcrG5RRjVr"(16文字)
    using Content = FixedString<PICO_STR_LL>;   // タスク名(日本語で約60文字。長いものは切り詰める)
    using DueText = FixedString<PICO_STR_M>;    // due.string("毎日 9時" 等。繰り返しの表示に使う)
    using Cursor  = FixedString<PICO_STR_L>;    // 次のページの目印(next_cursor)

    // 期限。時刻は全て**現地時刻**へ揃えて持つ(Ical と同じ)
    struct Due {
        bool    has = false;
        bool    has_time = false;   // 時刻つき("2026-10-01T15:00:00")か、日付だけか
        bool    recurring = false;
        int32_t day = 0;            // 1970-01-01 からの通算日数(現地)
        int32_t sec = 0;            // その日の0時からの秒(has_time のときだけ)
        DueText text;
    };

    struct Task {
        TaskId  id;
        Content content;
        Due     due;
        uint8_t priority = 1;       // 1(普通)〜4(最優先)。Todoistの画面の P1 は 4
        bool    subtask = false;    // parent_id がある
    };

    // ---- 日付の計算(Ical と同じ days_from_civil。依存を増やさないよう自前で持つ) ----
    int32_t DaysFromCivil(int year, int month, int day);
    void    CivilFromDays(int32_t days, int& year, int& month, int& day);
    int     Weekday(int32_t days);  // 0=日曜 … 6=土曜

    // due.date を読む: "2026-10-01" / "2026-10-01T15:00:00"(現地) / "2026-10-01T06:00:00Z"・
    // "...00.000000Z"(UTC。utc_offset_sec だけずらす)。読めなければ false
    bool ParseDueDate(const char* s, int32_t utc_offset_sec, Due& out);
    // 期限の時刻をエポック秒(UTC)へ。日付だけの期限は false
    bool DueEpoch(const Due& due, int32_t utc_offset_sec, int64_t& out);

    // 一覧に出す期限の短い表記。today_day < 0(時計が合っていない)なら "10/1 15:00" のように日付で書く。
    //   "今日" "今日 15:00" "明日 9:00" "昨日" "10/3(土)" "2027/1/5"
    void FormatDue(const Due& due, int32_t today_day, char* out, size_t out_size);
    // 期限を過ぎているか(日付だけなら前日まで、時刻つきなら今の時刻を過ぎたら)
    bool IsOverdue(const Due& due, int32_t today_day, int32_t now_sec);

    // 並べ替え: 期限の早い順(期限なしは最後)。同じ日なら日付だけのものが先、次に時刻順、
    // それも同じなら優先度の高い順。それ以外は受け取った順を保つ(安定)
    void SortTasks(Task* tasks, int count);

    // JSONの文字列として書く(前後の " も付ける)。入りきらなければ false
    template<size_t N>
    bool AppendJsonString(FixedString<N>& out, const char* s){
        if(!out.append('"')) return false;
        for(const char* p = s; *p; p++){
            const char c = *p;
            bool ok;
            switch(c){
                case '"':  ok = out.append("\\\""); break;
                case '\\': ok = out.append("\\\\"); break;
                case '\n': ok = out.append("\\n"); break;
                case '\r': ok = out.append("\\r"); break;
                case '\t': ok = out.append("\\t"); break;
                default:
                    if((uint8_t)c < 0x20) ok = out.appendFormat("\\u%04x", (unsigned)(uint8_t)c);
                    else ok = out.append(c);
                    break;
            }
            if(!ok) return false;
        }
        return out.append('"');
    }

    // タスクの一覧({"results":[...], "next_cursor":...})を読む
    class TaskParser : public JsonReader::Handler {
        public:
            // out[start_count..max) へ追記する。完了済み・削除済みは飛ばす
            void begin(Task* out, int max, int start_count, int32_t utc_offset_sec);
            bool feed(const void* data, size_t len){ return reader_.feed(data, len); }
            bool complete() const { return reader_.complete(); }
            const char* error() const { return reader_.error(); }

            int count() const { return count_; }
            // 入りきらずに捨てたタスクがあった
            bool overflowed() const { return overflowed_; }
            // 次のページの目印(無ければ空)
            const Cursor& nextCursor() const { return cursor_; }

            void onBegin(const char* key, bool is_array, int depth) override;
            void onEnd(bool is_array, int depth) override;
            void onValue(const char* key, JsonReader::Type type, const char* text, size_t len,
                         bool truncated, int depth) override;

        private:
            enum class Where : uint8_t { Other, Root, Results, Task, Due };

            JsonReader reader_;
            Task* out_ = nullptr;
            int max_ = 0;
            int count_ = 0;
            bool overflowed_ = false;
            int32_t utc_offset_ = 0;
            Cursor cursor_;

            Where where_[JsonReader::kMaxDepth + 1] = {};
            Task cur_;
            bool cur_skip_ = false;     // 完了済み/削除済み
            bool cur_has_id_ = false;

            void finishTask();
    };

    // 失敗の応答({"error": "...", ...})から理由を拾う。JSONでなければ本文の1行目
    class ErrorParser : public JsonReader::Handler {
        public:
            void begin();
            void feed(const void* data, size_t len);
            const char* message() const;

            void onValue(const char* key, JsonReader::Type type, const char* text, size_t len,
                         bool truncated, int depth) override;

        private:
            JsonReader reader_;
            FixedString<PICO_STR_LL> msg_;
            FixedString<PICO_STR_LL> first_line_;
            bool first_line_done_ = false;
            bool got_error_ = false;
    };
}
