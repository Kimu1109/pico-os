// Todoist の応答の読み取り(todo/Todoist_Proto)と、その下の JSON の読み取り(util/Json_Reader)のテスト。run.sh から呼ぶ。
//
// 通信は run_net.sh の todoist_net_test の担当で、ここではソケット抜きで
//   - JsonReader: 入れ子・エスケープ(\uXXXX、サロゲートペア)・長い文字列の切り詰め(UTF-8の途中で切らない)・
//     1バイトずつ食わせても同じ結果・誤りの検出
//   - TaskParser: Todoist API v1 の一覧から必要なキーだけ拾う、完了済み/削除済みを飛ばす、上限、next_cursor
//   - 期限の読み取り(日付だけ/浮動時刻/UTC)と表記、期限切れ、並べ替え
//   - 失敗の応答から理由を拾う、JSONの文字列の書き出し
// を見る。
#include "todo/Todoist_Proto.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}
static void eq_int(long a, long e, const char* label){
    const bool ok = (a == e);
    printf("%s %-46s 実測=%ld 期待=%ld\n", ok ? "[ OK ]" : "[FAIL]", label, a, e);
    if(!ok) failures++;
}
static void eq_str(const char* a, const char* e, const char* label){
    const bool ok = strcmp(a, e) == 0;
    printf("%s %-46s 実測=\"%s\"\n", ok ? "[ OK ]" : "[FAIL]", label, a);
    if(!ok) failures++;
}

// ---- JsonReader ----

// 受け取った出来事を1行ずつ文字列にして並べる
struct Recorder : JsonReader::Handler {
    std::vector<std::string> ev;
    void onBegin(const char* key, bool is_array, int depth) override {
        ev.push_back(std::string(is_array ? "[" : "{") + (key ? key : "-") + "@" + std::to_string(depth));
    }
    void onEnd(bool is_array, int depth) override {
        ev.push_back(std::string(is_array ? "]" : "}") + "@" + std::to_string(depth));
    }
    void onValue(const char* key, JsonReader::Type type, const char* text, size_t len, bool truncated, int depth) override {
        static const char* kNames[] = {"s", "n", "t", "f", "null"};
        ev.push_back(std::string(key ? key : "-") + "=" + kNames[(int)type] + ":" + std::string(text, len)
                     + (truncated ? "(切)" : "") + "@" + std::to_string(depth));
    }
    std::string joined() const {
        std::string s;
        for(auto& e : ev){ if(!s.empty()) s += " "; s += e; }
        return s;
    }
};

static std::string runJson(const std::string& text, bool bytewise, bool* ok = nullptr, bool* complete = nullptr){
    Recorder r;
    JsonReader jr;
    jr.reset(&r);
    bool good = true;
    if(bytewise){
        for(char c : text) good = jr.feed(&c, 1) && good;
    }else{
        good = jr.feed(text.data(), text.size());
    }
    if(ok) *ok = good;
    if(complete) *complete = jr.complete();
    return r.joined();
}

