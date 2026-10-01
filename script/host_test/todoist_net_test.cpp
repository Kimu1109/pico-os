// TODOアプリの通信係(TodoistClient)を本物のソケットで確かめる結合テスト。run_net.sh から呼ぶ。
//
// **本物のソケットとTLS(OpenSSL、pc/compat/)を使う。** 相手は script/host_test/todoist_fake_server.py
// (Todoist API v1 の偽物。run_net.sh が平文とHTTPSで立てる。HTTPSは使い捨てのCAで作った証明書)。
// 本物の api.todoist.com へは行かない(トークンが要るため)。
//
// 見ること:
//   - 今日/7日間/すべて の一覧を取れ、期限の早い順に並ぶ。完了済みは並ばない
//   - ページに分かれていれば続きを取り、kMaxTasks で打ち切って溢れたことが分かる
//   - タスクを足す(名前の " や \ が往復で崩れない、期限は due_lang=ja の自然言語)。期限が読めなければ理由が返る
//   - 完了にすると一覧から消え、繰り返しのタスクは次の回へ進む。もう無いタスクは404の理由が返る
//   - **接続を使い回す**(keep-alive)。HTTPSでも同じ
//   - トークンが違えば AuthError になり、勝手に取り直さない
//   - /sys/todoist.cfg を読み、平文のトークンは暗号化して書き直す
//
// 使い方: todoist_net_test <httpのport> <httpsのport> <ca.pem> <トークン> <ページ分けしたhttpのport>
#include "todo/Todoist_Client.hpp"
#include "functions/Log_Functions.hpp"
#include "storage/SD_Path.hpp"
#include "util/Secret_Cipher.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <unistd.h>

// ---- モック ----
void LogFunctions::Log(LogType, const char* fmt, ...){
    va_list args;
    va_start(args, fmt);
    printf("    [log] ");
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}

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

using S = TodoistClient::State;

// millis() はスタブで進まないので、回数で打ち切る(1回1msで最大10秒)
static bool runUntil(TodoistClient& c, const std::function<bool()>& done){
    for(int i = 0; i < 10000; i++){
        c.update(true);
        if(done()) return true;
        usleep(1000);
    }
    return false;
}

// 一覧を取り直して、終わるまで回す。成功したか
static bool fetch(TodoistClient& c){
    const uint32_t rev = c.tasksRevision();
    c.refreshNow();
    return runUntil(c, [&]{ return (c.tasksRevision() != rev && c.loaded() && !c.busy())
                                   || c.state() == S::Error || c.state() == S::AuthError; })
        && c.state() == S::Ok;
}

// 操作が終わるまで回す。成功したか
static bool runAction(TodoistClient& c, const std::function<bool()>& start){
    const uint32_t rev = c.actionRevision();
    if(!start()) return false;
    return runUntil(c, [&]{ return c.actionRevision() != rev; }) && c.lastActionOk();
}

static const Todoist::Task* byName(TodoistClient& c, const char* name){
    for(int i = 0; i < c.taskCount(); i++){
        if(strcmp(c.taskAt(i).content.c_str(), name) == 0) return &c.taskAt(i);
    }
    return nullptr;
}

