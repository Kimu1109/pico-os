// バックグラウンドの仕事の検証。
//
// 1. BackgroundFunctions: 登録・重複・入り切りと background.cfg・印・望む周波数
// 2. MusicService: アプリの外での再生(曲の終わりで次へ・読めない曲を飛ばす・リピートしない・
//    他のアプリに音を取られたら止まる・周波数の切り替え・[停止]で状態を返す)。
//    SoundFunctions は本物で、2コア目は同じスレッドで交互に回す(music_test と同じ)
// 3. ChatWatch: 部屋の一覧から知らせる部屋を選ぶ(通知は本物の NotificationFunctions)・設定が無いときの状態
// 4. LuaBackground: app.cfg からの登録・権限・使い捨てのエンジンで1回動かす・使える pico.* の絞り込み・
//    タイマーと pico.finish・時間切れ・エラーはダイアログを出さない・前に出ているアプリは見送る・
//    入り切りの保存・アプリが無くなったら外す
#include "system/Background_Functions.hpp"
#include "hal/Sound_Functions.hpp"
#include "system/Notification_Functions.hpp"
#include "services/App_Functions.hpp"
#include "gui/core/Widget_Functions.hpp"
#include "gui/core/GFX_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "gui/core/Keyboard_Functions.hpp"
#include "gui/core/Scene_Functions.hpp"
#include "apps/music/Music_Service.hpp"
#include "apps/chat/Chat_Watch.hpp"
#include "lua/LuaBackground.hpp"
#include "lua/LuaEngine.hpp"
#include "gui/scenes/LuaScene.hpp"
#include "gui/widgets/interfaces/ITextInputTarget.hpp"
#include "storage/SD_Path.hpp"
#include "consts.hpp"
#include "OS_Data.hpp"

#include <Arduino.h>
#include <I2S.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// ---- モック(lua_ext_test と同じ) ----
void PICO_GFX::MarkDirty(const Rect&){}
void PICO_GFX::Setup(){}
void PICO_GFX::FlushDirty(){}
void PICO_GFX::DrawDialogBackground(){}
void LogFunctions::Log(LogType, const char* fmt, ...){
    if(!getenv("LUA_TEST_VERBOSE")) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}
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

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

static std::string FileText(const char* path){
    auto it = HostSd::files.find(path);
    return it == HostSd::files.end() ? std::string() : it->second;
}

// ================================================================
// 1. 登録簿
// ================================================================
class FakeService : public BackgroundService {
    public:
        const char* k; bool toggle; bool on = true; bool act = false; uint32_t rate = 0;
        IconID ic = IconID::Refresh;
        int updates = 0; int stops = 0;
        FakeService(const char* key, bool t) : k(key), toggle(t) {}
        const char* key() const override { return k; }
        const char* name() const override { return k; }
        IconID icon() const override { return ic; }
        void update(uint32_t) override { updates++; }
        bool active() const override { return toggle ? on : act; }
        void status(FixedString<PICO_STR_LL>& out) const override { out.assign("x"); }
        bool toggleable() const override { return toggle; }
        bool enabled() const override { return on; }
        void setEnabled(bool v) override { on = v; }
        void stop() override { stops++; act = false; }
        uint32_t preferredSampleRate() const override { return rate; }
};

