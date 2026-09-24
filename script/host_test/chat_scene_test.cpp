// チャットアプリ(ChatScene / ChatLogView)のGUI配線のテスト。run.sh から呼ぶ。
//
// 応答の読み取りは chat_proto_test、実際の通信は run_net.sh の chat_net_test の担当で、ここでは
//   - ChatLogView の折り返し(幅を超えない/改行で切る/必ず進む)
//   - ChatLogView が発言の高さを id ごとに覚え、新着1件で全件を測り直さないこと
//   - 一番下を見ている間は新着に付いていき、遡って読んでいる間は動かないこと
//   - ChatScene が設定の無い/足りない状態で案内を出し、ウィジェットを解放漏れなく片付けること(ASan)
// を見る。
#include "gui/scenes/ChatScene.hpp"
#include "gui/widgets/apps/ChatLogView.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Keyboard_Functions.hpp"
#include "functions/Font_Functions.hpp"
#include "storage/SD_Path.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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
void KeyboardFunctions::HideAll(){}

static int pop_calls = 0;
void SceneFunctions::Pop(){ pop_calls++; }

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

// ---- 折り返し ----
static void testWrap(){
    FontFn::SetSmall();
    const int max_w = 100;
    const std::string text = "あいうえおかきくけこさしすせそ abc defghijklmnop\n次の行";
    const int len = (int)text.size();

    int pos = 0, lines = 0;
    bool within = true, progressed = true, utf8_ok = true;
    std::vector<std::string> out;
    while(pos < len && lines < 100){
        int next = pos;
        const int end = ChatLogView::WrapLine(text.c_str(), len, pos, max_w, next);
        const std::string line = text.substr(pos, end - pos);
        out.push_back(line);
        if(OSData::frame->textWidth(line.c_str()) > max_w) within = false;
        if(next <= pos) progressed = false;
        //行の終わりがUTF-8の文字の途中に来ていないか
        if(end < len && ((uint8_t)text[end] & 0xC0) == 0x80) utf8_ok = false;
        pos = next;
        lines++;
    }
    check(within, "折り返し: どの行も幅を超えない");
    check(progressed, "折り返し: 必ず1文字以上進む");
    check(utf8_ok, "折り返し: 文字の途中で切らない");
    check(!out.empty() && out.back() == "次の行", "折り返し: 改行で切り、改行そのものは行に含めない");

    //幅より広い1文字でも無限ループにならない
    int next = 0;
    const char* wide = "あ";
    const int end = ChatLogView::WrapLine(wide, 3, 0, 1, next);
    eq_int(end, 3, "折り返し: 幅に1文字も入らなくても1文字は置く");
    FontFn::SetDefault();
}

// ---- ChatLogView ----
struct FakeSource : ChatProto::IMessageSource {
    std::vector<ChatProto::Message> msgs;
    mutable int reads = 0;
    int messageCount() const override { return (int)msgs.size(); }
    const ChatProto::Message& messageAt(int i) const override { reads++; return msgs[i]; }
    void add(uint32_t id, const char* name, const char* text){
        ChatProto::Message m;
        m.id = id;
        m.epoch = 1790000000u + id;
        m.name.assign(name);
        m.text.assign(text);
        msgs.push_back(m);
    }
};

struct LogProbe : ChatLogView {
    using ChatLogView::ChatLogView;
    bool dirty() const { return needs_redraw; }
};