static void scenario(const char* label, const std::string& api, const char* token, const char* bad_token){
    printf("\n---- %s (%s) ----\n", label, api.c_str());

    // ---- トークン違い ----
    {
        TodoistClient bad;
        check(bad.configure(bad_token, api.c_str()), "違うトークンでも設定は受け付ける");
        const uint32_t rev = bad.statusRevision();
        bad.refreshNow();
        runUntil(bad, [&]{ return bad.state() == S::AuthError || (bad.statusRevision() != rev && bad.state() == S::Error); });
        check(bad.state() == S::AuthError, "401 なら AuthError");
        //勝手に取り直さない
        for(int i = 0; i < 50; i++){ bad.update(true); usleep(1000); }
        check(!bad.busy(), "AuthError の後は勝手に取り直さない");
        bad.stop();
    }

    TodoistClient c;
    c.setUtcOffset(9 * 3600);
    check(c.configure(token, (api + "/").c_str()), "設定を受け付ける(末尾の / は落とす)");

    // ---- 今日 ----
    check(fetch(c), "今日の一覧を取れる");
    eq_int(c.taskCount(), 4, "今日と期限切れで4件(完了済みは並ばない)");
    if(c.taskCount() == 4){
        eq_str(c.taskAt(0).content.c_str(), "期限切れの書類", "一番上は期限切れ");
        check(c.taskAt(0).priority == 4, "期限切れの書類は P1(priority 4)");
        eq_str(c.taskAt(1).content.c_str(), "朝の運動", "今日の日付だけのものが時刻つきより先");
        check(c.taskAt(1).due.recurring && strcmp(c.taskAt(1).due.text.c_str(), "毎日") == 0, "繰り返しと due.string");
        eq_str(c.taskAt(2).content.c_str(), "会議", "UTC 01:00 = 現地 10:00 が 15:00 より先");
        check(c.taskAt(2).due.has_time && c.taskAt(2).due.sec == 10 * 3600, "UTCの時刻を現地へずらす");
        eq_str(c.taskAt(3).content.c_str(), "牛乳を買う", "浮動時刻 15:00");
    }
    check(!c.truncated(), "溢れていない");
    check(c.connectionKept(), "接続を使い回すために持っている(keep-alive)");

    // ---- 7日間 / すべて ----
    c.setView(TodoistClient::View::Week);
    check(!c.loaded() && c.taskCount() == 0, "表示を変えると一覧を捨てる");
    check(fetch(c), "7日間の一覧を取れる");
    eq_int(c.taskCount(), 5, "7日間は5件");
    check(byName(c, "来週の準備") != nullptr, "3日後の予定が入る");

    c.setView(TodoistClient::View::All);
    check(fetch(c), "すべての一覧を取れる");
    eq_int(c.taskCount(), 7, "すべては7件(完了済みを除く)");
    const Todoist::Task* sub = byName(c, "サブタスク");
    check(sub && sub->subtask, "parent_id があればサブタスク");
    check(byName(c, "いつか読む本 \"引用\" と \\ 記号") != nullptr, "名前の \" と \\ を戻せる");
    if(c.taskCount() == 7) check(!c.taskAt(6).due.has, "期限なしは最後");

    // ---- 追加 ----
    check(runAction(c, [&]{ return c.add("pico から \"足した\" \\ タスク", "明日 15時"); }), "期限つきで足せる");
    check(fetch(c), "足した後に取り直せる");
    const Todoist::Task* added = byName(c, "pico から \"足した\" \\ タスク");
    check(added && added->due.has_time && added->due.sec == 15 * 3600, "足したタスクが期限つきで並ぶ");

    const bool bad_due = runAction(c, [&]{ return c.add("期限が読めない", "そのうち"); });
    check(!bad_due && c.lastAction() == TodoistClient::Action::Add, "読めない期限なら失敗する");
    eq_str(c.actionMessage(), "Date is invalid (HTTP 400)", "失敗の理由(JSONの error)");
    check(c.state() == S::Ok, "操作の失敗では一覧の状態は Ok のまま");

    check(runAction(c, [&]{ return c.add("期限なしで足す", ""); }), "期限なしでも足せる");

    // ---- 完了 ----
    const Todoist::Task* milk = byName(c, "牛乳を買う");
    Todoist::TaskId milk_id;
    if(milk) milk_id = milk->id;
    check(runAction(c, [&]{ return c.close(milk_id.c_str()); }), "完了にできる");
    check(byName(c, "牛乳を買う") == nullptr, "取り直す前に一覧から消える");
    eq_str(c.lastClosedId(), milk_id.c_str(), "完了にしたid");
    check(fetch(c), "完了の後に取り直せる");
    check(byName(c, "牛乳を買う") == nullptr, "取り直しても無い");

    const Todoist::Task* morning = byName(c, "朝の運動");
    Todoist::TaskId morning_id;
    int32_t morning_day = 0;
    if(morning){ morning_id = morning->id; morning_day = morning->due.day; }
    check(runAction(c, [&]{ return c.close(morning_id.c_str()); }), "繰り返しのタスクを完了にできる");
    check(fetch(c), "取り直せる");
    morning = byName(c, "朝の運動");
    check(morning && morning->due.day == morning_day + 1, "繰り返しのタスクは次の回へ進む");

    check(!runAction(c, [&]{ return c.close(milk_id.c_str()); }), "もう無いタスクは失敗する");
    eq_str(c.actionMessage(), "Task not found (HTTP 404)", "404の理由");
    check(!c.close("bad/id"), "id にパスの区切りがあれば受け付けない");

    // ---- 操作は1つずつ ----
    check(c.add("1つ目", ""), "操作を頼める");
    check(!c.add("2つ目", ""), "前の操作が終わるまで次は頼めない");
    runUntil(c, [&]{ return !c.actionBusy(); });
    check(c.connectionKept(), "最後まで接続を使い回している");
    c.stop();
    check(!c.connectionKept(), "stop() で接続を閉じる");
}