static void TestRegistry(){
    printf("--- BackgroundFunctions ---\n");
    BackgroundFunctions::Test::Reset();
    HostSd::files.erase(PICO_Path::FILE::CFG::SYS_BACKGROUND_CFG);
    FakeService a("a", false), b("b", true), b2("b", true);
    const uint32_t rev0 = BackgroundFunctions::Revision();
    check(BackgroundFunctions::Add(&a) && BackgroundFunctions::Add(&b), "登録");
    check(!BackgroundFunctions::Add(&b2), "同じキーは断る");
    check(BackgroundFunctions::Count() == 2 && BackgroundFunctions::Find("b") == &b, "数と引き当て");
    check(BackgroundFunctions::Revision() != rev0, "登録で版が進む");
    check(BackgroundFunctions::IndicatorIcon() == IconID::Refresh, "定期の仕事だけなら回る矢印の印");
    a.act = true; a.ic = IconID::Music; a.rate = 44100;
    check(BackgroundFunctions::IndicatorIcon() == IconID::Music, "今だけの仕事があればその絵");
    check(BackgroundFunctions::PreferredSampleRate() == 44100, "動いている仕事の望む周波数");
    a.act = false;
    check(BackgroundFunctions::PreferredSampleRate() == 0, "止まれば望まない");
    BackgroundFunctions::UpdateAt(1);
    check(a.updates == 1 && b.updates == 1, "毎フレーム全部の update()");

    BackgroundFunctions::SetEnabled(&b, false);
    check(!b.on && !BackgroundFunctions::AnyActive(), "切ると動いていない");
    check(FileText(PICO_Path::FILE::CFG::SYS_BACKGROUND_CFG).find("b=off") != std::string::npos, "background.cfg に覚える");
    check(!BackgroundFunctions::ConfigEnabled("b", true) && BackgroundFunctions::ConfigEnabled("zz", true), "読み戻し(無いキーは既定)");
    BackgroundFunctions::Remove(&b);
    FakeService b3("b", true);
    check(BackgroundFunctions::Add(&b3, true) && !b3.on, "登録し直すと覚えた入り切りを当てる");
    BackgroundFunctions::SetEnabled(&b3, true);
    check(BackgroundFunctions::ConfigEnabled("b", false), "入れ直しも覚える");
    BackgroundFunctions::Test::Reset();
    check(BackgroundFunctions::Count() == 0, "全部外す");
}

// ================================================================
// 2. MusicService
// ================================================================
static int ReadHook(int pin){ return pin == AUDIO_DETECT ? LOW : -1; }
static unsigned long g_now = 1000;
static void Step(int frames = 1){
    for(int i = 0; i < frames; i++){
        if(I2S::last) I2S::last->consume(I2S::last->queued.size());
        g_now += 5;
        PicoHostClock::now = g_now;
        SoundFunctions::UpdateAt(g_now);
        SoundFunctions::Core1StepAt(g_now);
        BackgroundFunctions::UpdateAt(g_now);
    }
}
// 条件が満たされるまで(最大 n フレーム)回す
template<class F> static bool StepUntil(F cond, int n = 4000){
    for(int i = 0; i < n; i++){
        if(cond()) return true;
        Step();
    }
    return cond();
}

