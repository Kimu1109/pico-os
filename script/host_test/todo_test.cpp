// TODOアプリのリマインダー(todo/Todo_Reminders)と画面の配線(TodoScene)のテスト。run.sh から呼ぶ。
//
// 応答の読み取りは todoist_proto_test、通信は run_net.sh の todoist_net_test の担当で、ここでは
//   - リマインダー: 時刻つきの期限を通知(本物の NotificationFunctions)の At の予約にすること、
//     近い順に4件まで・過ぎたものは入れない・何分前・一覧に無くなったものは消す(範囲の中だけ)・
//     変わっていなければ入れ直さない・ほかのC++の予約には触らない
//   - TodoScene: トークンが無いときの案内、生成/解放で漏れが無いこと(ASan)
// を見る。
#include "todo/Todo_Reminders.hpp"
#include "functions/Notification_Functions.hpp"
#include "gui/scenes/TodoScene.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Keyboard_Functions.hpp"
#include "storage/SD_Path.hpp"
#include "OS_Data.hpp"

#include <climits>
#include <cstdio>
#include <cstring>
#include <string>

// ---- モック ----
void PICO_GFX::MarkDirty(const Rect&){}
void PICO_GFX::Setup(){}
void PICO_GFX::FlushDirty(){}
void PICO_GFX::DrawDialogBackground(){}
void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}
void KeyboardFunctions::RegisterInputTarget(ITextInputTarget*){}
void KeyboardFunctions::UnregisterInputTarget(ITextInputTarget*){}
void KeyboardFunctions::Show(ITextInputTarget*, KeyboardFunctions::Layout, bool){}
void KeyboardFunctions::OnPanelShown(KeyboardPanel*){}
void KeyboardFunctions::OnPanelHidden(KeyboardPanel*){}
void KeyboardFunctions::OnPanelResized(KeyboardPanel*){}
void KeyboardFunctions::OnPanelChanged(KeyboardPanel*, bool){}
void KeyboardFunctions::HideAll(){}
void SceneFunctions::Pop(){}

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

using namespace NotificationFunctions;

static const int32_t kJst = 9 * 3600;
// 2026-10-01 12:00 JST
static const int64_t kNow = 1790823600LL;
static const int32_t kToday = Todoist::DaysFromCivil(2026, 10, 1);

static Todoist::Task MakeTask(const char* id, const char* content, const char* due){
    Todoist::Task t;
    t.id.assign(id);
    t.content.assign(content);
    if(due) Todoist::ParseDueDate(due, kJst, t.due);
    return t;
}

static const Rule* FindTag(const char* tag){
    for(int i = 0; i < RuleCount(); i++){
        const Rule* r = RuleAt(i);
        if(r && strcmp(r->content.tag.c_str(), tag) == 0) return r;
    }
    return nullptr;
}