static void testJson(){
    printf("\n---- JsonReader ----\n");
    const std::string doc =
        " {\"a\": 1, \"b\": [true, false, null, -2.5e3, \"x\"], \"c\": {\"d\": \"e\\\"\\\\\\/\\n\"}, \"e\": [], \"f\": {}} ";
    bool ok = false, complete = false;
    const std::string all = runJson(doc, false, &ok, &complete);
    check(ok && complete, "入れ子の文書を最後まで読める");
    eq_str(all.c_str(),
           "{-@1 a=n:1@1 [b@2 -=t:true@2 -=f:false@2 -=null:null@2 -=n:-2.5e3@2 -=s:x@2 ]@2 "
           "{c@2 d=s:e\"\\/\n@2 }@2 [e@2 ]@2 {f@2 }@2 }@1",
           "出来事の並び");
    check(runJson(doc, true) == all, "1バイトずつ食わせても同じ結果");

    //\u と サロゲートペア(🍣 = U+1F363)、対になっていない上位サロゲート
    eq_str(runJson("{\"k\":\"\\u3042\\uD83C\\uDF63\\uD800x\"}", true).c_str(),
           "{-@1 k=s:あ🍣\xEF\xBF\xBDx@1 }@1", "\\uXXXX とサロゲートペア");
    eq_str(runJson("[\"\\u0000\"]", false).c_str(), "[-@1 -=s:\xEF\xBF\xBD@1 ]@1", "\\u0000 は U+FFFD にする");

    //長い文字列: 上限で切り、UTF-8の文字の途中で切らない
    {
        std::string longs = "{\"k\":\"a";
        for(int i = 0; i < 200; i++) longs += "あ";
        longs += "\",\"n\":2}";
        Recorder r;
        JsonReader jr;
        jr.reset(&r);
        check(jr.feed(longs.data(), longs.size()) && jr.complete(), "長い文字列があっても最後まで読める");
        const std::string& v = r.ev.size() > 1 ? r.ev[1] : std::string();
        check(v.find("(切)") != std::string::npos, "切り詰めたことが分かる");
        //"k=s:" の後ろ、"(切)" の前が値。"a" + "あ"x n で、バイト数は 1 + 3n <= 255
        const size_t start = 4, end = v.find("(切)");
        const std::string val = v.substr(start, end - start);
        check(val.size() <= JsonReader::kValueBytes - 1 && (val.size() - 1) % 3 == 0, "文字の途中で切らない");
        check(r.ev.size() >= 3 && r.ev[2] == "n=n:2@1", "続くキーも読める");
    }

    //誤り
    bool bad_ok = true;
    runJson("{\"a\" 1}", false, &bad_ok);
    check(!bad_ok, "':' が無ければ誤り");
    runJson("[1,]", false, &bad_ok);
    check(!bad_ok, "末尾の ',' は誤り");
    runJson("{\"a\":1]", false, &bad_ok);
    check(!bad_ok, "括弧の対応が違えば誤り");
    runJson("{\"a\":tru}", false, &bad_ok);
    check(!bad_ok, "知らない値は誤り");
    runJson("{} x", false, &bad_ok);
    check(!bad_ok, "後ろに余計なものがあれば誤り");
    {
        std::string deep;
        for(int i = 0; i < JsonReader::kMaxDepth + 1; i++) deep += "[";
        runJson(deep, false, &bad_ok);
        check(!bad_ok, "入れ子が深すぎれば誤り");
    }
    bool part_complete = true;
    runJson("{\"a\":[1,2", false, &bad_ok, &part_complete);
    check(bad_ok && !part_complete, "途中で終わったら誤りではないが complete でない");
}

// ---- 期限 ----

static void testDue(){
    printf("\n---- 期限 ----\n");
    const int32_t jst = 9 * 3600;
    const int32_t d20261001 = Todoist::DaysFromCivil(2026, 10, 1);
    int y, m, d;
    Todoist::CivilFromDays(d20261001, y, m, d);
    check(y == 2026 && m == 10 && d == 1, "DaysFromCivil と CivilFromDays が往復する");
    eq_int(Todoist::Weekday(d20261001), 4, "2026-10-01 は木曜");

    Todoist::Due due;
    check(Todoist::ParseDueDate("2026-10-01", jst, due) && due.has && !due.has_time && due.day == d20261001,
          "日付だけ");
    check(Todoist::ParseDueDate("2026-10-01T15:30:00", jst, due) && due.has_time && due.day == d20261001
          && due.sec == 15 * 3600 + 30 * 60, "浮動時刻は現地時刻のまま");
    check(Todoist::ParseDueDate("2026-10-01T20:00:00.000000Z", jst, due) && due.day == d20261001 + 1
          && due.sec == 5 * 3600, "UTC(小数秒つき)は現地へずらす(日をまたぐ)");
    check(Todoist::ParseDueDate("2026-10-01T15:00:00+09:00", 0, due) && due.day == d20261001
          && due.sec == 6 * 3600, "時差つきはUTCへ戻してから現地へ");
    check(!Todoist::ParseDueDate("2026/10/01", jst, due), "区切りが違えば読まない");
    check(!Todoist::ParseDueDate("2026-13-01", jst, due), "13月は読まない");
    check(!Todoist::ParseDueDate("2026-10-01T25:00", jst, due), "25時は読まない");

    Todoist::ParseDueDate("2026-10-01T06:00:00Z", jst, due);
    int64_t epoch = 0;
    check(Todoist::DueEpoch(due, jst, epoch) && epoch == 1790834400LL, "エポック秒へ戻せる");
    Todoist::ParseDueDate("2026-10-01", jst, due);
    check(!Todoist::DueEpoch(due, jst, epoch), "日付だけならエポック秒は無い");

    char buf[48];
    auto fmt = [&](const char* date, int32_t today){
        Todoist::Due x;
        Todoist::ParseDueDate(date, jst, x);
        Todoist::FormatDue(x, today, buf, sizeof(buf));
        return std::string(buf);
    };
    eq_str(fmt("2026-10-01", d20261001).c_str(), "今日", "今日");
    eq_str(fmt("2026-10-02T09:05:00", d20261001).c_str(), "明日 9:05", "明日 + 時刻");
    eq_str(fmt("2026-09-30", d20261001).c_str(), "昨日", "昨日");
    eq_str(fmt("2026-10-03", d20261001).c_str(), "10/3(土)", "今年の日付は曜日つき");
    eq_str(fmt("2027-01-05", d20261001).c_str(), "2027/1/5", "来年は年つき");
    eq_str(fmt("2026-10-01T15:00:00", -1).c_str(), "2026/10/1 15:00", "時計が合っていなければ日付で");

    Todoist::Due od;
    Todoist::ParseDueDate("2026-09-30", jst, od);
    check(Todoist::IsOverdue(od, d20261001, 0), "昨日の期限は期限切れ");
    Todoist::ParseDueDate("2026-10-01", jst, od);
    check(!Todoist::IsOverdue(od, d20261001, 23 * 3600), "今日の(日付だけの)期限は期限切れではない");
    Todoist::ParseDueDate("2026-10-01T15:00:00", jst, od);
    check(Todoist::IsOverdue(od, d20261001, 16 * 3600) && !Todoist::IsOverdue(od, d20261001, 14 * 3600),
          "今日の時刻つきは時刻を過ぎたら期限切れ");
    check(!Todoist::IsOverdue(od, -1, 0), "時計が合っていなければ期限切れにしない");
}