static void TestMusic(){
    printf("--- MusicService ---\n");
    HostGpio::read_hook = &ReadHook;
    OSData::SD_usable = true;
    SoundFunctions::SetupAt(g_now);
    SoundFunctions::Core1StepAt(g_now);
    SoundFunctions::RequestSampleRate(SoundFunctions::kSampleRateHigh);
    Step(3);
    BackgroundFunctions::Test::Reset();
    MusicService::Test::Reset();
    BackgroundFunctions::Add(MusicService::Service());

    HostSd::files["/music/a.mml"] = "#title A\nA t240 l16 c";
    HostSd::files["/music/b.mml"] = "#title B\nA t240 l16 d";
    HostSd::files["/music/bad.mml"] = "A c v99";
    MusicService::Track t[3];
    t[0].name.assign("a.mml"); t[1].name.assign("bad.mml"); t[2].name.assign("b.mml");

    check(!BackgroundFunctions::AnyActive(), "鳴らす前は印なし");
    check(MusicService::StartPlaylist(t, 3, nullptr, 0, -1, "すべての曲", 0), "鳴らし始める");
    check(MusicService::Active() && MusicService::Current() == 0, "1曲目が鳴っている");
    check(BackgroundFunctions::IndicatorIcon() == IconID::Music, "印は音符");
    check(BackgroundFunctions::PreferredSampleRate() == SoundFunctions::kSampleRateHigh, "鳴っている間は44100Hzを望む");

    //画面が無くても(BackgroundFunctions::UpdateAt だけで)曲の終わりで次へ。読めない曲は飛ばす
    check(StepUntil([](){ return MusicService::Current() == 2; }), "曲の終わりで次へ進み、読めない曲は飛ばす");
    check((MusicService::FailedMask() & 2) && MusicService::ErrorShown() && !MusicService::ErrorSticky(), "飛ばした理由を出す");
    check(MusicService::Active(), "3曲目が鳴っている");
    check(StepUntil([](){ return MusicService::Finished(); }), "リピートしないなら最後で止まる");
    check(!MusicService::Active() && !BackgroundFunctions::AnyActive(), "止まれば印が消える");

    //利用者が選んだ読めない曲は止まって理由を出し続ける
    MusicService::PlayTrack(1);
    check(!MusicService::Active() && MusicService::ErrorSticky(), "選んだ曲が読めなければ止まって理由を出す");

    //一時停止/再開
    MusicService::PlayTrack(0);
    MusicService::TogglePlay();
    Step(2);
    check(MusicService::Paused() && MusicService::Active(), "一時停止(動いている扱いのまま)");
    MusicService::TogglePlay();
    check(!MusicService::Paused(), "再開");

    //他のアプリが曲を鳴らしたら自分の曲は止まった扱い(次へ進まず、相手の曲も止めない)
    const char* other = "A L c d e";
    check(SoundFunctions::MusicPlayText(other, strlen(other)), "他のアプリが曲を鳴らす");
    Step(2);
    check(MusicService::Interrupted() && !MusicService::Active(), "取られたら止まった扱い");
    check(SoundFunctions::MusicPlaying(), "相手の曲はそのまま");
    Step(50);
    check(MusicService::Current() == 0, "次の曲へ進まない");
    SoundFunctions::MusicStop();
    Step(2);

    //続きから鳴らし直せる。周波数の切り替えでも止まった扱い
    MusicService::TogglePlay();
    check(MusicService::Active() && !MusicService::Interrupted(), "続きから鳴らし直す");
    SoundFunctions::RequestSampleRate(SoundFunctions::kSampleRateLow);
    Step(2);
    check(MusicService::Interrupted(), "周波数の切り替えで止まった扱い");
    SoundFunctions::RequestSampleRate(SoundFunctions::kSampleRateHigh);

    //ミックス・リピートは music.cfg に覚える
    MusicService::SetShuffle(true);
    MusicService::CycleRepeat();
    check(FileText(PICO_Path::FILE::CFG::SYS_MUSIC_CFG).find("shuffle=true") != std::string::npos
          && FileText(PICO_Path::FILE::CFG::SYS_MUSIC_CFG).find("repeat=all") != std::string::npos, "music.cfg に覚える");

    //リピート全曲なら一巡しても鳴り続ける
    MusicService::Track ok2[2];
    ok2[0].name.assign("a.mml"); ok2[1].name.assign("b.mml");
    MusicService::StartPlaylist(ok2, 2, nullptr, 0, 3, "旅", -1);
    int changes = 0, last = MusicService::Current();
    for(int i = 0; i < 4000 && changes < 3; i++){
        Step();
        if(MusicService::Current() != last){ changes++; last = MusicService::Current(); }
    }
    check(changes >= 3 && MusicService::Active(), "リピート全曲は一巡しても続く");

    //SDをPCへ貸している(USBドライブ)間は、曲の終わりで止まる(次の曲を「読めない」にしない)
    OSData::SD_usable = false;
    check(StepUntil([](){ return !MusicService::Active(); }), "SDが使えなければ曲の終わりで止まる");
    check(MusicService::FailedMask() == 0 && !MusicService::ErrorShown(), "読めない曲の扱いにしない");
    OSData::SD_usable = true;
    MusicService::TogglePlay();
    check(MusicService::Active(), "SDが戻れば続きから鳴らせる");

    //main.cpp と同じく、画面が周波数を望まない(ホーム画面・動画アプリ)ときはバックグラウンドの望みを毎フレーム渡す
    {
        auto main_step = [](int frames){
            for(int i = 0; i < frames; i++){
                SoundFunctions::RequestSampleRate(BackgroundFunctions::PreferredSampleRate());
                Step();
            }
        };
        Step(5);    //前の曲の置き場が空くのを待つ
        MusicService::StartPlaylist(ok2, 2, nullptr, 0, 3, "旅", 0);
        main_step(3);
        check(MusicService::Active() && SoundFunctions::SampleRate() == SoundFunctions::kSampleRateHigh, "ホーム画面でも44100Hzで鳴り続ける");
        //他のアプリ(周波数を望まない)が曲を鳴らす: 取った側の音が周波数の切り替えで止まらない
        main_step(5);    //曲の置き場が空くのを待つ
        check(SoundFunctions::MusicPlayText(other, strlen(other)), "他のアプリが曲を鳴らす(ホーム画面の要求のまま)");
        main_step(5);
        check(MusicService::Interrupted() && SoundFunctions::MusicPlaying(), "取った側の音は鳴り続ける");
        check(SoundFunctions::SampleRate() == SoundFunctions::kSampleRateHigh, "取った側が鳴っている間は周波数を変えない");
        SoundFunctions::MusicStop();
        main_step(3);
        check(SoundFunctions::SampleRate() == SoundFunctions::kSampleRateLow, "鳴り終われば手放して設定の22050Hzへ");
        //ホーム画面のウィジェットの再生ボタン: 鳴らした直後に周波数の切り替えで止まらない
        MusicService::TogglePlay();
        main_step(10);
        check(MusicService::Active() && !MusicService::Interrupted(), "ウィジェットから鳴らし直しても止まらない");
        check(SoundFunctions::SampleRate() == SoundFunctions::kSampleRateHigh, "鳴らす前に44100Hzへ切り替える");
        //画面がはっきり22050Hzを望んでいれば、それを上書きしない
        MusicService::Stop();
        SoundFunctions::RequestSampleRate(SoundFunctions::kSampleRateLow);
        Step(2);
        MusicService::TogglePlay();
        check(SoundFunctions::SampleRate() == SoundFunctions::kSampleRateLow && MusicService::Active(), "画面の要求があればその周波数で鳴らす");
        SoundFunctions::RequestSampleRate(SoundFunctions::kSampleRateHigh);
        Step(2);
    }

    //一覧の[停止]で止まり、状態を返す
    MusicService::Service()->stop();
    check(!MusicService::HasQueue() && !SoundFunctions::MusicPlaying(), "[停止]で止めて状態を返す");
    MusicService::SetShuffle(false);
    MusicService::CycleRepeat(); MusicService::CycleRepeat();    //All → One → Off
    BackgroundFunctions::Test::Reset();
}