static void testLogView(){
    FakeSource src;
    LogProbe view(0, 20, 234, 200);
    view.setSource(&src);
    view.setPlaceholder("読み込み中…");
    view.refresh();
    view.render();
    check(!view.dirty(), "発言0件でも描ける");

    for(uint32_t i = 1; i <= 20; i++) src.add(i, (i % 2) ? "ありす" : "ぼぶ", "長めの発言です。折り返しが何行か要るくらいの長さにしておきます。");
    view.refresh();
    view.render();
    check(!view.dirty(), "20件を描ける");

    check(view.isFollowingBottom(), "開いた直後は一番下にいる");
    eq_int(view.measuredCount(), 20, "20件を1回ずつ測る");

    //一番下を見ている → 新着に付いていく
    const int bottom_before = view.getScrollY();
    src.add(21, "ありす", "新着");
    view.refresh();
    view.render();
    check(view.getScrollY() > bottom_before, "一番下を見ていれば新着に付いていく");
    eq_int(view.measuredCount(), 21, "新着1件で測るのはその1件だけ");

    //遡る: 下へなぞる(中身が下がる=上の発言が見える)
    OSData::touchY = 50;
    view.causeOnPressStart();
    OSData::touchY = 150;
    view.causeOnPressMove();
    check(view.dirty(), "なぞるとスクロールして描き直す");
    check(!view.isFollowingBottom(), "遡ると一番下から外れる");
    view.render();

    const int reading = view.getScrollY();
    src.add(22, "ぼぶ", "遡っている間の新着");
    view.refresh();
    view.render();
    eq_int(view.getScrollY(), reading, "遡っている間の新着ではスクロールしない");

    //一番下へ戻すと、また付いていく
    view.scrollToBottom();
    const int bottom2 = view.getScrollY();
    src.add(23, "ありす", "もう1件");
    view.refresh();
    check(view.getScrollY() > bottom2, "一番下へ戻せばまた付いていく");

    //並びの先頭が押し出されても(リングバッファの古いほうが消えても)描ける
    src.msgs.erase(src.msgs.begin(), src.msgs.begin() + 5);
    view.refresh();
    view.render();
    check(!view.dirty(), "古い発言が押し出されても描ける");

    check(ChatLogView::NameColor("ありす") == ChatLogView::NameColor("ありす"), "同じ名前は同じ色");
}

// ---- ChatScene ----
static const char* statusText(){
    //状態の1行は Label<PICO_STR_LL>(タイトルは Label<PICO_STR_M>)
    const char* found = "";
    for(Widget* w : WidgetFunctions::widgets){
        if(w->getWidgetType() != WidgetType::Label) continue;
        if(w->getLocalRect().y < 60) continue; //上部のタイトルは除く
        found = static_cast<Label<PICO_STR_LL>*>(w)->getText()->c_str();
    }
    return found;
}

static void testScene(){
    auto* scene = new ChatScene();

    OSData::SD_usable = false;
    scene->onEnter();
    check(!WidgetFunctions::widgets.empty(), "onEnter(): ウィジェットが並ぶ");
    check(strcmp(statusText(), "SDカードがありません") == 0, "SDが無ければその旨を出す");
    for(int i = 0; i < 5; i++) scene->onUpdate(); //設定が無い間は何もしない(落ちない)
    scene->onExit();
    WidgetFunctions::ClearSceneWidgets();
    check(WidgetFunctions::widgets.empty(), "onExit()後: ウィジェットが全て解放される");

    OSData::SD_usable = true;
    HostSd::files.erase(PICO_Path::FILE::CFG::SYS_CHAT_CFG);
    scene->onEnter();
    check(strcmp(statusText(), "/sys/chat.cfg がありません") == 0, "chat.cfg が無ければ置き場所を案内する");
    scene->onExit();
    WidgetFunctions::ClearSceneWidgets();

    HostSd::files[PICO_Path::FILE::CFG::SYS_CHAT_CFG] = "server = http://127.0.0.1:9\n";
    scene->onEnter();
    check(strstr(statusText(), "token") != nullptr, "token が無ければ書くよう案内する");
    scene->onExit();
    WidgetFunctions::ClearSceneWidgets();

    HostSd::files[PICO_Path::FILE::CFG::SYS_CHAT_CFG] = "server = ftp://x\ntoken = abc\n";
    scene->onEnter();
    check(strstr(statusText(), "URL") != nullptr, "server が http(s) でなければ案内する");
    scene->onExit();
    WidgetFunctions::ClearSceneWidgets();

    HostSd::files[PICO_Path::FILE::CFG::SYS_CHAT_CFG] = "server = http://127.0.0.1:9\ntoken = ab cd\n";
    scene->onEnter();
    check(strstr(statusText(), "token") != nullptr, "token に使えない文字があれば案内する");

    //一覧で「戻る」を押すとランチャへ戻る
    Button* back = nullptr;
    for(Widget* w : WidgetFunctions::widgets){
        if(w->getWidgetType() == WidgetType::Button && w->getLocalRect().x < 10) back = static_cast<Button*>(w);
    }
    check(back != nullptr, "戻るボタンがある");
    if(back) back->causeOnPressEnd();
    eq_int(pop_calls, 1, "一覧で戻る → Pop()");
    scene->onExit();
    WidgetFunctions::ClearSceneWidgets();
    HostSd::files.erase(PICO_Path::FILE::CFG::SYS_CHAT_CFG);
    OSData::SD_usable = false;

    delete scene;
}

int main(){
    testWrap();
    testLogView();
    testScene();

    printf("\n%s (失敗 %d件)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