// ---- タスクの一覧 ----

static const char* kListJson = R"JSON({
  "results": [
    {"user_id":"1","id":"A1","project_id":"P","section_id":null,"parent_id":null,"labels":["x"],
     "deadline":null,"duration":{"amount":30,"unit":"minute"},"checked":false,"is_deleted":false,
     "due":{"date":"2026-10-02","is_recurring":false,"lang":"ja","string":"明日"},
     "priority":1,"content":"牛乳を買う","description":"低脂肪","note_count":0},
    {"id":"B2","parent_id":"A1","checked":false,"is_deleted":false,"due":null,"priority":4,
     "content":"\u3042\"引用\"\\","child_order":2},
    {"id":"C3","checked":true,"content":"完了済み","priority":1},
    {"id":"D4","is_deleted":true,"content":"削除済み"},
    {"id":"E5","content":"毎朝","priority":2,
     "due":{"date":"2026-10-01T00:30:00Z","is_recurring":true,"string":"毎日 9:30","timezone":"Asia/Tokyo"}}
  ],
  "next_cursor": "abc.def"
})JSON";

static void testTasks(){
    printf("\n---- タスクの一覧 ----\n");
    const int32_t jst = 9 * 3600;
    Todoist::Task tasks[4];
    Todoist::TaskParser p;
    p.begin(tasks, 4, 0, jst);
    check(p.feed(kListJson, strlen(kListJson)) && p.complete(), "一覧を最後まで読める");
    eq_int(p.count(), 3, "完了済み/削除済みを除いて3件");
    check(!p.overflowed(), "溢れていない");
    eq_str(p.nextCursor().c_str(), "abc.def", "next_cursor");
    if(p.count() == 3){
        eq_str(tasks[0].id.c_str(), "A1", "1件目のid");
        eq_str(tasks[0].content.c_str(), "牛乳を買う", "1件目の名前");
        check(tasks[0].due.has && !tasks[0].due.has_time && tasks[0].due.day == Todoist::DaysFromCivil(2026, 10, 2),
              "1件目の期限(日付だけ)");
        eq_str(tasks[0].due.text.c_str(), "明日", "due.string");
        check(!tasks[0].subtask && tasks[0].priority == 1, "1件目はサブタスクでない");
        eq_str(tasks[1].content.c_str(), "あ\"引用\"\\", "エスケープを戻した名前");
        check(tasks[1].subtask && tasks[1].priority == 4 && !tasks[1].due.has, "2件目はサブタスク・P1・期限なし");
        check(tasks[2].due.has_time && tasks[2].due.recurring && tasks[2].due.sec == 9 * 3600 + 30 * 60,
              "繰り返しの時刻つき(UTC→現地)");
    }

    //1バイトずつでも同じ
    Todoist::Task t2[4];
    Todoist::TaskParser p2;
    p2.begin(t2, 4, 0, jst);
    for(const char* c = kListJson; *c; c++) p2.feed(c, 1);
    check(p2.complete() && p2.count() == 3 && strcmp(t2[2].content.c_str(), "毎朝") == 0,
          "1バイトずつ食わせても同じ結果");

    //上限: 2件しか入らない
    Todoist::Task t3[2];
    Todoist::TaskParser p3;
    p3.begin(t3, 2, 0, jst);
    p3.feed(kListJson, strlen(kListJson));
    check(p3.count() == 2 && p3.overflowed(), "入りきらなければ溢れたことが分かる");

    //続きのページは start_count から追記する
    Todoist::Task t4[4];
    t4[0].content.assign("前のページ");
    Todoist::TaskParser p4;
    p4.begin(t4, 4, 1, jst);
    const char* page2 = "{\"results\":[{\"id\":\"Z9\",\"content\":\"次\"}],\"next_cursor\":null}";
    p4.feed(page2, strlen(page2));
    check(p4.count() == 2 && strcmp(t4[0].content.c_str(), "前のページ") == 0
          && strcmp(t4[1].id.c_str(), "Z9") == 0, "続きのページを後ろへ足す");
    check(p4.nextCursor().empty(), "next_cursor が null なら続きは無い");

    //並べ替え: 期限の早い順、同じ日は日付だけ→時刻順、期限なしは最後、同じなら優先度
    Todoist::Task s[5];
    auto set = [&](int i, const char* id, const char* date, int prio){
        s[i] = Todoist::Task();
        s[i].id.assign(id);
        s[i].priority = (uint8_t)prio;
        if(date) Todoist::ParseDueDate(date, jst, s[i].due);
    };
    set(0, "none", nullptr, 4);
    set(1, "d2-15", "2026-10-02T15:00:00", 1);
    set(2, "d2-all", "2026-10-02", 1);
    set(3, "d1", "2026-10-01", 1);
    set(4, "d2-09", "2026-10-02T09:00:00", 1);
    Todoist::SortTasks(s, 5);
    std::string order;
    for(auto& t : s){ order += t.id.c_str(); order += " "; }
    eq_str(order.c_str(), "d1 d2-all d2-09 d2-15 none ", "並べ替え");
    Todoist::Task q[3];
    for(int i = 0; i < 3; i++){ q[i].id.assign(i == 0 ? "p1" : i == 1 ? "p4" : "p1b"); }
    q[0].priority = 1; q[1].priority = 4; q[2].priority = 1;
    Todoist::SortTasks(q, 3);
    eq_str((std::string(q[0].id.c_str()) + q[1].id.c_str() + q[2].id.c_str()).c_str(), "p4p1p1b",
           "期限が無ければ優先度順で、同じなら元の順");
}

