// メールアプリの画面(MailScene)のテスト。run.sh から呼ぶ。
//
// 読み取りの部品は mail_proto_test、通信は run_net.sh の mail_net_test の担当で、ここでは
//   - アカウントが無いときの案内、生成/解放で漏れが無いこと(ASan)、入り直しても同じ数のウィジェット
//   - Microsoft のアカウント(auth = microsoft)はアプリのIDが要り、パスワードは要らない
//   - [設定] のサーバ名の読み方("host" / "host:port" / "imaps://host:port")
//   - 一覧の日付の形(今日は時刻、今年は月/日、それより前は年から)
// を見る。
#include "apps/mail/MailScene.hpp"
#include "gui/core/Widget_Functions.hpp"
#include "gui/core/Scene_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "gui/core/GFX_Functions.hpp"
#include "gui/core/Keyboard_Functions.hpp"
#include "storage/SD_Path.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstring>
#include <ctime>
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
static int pops = 0;
void SceneFunctions::Pop(){ pops++; }

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

static bool anyLabelContains(const char* part){
    for(Widget* w : WidgetFunctions::widgets){
        auto* l = dynamic_cast<Label<PICO_STR_LL>*>(w);
        if(l && strstr(l->getText()->c_str(), part) != nullptr) return true;
    }
    return false;
}

static void testScene(){
    printf("\n---- MailScene ----\n");
    OSData::SD_usable = true;
    HostSd::files.erase(PICO_Path::FILE::CFG::SYS_MAIL_CFG);

    MailScene* scene = new MailScene();
    scene->onEnter();
    const int widgets = (int)WidgetFunctions::widgets.size();
    check(widgets >= 8, "ウィジェットを作る");
    for(int i = 0; i < 5; i++) scene->onUpdate();
    check(anyLabelContains("アカウントがありません"), "アカウントが無ければ案内を出す");

    //Esc で閉じる(一覧の画面ではアプリを閉じる)
    KeyInputFunctions::Event esc;
    esc.key = KeyInputFunctions::Key::Escape;
    pops = 0;
    check(scene->onKey(esc) && pops == 1, "一覧で Esc はアプリを閉じる");

    scene->onExit();
    WidgetFunctions::ClearSceneWidgets();
    eq_int((long)WidgetFunctions::widgets.size(), 0, "片付けるとウィジェットは残らない");

    //もう一度入っても同じように作れる(Push()から戻ったとき)
    scene->onEnter();
    check((int)WidgetFunctions::widgets.size() == widgets, "入り直しても同じ数");
    scene->onExit();
    WidgetFunctions::ClearSceneWidgets();
    delete scene;

    //設定があっても(Wi-Fiが無ければ)繋ぎに行かずに案内を出す
    HostSd::files[PICO_Path::FILE::CFG::SYS_MAIL_CFG] =
        "host = imap.example.jp\nuser = me@example.jp\npassword = secret\n";
    scene = new MailScene();
    scene->onEnter();
    for(int i = 0; i < 5; i++) scene->onUpdate();
    check(!anyLabelContains("アカウントがありません"), "設定があれば案内は消える");
    check(anyLabelContains("Wi-Fiに繋がっていません"), "Wi-Fiが無ければ繋ぎに行かずに案内");
    scene->onExit();
    WidgetFunctions::ClearSceneWidgets();
    delete scene;

    //Microsoft のアカウント: アプリのIDが無ければ案内。あればパスワード無しで受け付ける
    HostSd::files[PICO_Path::FILE::CFG::SYS_MAIL_CFG] = "auth = microsoft\nuser = me@outlook.jp\n";
    scene = new MailScene();
    scene->onEnter();
    for(int i = 0; i < 5; i++) scene->onUpdate();
    check(anyLabelContains("oauth-client-id"), "Microsoft: アプリのIDが無ければ案内");
    scene->onExit();
    WidgetFunctions::ClearSceneWidgets();
    delete scene;
    HostSd::files[PICO_Path::FILE::CFG::SYS_MAIL_CFG] = "auth = microsoft\nuser = me@outlook.jp\noauth-client-id = abc\n";
    scene = new MailScene();
    scene->onEnter();
    eq_int((long)WidgetFunctions::widgets.size(), widgets, "Microsoft: ウィジェットの数は同じ");
    for(int i = 0; i < 5; i++) scene->onUpdate();
    check(!anyLabelContains("アカウントがありません") && anyLabelContains("Wi-Fiに繋がっていません"),
          "Microsoft: パスワード無しで受け付ける");
    scene->onExit();
    WidgetFunctions::ClearSceneWidgets();
    delete scene;
    HostSd::files.erase(PICO_Path::FILE::CFG::SYS_MAIL_CFG);
}

static void testHostPort(){
    printf("\n---- サーバ名 ----\n");
    FixedString<PICO_STR_M> host;
    uint16_t port = 0;
    check(MailScene::ParseHostPort("imap.gmail.com", host, port) && host == "imap.gmail.com" && port == 993, "host だけなら 993");
    check(MailScene::ParseHostPort(" mail.example.jp:143 ", host, port) && host == "mail.example.jp" && port == 143, "host:port");
    check(MailScene::ParseHostPort("imaps://imap.example.com:9993/", host, port) && host == "imap.example.com" && port == 9993,
          "imaps:// と末尾の / は落とす");
    check(!MailScene::ParseHostPort("", host, port), "空は不可");
    check(!MailScene::ParseHostPort("bad host", host, port), "空白を含む名前は不可");
    check(!MailScene::ParseHostPort("host:99999", host, port), "範囲外のポートは不可");
    check(!MailScene::ParseHostPort("host:12a", host, port), "数字でないポートは不可");
}

static void testListDate(){
    printf("\n---- 一覧の日付 ----\n");
    char buf[24];
    const time_t now = time(nullptr);
    struct tm n;
    localtime_r(&now, &n);

    //今日の 0時5分
    struct tm t = n;
    t.tm_hour = 0; t.tm_min = 5; t.tm_sec = 0;
    MailScene::FormatListDate((int64_t)mktime(&t), buf, sizeof(buf));
    eq_str(buf, "00:05", "今日は時刻");

    //今年の別の日(1月1日。今日が1月1日なら2日)
    t = n;
    t.tm_mon = 0; t.tm_mday = (n.tm_mon == 0 && n.tm_mday == 1) ? 2 : 1; t.tm_hour = 12;
    MailScene::FormatListDate((int64_t)mktime(&t), buf, sizeof(buf));
    char expect[24];
    snprintf(expect, sizeof(expect), "1/%d", t.tm_mday);
    eq_str(buf, expect, "今年は月/日");

    t = n;
    t.tm_year -= 1; t.tm_mon = 11; t.tm_mday = 31; t.tm_hour = 12;
    MailScene::FormatListDate((int64_t)mktime(&t), buf, sizeof(buf));
    snprintf(expect, sizeof(expect), "%d/12/31", n.tm_year + 1900 - 1);
    eq_str(buf, expect, "去年より前は年から");

    MailScene::FormatListDate(0, buf, sizeof(buf));
    eq_str(buf, "", "分からない日時は空");
}

int main(){
    testScene();
    testHostPort();
    testListDate();
    printf("\n%s (失敗 %d件)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