static void paging(const std::string& api, const char* token){
    printf("\n---- ページ分け (%s) ----\n", api.c_str());
    TodoistClient c;
    check(c.configure(token, api.c_str()), "設定を受け付ける");
    c.setView(TodoistClient::View::All);
    check(fetch(c), "ページに分かれた一覧を取れる");
    eq_int(c.taskCount(), TodoistClient::kMaxTasks, "kMaxTasks 件で打ち切る");
    check(c.truncated(), "溢れたことが分かる");
    //期限のあるものが先に並ぶ(全部取ってから並べ替える)
    check(c.taskCount() > 0 && strcmp(c.taskAt(0).content.c_str(), "期限切れの書類") == 0, "ページを跨いでも並べ替える");

    c.setView(TodoistClient::View::Today);
    check(fetch(c), "少ない一覧も取れる");
    eq_int(c.taskCount(), 4, "今日は4件");
    check(!c.truncated(), "溢れていない");
    c.stop();
}

static void config(const std::string& api, const char* token){
    printf("\n---- /sys/todoist.cfg ----\n");
    const char* path = PICO_Path::FILE::CFG::SYS_TODOIST_CFG;

    TodoistClient none;
    HostSd::files.erase(path);
    check(!none.loadConfig() && none.state() == S::NotConfigured, "todoist.cfg が無ければ NotConfigured");

    HostSd::files[path] = std::string("# テスト\napi = ") + api + "\ntoken = " + token
                        + "\nreminders = false\nremind-before-min = 10\n";
    TodoistClient c;
    check(c.loadConfig(), "平文のトークンを読める");
    check(!c.remindersEnabled() && c.remindBeforeMin() == 10, "リマインダーの設定を読める");
    const std::string saved = HostSd::files[path];
    check(saved.find("token=enc1:") != std::string::npos && saved.find(token) == std::string::npos,
          "平文のトークンは暗号化して書き直す");
    check(saved.find("remind-before-min = 10") != std::string::npos, "ほかの行は残す");

    TodoistClient c2;
    check(c2.loadConfig(), "暗号化したトークンを読める");
    check(fetch(c2), "暗号化したトークンで繋がる");
    c2.stop();

    //today-filter = today は偽物が知らないフィルタ → 400 の理由が状態に出る
    HostSd::files[path] += "today-filter = today\n";
    TodoistClient c3;
    c3.loadConfig();
    c3.refreshNow();
    runUntil(c3, [&]{ return c3.state() == S::Error; });
    eq_str(c3.statusText(), "Invalid filter query (HTTP 400)", "フィルタを差し替えられる(偽物は知らないので400)");
    c3.stop();
}

int main(int argc, char** argv){
    if(argc < 6){
        fprintf(stderr, "使い方: %s <http port> <https port> <ca.pem> <token> <paged http port>\n", argv[0]);
        return 2;
    }
    std::ifstream ca_in(argv[3]);
    std::stringstream ca;
    ca << ca_in.rdbuf();
    OSData::SD_usable = true;
    HostSd::files[PICO_Path::FILE::TLS_EXTRA_CA_PEM] = ca.str();

    const char* token = argv[4];
    scenario("HTTP", std::string("http://127.0.0.1:") + argv[1], token, "0000wrongtoken");
    scenario("HTTPS", std::string("https://localhost:") + argv[2], token, "0000wrongtoken");
    paging(std::string("http://127.0.0.1:") + argv[5], token);
    config(std::string("http://127.0.0.1:") + argv[5], token);

    printf("\n%s (失敗 %d件)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