// ---- 失敗の応答・JSONの書き出し ----

static void testErrorAndWrite(){
    printf("\n---- 失敗の応答 ----\n");
    Todoist::ErrorParser e;
    e.begin();
    const char* j = "{\"error\":\"Invalid argument value\",\"error_code\":20,\"error_extra\":{\"error\":\"no\"},\"http_code\":400}";
    e.feed(j, strlen(j));
    eq_str(e.message(), "Invalid argument value", "JSONの error を拾う(入れ子のものは見ない)");
    e.begin();
    e.feed("Forbidden\nmore", 14);
    eq_str(e.message(), "Forbidden", "JSONでなければ本文の1行目");

    printf("\n---- JSONの書き出し ----\n");
    FixedString<PICO_STR_256B> out;
    out.append("{\"content\":");
    check(Todoist::AppendJsonString(out, "牛乳\"と\\と\n\t\x01"), "書ける");
    out.append("}");
    eq_str(out.c_str(), "{\"content\":\"牛乳\\\"と\\\\と\\n\\t\\u0001\"}", "エスケープ");
    //書いたものを読み戻せる
    Recorder r;
    JsonReader jr;
    jr.reset(&r);
    check(jr.feed(out.c_str(), out.length()) && jr.complete() && r.ev.size() == 3
          && r.ev[1] == "content=s:牛乳\"と\\と\n\t\x01@1", "書いたものを読み戻せる");
    FixedString<8> tiny;
    check(!Todoist::AppendJsonString(tiny, "長い長い"), "入りきらなければ false");
}

int main(){
    testJson();
    testDue();
    testTasks();
    testErrorAndWrite();
    printf("\n%s (失敗 %d件)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