// ================================================================
// 3. ChatWatch
// ================================================================
static ChatProto::Room R(uint32_t id, const char* name, uint32_t last, uint32_t unread){
    ChatProto::Room r;
    r.id = id; r.name.assign(name); r.last_id = last; r.unread = unread;
    return r;
}

static void TestChat(){
    printf("--- ChatWatch ---\n");
    ChatWatch::Test::Reset();
    const int h0 = NotificationFunctions::HistoryCount();
    ChatProto::Room rooms[3] = { R(1, "雑談", 10, 2), R(2, "連絡", 5, 0), R(3, "趣味", 7, 1) };
    check(ChatWatch::OnRooms(rooms, 3) == 2, "最初は未読のある部屋を全部知らせる");
    check(NotificationFunctions::HistoryCount() == h0 + 2, "通知が出る");
    const NotificationFunctions::Entry* e = NotificationFunctions::HistoryAt(0);
    check(e && strcmp(e->content.app.c_str(), "チャット") == 0, "タップでチャットアプリを開く");
    check(ChatWatch::OnRooms(rooms, 3) == 0, "同じ一覧なら知らせない");
    rooms[0].last_id = 11; rooms[0].unread = 3;
    rooms[1].last_id = 6;  rooms[1].unread = 0;     //読んだ後に誰かが書いた(既読)
    check(ChatWatch::OnRooms(rooms, 3) == 1, "新しい発言の来た未読の部屋だけ知らせる");

    //設定が無ければ取りに行かず、理由を出す
    HostSd::files.erase(PICO_Path::FILE::CFG::SYS_CHAT_CFG);
    BackgroundFunctions::Test::Reset();
    BackgroundFunctions::Add(ChatWatch::Service());
    BackgroundFunctions::UpdateAt(g_now += 10);
    FixedString<PICO_STR_LL> st;
    ChatWatch::Service()->status(st);
    check(strstr(st.c_str(), "設定されていません") != nullptr, "設定が無ければ理由を出す");
    check(!ChatWatch::Service()->active(), "設定が無ければ動いていない扱い");
    BackgroundFunctions::Test::Reset();
}