static void testReminders(){
    printf("\n---- リマインダー ----\n");
    OSData::SD_usable = true;
    HostSd::files.clear();
    SetupAt(0);

    //ほかのC++の予約が1件あると、TODOが使えるのは残り3件
    Content other;
    other.title.assign("ほかの予約");
    When w;
    w.trigger = Trigger::Daily;
    w.hour = 7;
    check(Schedule(other, w) == Result::Ok, "ほかのC++の予約を入れられる");

    Todoist::Task tasks[7] = {
        MakeTask("past", "もう過ぎた", "2026-10-01T09:00:00"),
        MakeTask("allday", "日付だけ", "2026-10-01"),
        MakeTask("t15", "15時の会議", "2026-10-01T15:00:00"),
        MakeTask("t13", "13時の電話", "2026-10-01T13:00:00"),
        MakeTask("utc", "UTCの予定", "2026-10-01T05:00:00Z"),       // = 14:00 JST
        MakeTask("t18", "18時の買い物", "2026-10-01T18:00:00"),
        MakeTask("nodue", "期限なし", nullptr),
    };
    const int64_t end_of_today = (int64_t)(kToday + 1) * 86400 - kJst;
    const int n = TodoReminders::Sync(tasks, 7, kNow, end_of_today, kJst, 0, "TODO");
    eq_int(n, 3, "空いている3件だけ入れる");
    eq_int(TodoReminders::Count(), 3, "TODOの予約は3件");
    check(FindTag("td:t13") && FindTag("td:utc") && FindTag("td:t15"), "近い順に13時・14時(UTC)・15時");
    check(!FindTag("td:t18"), "4件目(18時)は入らない");
    check(!FindTag("td:past") && !FindTag("td:allday") && !FindTag("td:nodue"),
          "過ぎたもの・日付だけ・期限なしは入れない");
    const Rule* r13 = FindTag("td:t13");
    if(r13){
        check(r13->when.trigger == Trigger::At && r13->when.at_epoch == kNow + 3600, "13時ちょうどに知らせる");
        eq_str(r13->content.title.c_str(), "13時の電話", "タイトルはタスク名");
        eq_str(r13->content.body.c_str(), "期限 2026/10/1 13:00", "本文は期限");
        eq_str(r13->content.app.c_str(), "TODO", "タップでTODOアプリを開く");
        eq_str(r13->content.data.c_str(), "t13", "起動理由にタスクのid");
        check(r13->content.owner.empty(), "送り主は空(C++)");
    }
    eq_int(RuleCount(), 4, "予約は全部で4件");

    //同じ一覧で入れ直しても予約は変わらない(idも同じ)
    const uint16_t id13 = r13 ? r13->id : 0;
    TodoReminders::Sync(tasks, 7, kNow, end_of_today, kJst, 0, "TODO");
    check(FindTag("td:t13") && FindTag("td:t13")->id == id13, "変わっていなければ入れ直さない");

    //13時を完了にした(一覧から消えた) → 13時は消え、空いた枠に18時が入る
    Todoist::Task after[6] = {tasks[0], tasks[1], tasks[2], tasks[4], tasks[5], tasks[6]};
    TodoReminders::Sync(after, 6, kNow, end_of_today, kJst, 0, "TODO");
    check(!FindTag("td:t13") && FindTag("td:t18"), "一覧から消えたものは消し、次を入れる");

    //10分前
    TodoReminders::Sync(after, 6, kNow, end_of_today, kJst, 10, "TODO");
    const Rule* u = FindTag("td:utc");
    check(u && u->when.at_epoch == kNow + 2 * 3600 - 600, "10分前に知らせる");
    check(u && strstr(u->content.body.c_str(), "(10分前)") != nullptr, "本文に何分前かを書く");

    //範囲の外の予約は、一覧に無くても残す(別の表示で入れたもの)
    TodoReminders::CancelAll();
    eq_int(TodoReminders::Count(), 0, "CancelAll で全部消える");
    eq_int(RuleCount(), 1, "ほかの予約には触らない");
    Todoist::Task tomorrow[1] = { MakeTask("tm9", "明日9時", "2026-10-02T09:00:00") };
    TodoReminders::Sync(tomorrow, 1, kNow, INT64_MAX, kJst, 0, "TODO");
    check(FindTag("td:tm9") != nullptr, "明日の予定を入れる(7日間の表示)");
    //今日の表示(今日の終わりまでを網羅)には明日の予定が無いが、範囲の外なので消さない
    TodoReminders::Sync(tasks, 2, kNow, end_of_today, kJst, 0, "TODO");
    check(FindTag("td:tm9") != nullptr, "範囲の外の予約は残す");
    //すべての表示(全部を網羅)に無ければ消す
    TodoReminders::Sync(tasks, 2, kNow, INT64_MAX, kJst, 0, "TODO");
    check(FindTag("td:tm9") == nullptr, "全部を網羅した一覧に無ければ消す");

    TodoReminders::Sync(tomorrow, 1, kNow, INT64_MAX, kJst, 0, "TODO");
    TodoReminders::Cancel("tm9");
    check(FindTag("td:tm9") == nullptr, "1件だけ取り消せる");

    //保存される(Delay以外は notify_rules.tsv へ)
    TodoReminders::Sync(tomorrow, 1, kNow, INT64_MAX, kJst, 0, "TODO");
    SaveNow();
    const std::string saved = HostSd::files[PICO_Path::FILE::SYS_NOTIFY_RULES];
    check(saved.find("td:tm9") != std::string::npos, "予約は保存される(再起動しても知らせる)");
}

static void testScene(){
    printf("\n---- TodoScene ----\n");
    OSData::SD_usable = true;
    HostSd::files.erase(PICO_Path::FILE::CFG::SYS_TODOIST_CFG);

    TodoScene* scene = new TodoScene();
    scene->onEnter();
    const int widgets = (int)WidgetFunctions::widgets.size();
    check(widgets >= 10, "ウィジェットを作る");
    for(int i = 0; i < 5; i++) scene->onUpdate();

    //状態の行にトークンの案内が出る
    bool found = false;
    for(Widget* w : WidgetFunctions::widgets){
        //状態の行は Label<PICO_STR_LL>(ほかの Label とは N が違う)
        auto* l = dynamic_cast<Label<PICO_STR_LL>*>(w);
        if(l && strstr(l->getText()->c_str(), "トークン") != nullptr) found = true;
    }
    check(found, "トークンが無ければ案内を出す");

    scene->onExit();
    WidgetFunctions::ClearSceneWidgets();
    eq_int((long)WidgetFunctions::widgets.size(), 0, "片付けるとウィジェットは残らない");

    //もう一度入っても同じように作れる(Push()から戻ったとき)
    scene->onEnter();
    check((int)WidgetFunctions::widgets.size() == widgets, "入り直しても同じ数");
    scene->onExit();
    WidgetFunctions::ClearSceneWidgets();
    delete scene;
}

int main(){
    testReminders();
    testScene();
    printf("\n%s (失敗 %d件)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
