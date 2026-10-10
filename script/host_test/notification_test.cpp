// 通知(NotificationFunctions)の中身の検証。
//
// 時刻・電池・Wi-FiはUpdateAt()へ直接渡す。トースト・音・起動はNotification_Sources.cppの仕事なので
// ここではリンクせず、中身が「トーストの順番待ち」「通知音の要求」「起動」として外へ出すものを見る。
// 確かめること:
//   - すぐ出す(Post)と履歴・未読・トーストの順番待ち・通知音
//   - Delay / At(時計が合うまで待つ・過ぎていれば出す) / Daily(同じ分に2回出さない・登録した分は見送る) / Every
//   - 電池(下回ったら1回、戻るまで出さない) / Wi-Fi(つながった/切れた瞬間だけ)
//   - 同じ送り主・同じtagの置き換え、送り主ごとの上限、表の満杯、他の送り主の予約は消せない
//   - 履歴の輪(溢れたら古いものから消える)、既読、全消去
//   - 控えめ(quiet)ではトーストも音も出さない
//   - 保存と読み込み(Delayは保存しない)、壊れた行を捨てる、送り主が消えた予約を捨てる
//   - Open()で起動し、起動理由は送り主が一致するときに1回だけ受け取れる
#include "functions/Notification_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "storage/SD_Path.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstring>
#include <string>

// ---- 偽物 ----
void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

using namespace NotificationFunctions;

static struct tm At(int year, int mon, int mday, int hour, int min, int wday = 3){
    struct tm t = {};
    t.tm_year = year - 1900; t.tm_mon = mon - 1; t.tm_mday = mday;
    t.tm_hour = hour; t.tm_min = min; t.tm_wday = wday;
    t.tm_yday = (mon - 1) * 31 + mday - 1;
    return t;
}
static const struct tm kNoClock = At(1970, 1, 1, 0, 0);

static Content Make(const char* title, const char* owner = "", const char* tag = ""){
    Content c;
    c.title.assign(title);
    c.owner.assign(owner);
    c.tag.assign(tag);
    c.app.assign(owner[0] ? "App" : "");
    return c;
}

static int ToastCount(){
    int n = 0;
    uint32_t seq;
    while(PopToast(seq)) n++;
    return n;
}

static void Reset(const char* cfg = nullptr, const char* rules = nullptr){
    HostSd::files.clear();
    if(cfg) HostSd::files[PICO_Path::FILE::CFG::SYS_NOTIFY_CFG] = cfg;
    if(rules) HostSd::files[PICO_Path::FILE::SYS_NOTIFY_RULES] = rules;
    OSData::SD_usable = true;
    SetupAt(0);
    SetLauncher(nullptr);
}

static std::string last_launched;
static bool FakeLaunch(const char* name){ last_launched = name; return true; }

static bool ResolveKeepA(const char* owner, AppName& out){
    if(strcmp(owner, "/lua/apps/a") == 0){ out.assign("新しい名前"); return true; }
    return false;
}