// ================================================================
// 4. LuaBackground
// ================================================================
static const char* kDir = "/lua/apps/bg";
static void PutApp(const char* cfg, const char* script){
    HostSd::files["/lua/apps/bg/main.lua"] = "-- 画面";
    HostSd::files["/lua/apps/bg/app.cfg"] = cfg;
    HostSd::files["/lua/apps/bg/bg.lua"] = script;
}
static BackgroundService* Job(){ return BackgroundFunctions::Find("lua:bg"); }
static const char* fg_script = nullptr;

// 予定の時刻を過ぎるまで進めて1回動かす
static void RunDue(){
    BackgroundService* j = Job();
    if(j) j->runNow();
    PicoHostClock::now = (g_now += 10);
    BackgroundFunctions::UpdateAt(g_now);
}

static void TestLua(){
    printf("--- LuaBackground ---\n");
    BackgroundFunctions::Test::Reset();
    LuaBackground::Test::Reset();
    LuaBackground::foreground_script = [](){ return fg_script; };
    OSData::SD_usable = true;
    HostSd::files.erase(PICO_Path::FILE::CFG::SYS_BACKGROUND_CFG);

    const char* cfg = "permission_background = true\npermission_notify = true\nbackground = bg.lua\nbackground_interval = 60\n";
    PutApp(cfg,
        "pico.sd_write('/lua/apps/bg/out.txt', tostring(pico.is_background()) .. ',' .. tostring(pico.create == nil)"
        " .. ',' .. tostring(pico.iso == nil) .. ',' .. tostring(pico.sd_write ~= nil))\n"
        "pico.notify{title='天気', body='晴れ'}\n");
    check(AppFunctions::Register("天気", IconID::AppBox, &MakeLuaAppScene, "/lua/apps/bg/main.lua"), "アプリを登録");

    check(LuaBackground::Refresh() == 1 && Job(), "app.cfg の background で仕事になる");
    check(strcmp(Job()->name(), "天気") == 0 && Job()->toggleable() && Job()->active(), "名前はアプリの名前、入っている");
    PicoHostClock::now = (g_now += 10);
    BackgroundFunctions::UpdateAt(g_now);
    check(!HostSd::files.count("/lua/apps/bg/out.txt"), "起動直後は待つ");

    const int h0 = NotificationFunctions::HistoryCount();
    RunDue();
    check(FileText("/lua/apps/bg/out.txt") == "true,true,true,true", "動いた: is_background・画面の pico.* と pico.iso は無い・SDは使える");
    check(NotificationFunctions::HistoryCount() == h0 + 1, "通知を出せる");
    check(LuaBackground::RunningEngine() == nullptr, "終わったらエンジンを捨てる");
    FixedString<PICO_STR_LL> st;
    Job()->status(st);
    check(strstr(st.c_str(), "1回") != nullptr, "回数を状態に出す");

    //タイマーが残っている間はエンジンを持ち、鳴ったら終わる
    PutApp(cfg, "pico.after(100, function() pico.sd_write('/lua/apps/bg/t.txt', 'done') end)");
    RunDue();
    check(LuaBackground::RunningEngine() != nullptr && !HostSd::files.count("/lua/apps/bg/t.txt"), "タイマーを待つ");
    for(int i = 0; i < 20; i++){ PicoHostClock::now = (g_now += 10); BackgroundFunctions::UpdateAt(g_now); }
    check(FileText("/lua/apps/bg/t.txt") == "done" && LuaBackground::RunningEngine() == nullptr, "タイマーが鳴って終わる");

    //pico.finish() で残りのタイマーごと終える
    PutApp(cfg, "pico.every(1000, function() end) pico.finish()");
    RunDue();
    check(LuaBackground::RunningEngine() == nullptr, "pico.finish() で終える");

    //終わらなければ時間切れ
    PutApp(cfg, "pico.every(1000, function() end)");
    RunDue();
    check(LuaBackground::RunningEngine() != nullptr, "every が残っていれば動き続ける");
    for(int i = 0; i < 40; i++){ PicoHostClock::now = (g_now += 1000); BackgroundFunctions::UpdateAt(g_now); }
    check(LuaBackground::RunningEngine() == nullptr, "時間切れで止める");
    Job()->status(st);
    check(strstr(st.c_str(), "時間切れ") != nullptr, "時間切れを状態に出す");

    //エラーはダイアログを出さず状態へ
    const size_t dialogs = WidgetFunctions::dialog_roots.size();
    PutApp(cfg, "local x = nil; x.y = 1");
    RunDue();
    check(WidgetFunctions::dialog_roots.size() == dialogs, "エラーでダイアログを出さない");
    Job()->status(st);
    check(strstr(st.c_str(), "失敗") != nullptr, "エラーを状態に出す");

    //画面の pico.* を呼べばエラー(nil を呼んだ)
    PutApp(cfg, "pico.create('Button')");
    RunDue();
    Job()->status(st);
    check(strstr(st.c_str(), "失敗") != nullptr, "画面の pico.* は使えない");

    //そのアプリが前に出ていれば見送る
    HostSd::files.erase("/lua/apps/bg/out.txt");
    PutApp(cfg, "pico.sd_write('/lua/apps/bg/out.txt', 'x')");
    fg_script = "/lua/apps/bg/main.lua";
    RunDue();
    check(!HostSd::files.count("/lua/apps/bg/out.txt"), "アプリが前に出ていれば見送る");
    Job()->status(st);
    check(strstr(st.c_str(), "見送り") != nullptr, "見送った理由");
    fg_script = nullptr;
    RunDue();
    check(FileText("/lua/apps/bg/out.txt") == "x", "閉じれば動く");

    //切ると動かず、background.cfg に覚える
    BackgroundFunctions::SetEnabled(Job(), false);
    HostSd::files.erase("/lua/apps/bg/out.txt");
    RunDue();
    check(!HostSd::files.count("/lua/apps/bg/out.txt"), "切ると動かない");
    check(FileText(PICO_Path::FILE::CFG::SYS_BACKGROUND_CFG).find("lua:bg=off") != std::string::npos, "切ったことを覚える");
    BackgroundFunctions::SetEnabled(Job(), true);

    //権限が無ければ・フォルダの外を指していれば仕事にしない
    PutApp("background = bg.lua\n", "");
    check(LuaBackground::Refresh() == 0 && !Job(), "permission_background が無ければ動かさない");
    PutApp("permission_background = true\nbackground = ../x.lua\n", "");
    check(LuaBackground::Refresh() == 0, "フォルダの外のスクリプトは断る");

    //アプリが無くなったら外す
    PutApp(cfg, "");
    check(LuaBackground::Refresh() == 1, "戻すと仕事になる");
    AppFunctions::Unregister(AppFunctions::FindByArg("/lua/apps/bg/main.lua"));
    check(LuaBackground::Refresh() == 0 && !Job(), "アプリが無くなったら外す");
    (void)kDir;
    LuaBackground::Test::Reset();
    BackgroundFunctions::Test::Reset();
}

int main(){
    setvbuf(stdout, nullptr, _IONBF, 0);
    OSData::SD_usable = true;
    TestRegistry();
    TestMusic();
    TestChat();
    TestLua();
    printf("\n%s (%d件の失敗)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