int main(){
    Sensors s;
    unsigned long t = 1000;

    // ---- すぐ出す ----
    Reset();
    check(Post(Make("")) == 0, "タイトルが空なら出さない");
    const uint32_t seq1 = Post(Make("こんにちは"));
    check(seq1 != 0 && HistoryCount() == 1 && UnreadCount() == 1, "Post()で履歴に積まれ未読になる");
    check(TakeChime() && !TakeChime(), "通知音の要求は1回取り出すと下りる");
    check(ToastCount() == 1, "トーストの順番待ちに入る");
    {
        Content c = Make("音なし");
        c.sound = false;
        Post(c);
        check(!TakeChime(), "sound=falseなら通知音を要求しない");
    }
    check(HistoryAt(0)->content.title == "音なし", "履歴の0番が一番新しい");
    MarkRead(seq1);
    check(UnreadCount() == 1, "既読にすると未読が減る");
    MarkAllRead();
    check(UnreadCount() == 0, "全部既読");

    // ---- 控えめ ----
    Reset();
    SetMode(Mode::Quiet, false);
    Post(Make("静か"));
    check(HistoryCount() == 1 && UnreadCount() == 1, "控えめでも履歴と未読には残る");
    check(ToastCount() == 0 && !TakeChime(), "控えめではトーストも音も出さない");
    SetSoundEnabled(false, false);
    SetMode(Mode::On, false);
    Post(Make("音を切った"));
    check(ToastCount() == 1 && !TakeChime(), "音を切るとトーストだけ");

    // ---- Delay ----
    Reset();
    uint16_t id = 0;
    When w; w.trigger = Trigger::Delay; w.delay_ms = 5000;
    UpdateAt(t = 1000, kNoClock, 0, s);
    check(Schedule(Make("5秒後", "/lua/apps/a"), w, &id) == Result::Ok && id != 0, "Delayを予約できる");
    check(RuleCount() == 1, "予約が1件");
    UpdateAt(t = 5999, kNoClock, 0, s);
    check(HistoryCount() == 0, "時間前は出さない");
    UpdateAt(t = 6000, kNoClock, 0, s);
    check(HistoryCount() == 1 && RuleCount() == 0, "時間になったら出して予約は消える");
    check(HistoryAt(0)->epoch == 0, "時計が合っていなければ時刻は0");

    // ---- Every ----
    Reset();
    UpdateAt(t = 0, kNoClock, 0, s);
    w = When(); w.trigger = Trigger::Every; w.every_ms = 5000;
    check(Schedule(Make("短すぎ"), w) == Result::Invalid, "Everyはkmin未満を断る");
    w.every_ms = 10000;
    Schedule(Make("10秒ごと"), w);
    for(t = 0; t <= 35000; t += 100) UpdateAt(t, kNoClock, 0, s);
    check(HistoryCount() == 3 && RuleCount() == 1, "Everyは繰り返して予約は残る(35秒で3回)");
    //止まっていた(フレームが来なかった)後でも溜めて何回も出さない
    UpdateAt(t = 200000, kNoClock, 0, s);
    check(HistoryCount() == 4, "遅れても1回だけ");

    // ---- At ----
    Reset();
    const int64_t base = 1790000000; // 2026年頃
    w = When(); w.trigger = Trigger::At; w.at_epoch = base + 60;
    Schedule(Make("時刻"), w);
    UpdateAt(t = 100, kNoClock, 100, s);
    check(HistoryCount() == 0, "時計が合う前は出さない");
    UpdateAt(t = 200, At(2026, 9, 30, 12, 0), base, s);
    check(HistoryCount() == 0, "時刻前は出さない");
    UpdateAt(t = 300, At(2026, 9, 30, 12, 1), base + 3600, s);
    check(HistoryCount() == 1 && RuleCount() == 0, "過ぎていれば(同期が遅れても)出して消える");
    check(HistoryAt(0)->epoch == base + 3600, "出した時刻を覚える");

    // ---- Daily ----
    Reset();
    UpdateAt(t = 0, At(2026, 9, 30, 7, 30), base, s);
    w = When(); w.trigger = Trigger::Daily; w.hour = 7; w.minute = 30;
    Schedule(Make("毎朝"), w);
    UpdateAt(t = 16, At(2026, 9, 30, 7, 30), base, s);
    check(HistoryCount() == 0, "ちょうどその分に登録しても今すぐは出さない");
    UpdateAt(t = 32, At(2026, 9, 30, 7, 31), base, s);
    UpdateAt(t = 48, At(2026, 10, 1, 7, 30), base, s);
    check(HistoryCount() == 1, "翌日のその時刻に出る");
    for(int i = 0; i < 10; i++) UpdateAt(t += 16, At(2026, 10, 1, 7, 30), base, s);
    check(HistoryCount() == 1 && RuleCount() == 1, "同じ分に2回出さず、予約は残る");

    // ---- 電池 ----
    Reset();
    w = When(); w.trigger = Trigger::BatteryLow; w.below = 20;
    Schedule(Make("電池が少ない"), w);
    s = Sensors(); s.battery_valid = true; s.battery_percent = 50;
    UpdateAt(t = 0, kNoClock, 0, s);
    s.battery_percent = 19; UpdateAt(t += 16, kNoClock, 0, s);
    check(HistoryCount() == 1, "下回ったら出す");
    s.battery_percent = 18; UpdateAt(t += 16, kNoClock, 0, s);
    s.battery_percent = 22; UpdateAt(t += 16, kNoClock, 0, s);
    s.battery_percent = 19; UpdateAt(t += 16, kNoClock, 0, s);
    check(HistoryCount() == 1, "少し戻っただけ(+5%未満)では次を出さない");
    s.battery_percent = 30; UpdateAt(t += 16, kNoClock, 0, s);
    s.battery_percent = 10; UpdateAt(t += 16, kNoClock, 0, s);
    check(HistoryCount() == 2, "十分戻ってからまた下回れば出す");
    s.battery_valid = false; s.battery_percent = 0; UpdateAt(t += 16, kNoClock, 0, s);
    check(HistoryCount() == 2, "読めていない間は判定しない");

    // ---- Wi-Fi ----
    Reset();
    s = Sensors(); s.wifi_connected = true;
    UpdateAt(t = 0, kNoClock, 0, s);
    w = When(); w.trigger = Trigger::WifiDisconnected;
    Schedule(Make("切れた"), w);
    w.trigger = Trigger::WifiConnected;
    Schedule(Make("つながった"), w);
    UpdateAt(t += 16, kNoClock, 0, s);
    check(HistoryCount() == 0, "登録した時点の状態では出さない");
    s.wifi_connected = false; UpdateAt(t += 16, kNoClock, 0, s);
    UpdateAt(t += 16, kNoClock, 0, s);
    check(HistoryCount() == 1 && HistoryAt(0)->content.title == "切れた", "切れた瞬間に1回");
    s.wifi_connected = true; UpdateAt(t += 16, kNoClock, 0, s);
    check(HistoryCount() == 2 && HistoryAt(0)->content.title == "つながった", "つながった瞬間に1回");

    // ---- 置き換え・上限・送り主 ----
    Reset();
    w = When(); w.trigger = Trigger::Delay; w.delay_ms = 1000;
    uint16_t a = 0, b = 0;
    Schedule(Make("1", "/lua/apps/a", "x"), w, &a);
    Schedule(Make("2", "/lua/apps/a", "x"), w, &b);
    check(RuleCount() == 1 && FindRule(b) && FindRule(b)->content.title == "2" && !FindRule(a),
          "同じ送り主・同じtagは置き換える");
    Schedule(Make("3", "/lua/apps/b", "x"), w);
    check(RuleCount() == 2, "送り主が違えば同じtagでも別");
    for(int i = 0; i < kMaxRulesPerOwner; i++) Schedule(Make("n", "/lua/apps/a"), w);
    check(Schedule(Make("n", "/lua/apps/a"), w) == Result::OwnerQuota, "送り主ごとの上限");
    check(Cancel("/lua/apps/b", b) == 0 && FindRule(b), "他の送り主の予約は消せない");
    check(CancelTag("/lua/apps/a", "x") == 1, "tagで取り消せる");
    check(CancelAll("/lua/apps/a") == kMaxRulesPerOwner - 1, "送り主の予約を全部消す");
    check(RuleCount() == 1, "他の送り主の予約は残る");
    //表の満杯(OS=送り主なしは送り主ごとの上限が無い)
    Reset();
    for(int i = 0; i < kMaxRules; i++) Schedule(Make("os"), w);
    check(Schedule(Make("os"), w) == Result::Full, "表が満杯なら断る");

    // ---- 履歴のtag置き換え・輪 ----
    Reset();
    Post(Make("古い", "/lua/apps/a", "t"));
    Post(Make("新しい", "/lua/apps/a", "t"));
    check(HistoryCount() == 1 && HistoryAt(0)->content.title == "新しい", "同じtagの通知は履歴でも置き換える");
    Reset();
    for(int i = 0; i < kMaxHistory + 3; i++){
        char title[16]; snprintf(title, sizeof(title), "n%d", i);
        Post(Make(title));
    }
    check(HistoryCount() == kMaxHistory, "履歴は上限まで");
    {
        char newest[16]; snprintf(newest, sizeof(newest), "n%d", kMaxHistory + 2);
        char oldest[16]; snprintf(oldest, sizeof(oldest), "n%d", 3);
        check(HistoryAt(0)->content.title == newest && HistoryAt(kMaxHistory - 1)->content.title == oldest,
              "溢れたら古いものから消える");
    }
    check(ToastCount() == kMaxToastQueue, "トーストの順番待ちは上限まで(古いものを捨てる)");
    const uint32_t mid = HistoryAt(5)->seq;
    RemoveEntry(mid);
    check(HistoryCount() == kMaxHistory - 1 && !FindEntry(mid), "1件消せる");
    {
        //n0〜n(kMaxHistory+2)を出し、新しい方から6番目(index 5)を消した
        const int newest = kMaxHistory + 2;
        char t0[16], t4[16], t5[16];
        snprintf(t0, sizeof(t0), "n%d", newest);
        snprintf(t4, sizeof(t4), "n%d", newest - 4);
        snprintf(t5, sizeof(t5), "n%d", newest - 6);
        check(HistoryAt(0)->content.title == t0 && HistoryAt(4)->content.title == t4 &&
              HistoryAt(5)->content.title == t5, "消した後も順番が保たれる");
    }
    ClearHistory();
    check(HistoryCount() == 0 && UnreadCount() == 0, "全消去");

    // ---- 保存と読み込み ----
    Reset();
    UpdateAt(t = 0, kNoClock, 0, Sensors());
    w = When(); w.trigger = Trigger::Daily; w.hour = 8; w.minute = 5;
    Content c = Make("朝\tです", "/lua/apps/a", "morning");
    Sanitize(c.body, "本文\n2行目");
    c.data.assign("d1");
    Schedule(c, w);
    w = When(); w.trigger = Trigger::Delay; w.delay_ms = 1000;
    Schedule(Make("消える"), w);
    w = When(); w.trigger = Trigger::BatteryLow; w.below = 15;
    Content c2 = Make("電池");
    c2.sound = false;
    Schedule(c2, w);
    check(HostSd::files.count(PICO_Path::FILE::SYS_NOTIFY_RULES) == 0, "保存はすぐには書かない");
    UpdateAt(t = kSaveDelayMs + 1, kNoClock, 0, Sensors());
    check(HostSd::files.count(PICO_Path::FILE::SYS_NOTIFY_RULES) == 1, "少し経ってからまとめて書く");
    {
        const std::string saved = HostSd::files[PICO_Path::FILE::SYS_NOTIFY_RULES];
        check(saved.find("消える") == std::string::npos, "Delayは保存しない");
        check(saved.find("朝 です") != std::string::npos && saved.find("本文 2行目") != std::string::npos,
              "タブ/改行は空白にしてから保存する");
        HostSd::files.clear();
        HostSd::files[PICO_Path::FILE::SYS_NOTIFY_RULES] = saved + "daily\t99:00\t1\t\t\t\t\tx\t\nbogus\n";
    }
    SetupAt(0);
    check(RuleCount() == 2, "読み込める(壊れた行は捨てる)");
    {
        const Rule* r0 = RuleAt(0);
        const Rule* r1 = RuleAt(1);
        check(r0 && r0->when.trigger == Trigger::Daily && r0->when.hour == 8 && r0->when.minute == 5 &&
              r0->content.owner == "/lua/apps/a" && r0->content.tag == "morning" && r0->content.data == "d1" &&
              r0->content.body == "本文 2行目" && r0->content.sound, "Dailyの中身が戻る");
        check(r1 && r1->when.trigger == Trigger::BatteryLow && r1->when.below == 15 && !r1->content.sound,
              "電池の中身が戻る");
    }
    // 送り主が消えた予約は捨て、名前は最新にする
    w = When(); w.trigger = Trigger::Daily; w.hour = 1; w.minute = 0;
    Schedule(Make("消えたアプリ", "/lua/apps/gone"), w);
    RefreshOwners(&ResolveKeepA);
    check(RuleCount() == 2, "送り主が見つからない予約は捨てる(OSの予約は残す)");
    check(RuleAt(0)->content.app == "新しい名前", "アプリ名を登録簿に合わせる");
    // 全部消したらファイルも消える
    CancelAll("/lua/apps/a");
    CancelAll("");
    SaveNow();
    check(HostSd::files.count(PICO_Path::FILE::SYS_NOTIFY_RULES) == 0, "予約が無くなればファイルを消す");

    // ---- 設定 ----
    Reset("mode = quiet\nsound = false\n");
    check(GetMode() == Mode::Quiet && !GetSoundEnabled(), "notify.cfgを読む");
    SetMode(Mode::On);
    check(HostSd::files[PICO_Path::FILE::CFG::SYS_NOTIFY_CFG].find("mode=on") != std::string::npos,
          "SetMode()はnotify.cfgへ書く");

    // ---- 開く・起動理由 ----
    Reset();
    UpdateAt(t = 10000, kNoClock, 0, Sensors());
    Content oc = Make("開いて", "/lua/apps/a", "tg");
    oc.data.assign("42");
    const uint32_t os = Post(oc);
    const uint32_t sys = Post(Make("システム"));
    check(!Open(os), "起動の仕組みが無ければ開けない");
    SetLauncher(&FakeLaunch);
    check(!Open(sys), "アプリの無い通知は開けない(既読にはする)");
    check(FindEntry(sys)->read, "開こうとしたら既読");
    check(Open(os) && last_launched == "App", "送ったアプリを起動する");
    Tag tag, data;
    check(!TakeLaunchReason("/lua/apps/b", tag, data), "送り主が違えば受け取れない");
    check(TakeLaunchReason("/lua/apps/a", tag, data) && tag == "tg" && data == "42", "起動理由を受け取る");
    check(!TakeLaunchReason("/lua/apps/a", tag, data), "起動理由は1回きり");
    Open(os);
    UpdateAt(t += kLaunchReasonTtlMs + 1, kNoClock, 0, Sensors());
    check(!TakeLaunchReason("/lua/apps/a", tag, data), "時間が経った起動理由は捨てる");

    printf("\n%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
