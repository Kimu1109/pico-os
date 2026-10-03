#include <Arduino.h>

#include "functions/SD_Functions.hpp"
#include "functions/Touch_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Display_Functions.hpp"
#include "functions/Power_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/IME_Functions.hpp"
#include "functions/Keyboard_Functions.hpp"
#include "functions/Network_Functions.hpp"
#include "functions/Sound_Functions.hpp"
#include "functions/Battery_Functions.hpp"
#include "functions/Alarm_Functions.hpp"
#include "functions/Notification_Functions.hpp"
#include "functions/Screenshot_Functions.hpp"
#include "functions/Pad_Functions.hpp"
#include "functions/KeyInput_Functions.hpp"
#include "functions/CardKB_Functions.hpp"
#include "functions/Task_Functions.hpp"
#include "functions/Time_Functions.hpp"
#include "functions/Test_Functions.hpp"
#include "functions/Mem_Functions.hpp"
#include "functions/App_Functions.hpp"
#include "functions/Profiler_Functions.hpp"
#include "functions/CrashDump_Functions.hpp"
#include "functions/DevTools_Functions.hpp"
#include "lua/LuaDebugger.hpp"
#include "lua/LuaDebugScreen.hpp"

#include "gui/widgets/systems/Statusbar.hpp"
#include "gui/widgets/systems/PerfOverlay.hpp"
#include "gui/scenes/HomeScene.hpp"

#include "OS_Data.hpp"
#include <SPI.h>

//シーンをまたいで常駐させるウィジェットはオーバーレイ層に置く
static Statusbar* status;
//フレーム時間の表示(/sys/debug.cfg の perf-overlay)。全オーバーレイの一番上
static PerfOverlay* perf_overlay;
#if !defined(__EMSCRIPTEN__)
//Luaデバッガの画面。Webビルドはブラウザのメインスレッドを止められないので持たない(止まらずに続ける)
static LuaDebugScreen lua_debug_screen;
#endif

//コア1(音声専用)のスタックをヒープ側の別領域へ切り出す(arduino-pico側の弱いシンボルを上書き)。
//既定(false)だとコア0/コア1のスタックがSCRATCH_Y/SCRATCH_Xという隣接した4KBバンクに
//置かれ、コア0側が(Luaのload()での深い再帰等で)スタックを使い切ると、そのままコア1の
//スタックへあふれて壊してしまう。実際にLuaアプリでコア0のスタックオーバーフローが
//コア1の音声を巻き込み、二度と直らない雑音が鳴り続く事故が起きたため、コア1のスタックを
//ヒープ上の独立した領域(8KB)へ分離した
bool core1_separate_stack = true;

void setup() {
    pinMode(LED_BUILTIN, OUTPUT);

    PICO_GFX::Setup();
    PICO_SD::Setup();

    LogFunctions::Setup();
    //前回の起動で落ちていたら /crash/ へダンプを書く(SDとログの後、できるだけ早く)
    CrashDumpFunctions::Setup();
    CrashDumpFunctions::InstallHandlers();
    DisplayFunctions::Setup(); //display.cfgを読むのでSDより後
    PowerFunctions::Setup();   //同上(sleep-timeout)

    //以降のSetupがどれだけヒープを食うかを見るための基準点
    MemFunctions::Setup();

    PICO_Touch::Setup();
    PadFunctions::Setup();
    KeyInputFunctions::Setup();
    CardKbFunctions::Setup();   //あれば使うだけ(CardKB2。無くても何も出さない)
    PICO_Task::Setup();
    WidgetFunctions::Setup();

    //--- 常駐(オーバーレイ層): シーン遷移で破棄されない ---
    status = new Statusbar();
    WidgetFunctions::AddOverlay(status);
    //ステータスバーの右端のカメラはスクリーンショット、それ以外のタップは通知センターを開く
    status->setOnPressEnd([](){
        if(OSData::touchX < SCREEN_WIDTH - ScreenshotFunctions::kButtonWidth){
            NotificationFunctions::OpenCenter();
            return;
        }
        char path[48];
        const bool ok = ScreenshotFunctions::Capture(path, sizeof(path));
        NotificationFunctions::Content c;
        NotificationFunctions::Sanitize(c.title, ok ? "スクリーンショット" : "スクリーンショット失敗");
        NotificationFunctions::Sanitize(c.body, path);
        //通知をタップしたらスクリーンショットをビューワーで開く("file:"+パス。Notification_Sources.cppが解釈する)
        if(ok){
            char target[PICO_STR_M];
            snprintf(target, sizeof(target), "file:%s", path);
            NotificationFunctions::Sanitize(c.app, target);
        }
        c.sound = false;
        NotificationFunctions::Post(c);
    });

    NetworkFunctions::Setup();
    KeyboardFunctions::Setup(); //キーボード3種もAddOverlay()される
    IME_Functions::Setup();
    TimeFunctions::Setup();
    SoundFunctions::Setup(); //sound.cfgを読むのでSDより後
    BatteryFunctions::Setup();
    AlarmFunctions::Setup(); //alarm.cfgを読むのでSDより後

    TestFunctions::Setup();

    //ランチャに並べるアプリを登録する(一覧は App_List.cpp)
    AppFunctions::Setup();

    //通知(予約の読み込みとトースト)。予約の送り主を登録簿と突き合わせるのでAppFunctionsより後、
    //トーストを全オーバーレイの一番上に置くのでキーボードより後
    NotificationFunctions::Setup();

    //--- 開発者向けの道具(/sys/debug.cfg) ---
    perf_overlay = new PerfOverlay();
    WidgetFunctions::AddOverlay(perf_overlay); //トーストよりさらに上
    DevToolsFunctions::perf_overlay_visibility = [](bool visible){
        if(perf_overlay) perf_overlay->setVisible(visible);
    };
#if !defined(__EMSCRIPTEN__)
    LuaDebugger::SetFrontend(&lua_debug_screen);
#endif
    LuaDebugger::SetErrorReporter(&CrashDumpFunctions::SaveLuaError);
    LuaDebugger::SetActivityHook(&CrashDumpFunctions::SetLua);
    PadFunctions::extra_line_handler = &LuaDebugger::FeedSerialLine;
    DevToolsFunctions::Setup();
    //前回のクラッシュを知らせる(タップでダンプを開く)
    CrashDumpFunctions::PostPendingNotice();

    //ここまでの確保は全てOS常駐。シーンアリーナを導入する際の「永続領域」に相当する
    MemFunctions::SealPermanentBaseline();

    //--- ここから先はシーンの所有物 ---
    SceneFunctions::Setup(new HomeScene());

    pinMode(LED_BUILTIN, HIGH);

    LOG_SYS_OK("System setup has succeeded!");
}

void loop() {
    //フレームの頭: プロファイラの区切りとウォッチドッグへの「生きている」
    ProfilerFunctions::BeginFrame();
    CrashDumpFunctions::Feed();

    PICO_Touch::Update();
    //外部コントローラー(今はUSBシリアル経由のPCのキーボード)。
    //シーンのonUpdate()より前に読み、1フレームの間は同じ答えを返す。
    //同じSerialで届く物理キーボードの打鍵("key ...")もここで KeyInputFunctions の列へ積まれる
    PadFunctions::Update();
    //CardKB2の打鍵も同じ列へ積む(あれば。無いときは1秒に1回呼びかけるだけ)
    CardKbFunctions::Update();
    ProfilerFunctions::Mark(ProfilerFunctions::Section::Input);

    //操作の有無を見て自動調光を掛ける/戻す(タッチ・パッドの状態が確定した直後)
    DisplayFunctions::Update();

    //さらに操作が無ければスリープへ入る/操作で戻す(起こしたタッチはここで握りつぶす)。
    //各画面のonUpdate()が呼ぶKeepAwake()は次のフレームのここで読まれる
    PowerFunctions::Update();
    ProfilerFunctions::Mark(ProfilerFunctions::Section::Power);

    //保留中のシーン遷移をフレーム境界で適用する(ウィジェット更新より前)
    SceneFunctions::Update();
    {
        Scene* cur = SceneFunctions::Current();
        CrashDumpFunctions::SetScene(cur ? cur->getName() : nullptr);
    }
    //Luaデバッガで止まっていた間に液晶へ直接描いたので、全体を描き直す
    //(半透明のダイアログの下も。MarkDirty()だとFlushDirty()がそこを描き直さない)
    if(LuaDebugger::TakeRedrawRequest()) PICO_GFX::MarkDirtyBelow({0, 0, SCREEN_WIDTH, SCREEN_HEIGHT});

    //物理キーボードの打鍵を今の画面/開いているキー盤へ配る(遷移の適用後、ウィジェット更新の前)
    KeyInputFunctions::Update();
    ProfilerFunctions::Mark(ProfilerFunctions::Section::Scene);

    WidgetFunctions::UpdateAll();
    ProfilerFunctions::Mark(ProfilerFunctions::Section::Widgets);

    PICO_GFX::FlushDirty();
    ProfilerFunctions::Mark(ProfilerFunctions::Section::Flush);

    //現シーン滞在中のピーク使用量を追う(mallinfoを読むだけ)
    MemFunctions::Update();

    PICO_Task::Update();
    LogFunctions::Update();
    TimeFunctions::Update();
    ProfilerFunctions::Mark(ProfilerFunctions::Section::Tasks);
    NetworkFunctions::Update();
    ProfilerFunctions::Mark(ProfilerFunctions::Section::Network);
    //アンプの抜き差しの検出(音そのものは2コア目が作って流す)
    SoundFunctions::Update();
    //VSYS電圧の読み取り(内部でkSampleIntervalMsごとに間引く)
    BatteryFunctions::Update();
    //アラームの時刻の見張り(時計アプリを閉じていても鳴らす)。TimeFunctions::Update()より後
    AlarmFunctions::Update();
    //通知の予約の見張りとトーストの出し入れ。TimeFunctions::Update()より後
    NotificationFunctions::Update();
    //プロファイラの集計をシリアルへ(perf-log = true のときだけ)
    DevToolsFunctions::Update();
    ProfilerFunctions::Mark(ProfilerFunctions::Section::Services);

    //ここまでが仕事の時間(この後の休みはフレーム時間にだけ入る)
    ProfilerFunctions::EndWork();

    //スリープ中だけ少し休んでCPUを寝かせる(それ以外は何もしない)
    PowerFunctions::IdleWait();
}

//--- 2コア目: 音声専用 ---
//音源の計算とI2Sへの書き込みだけをする。1コア目の描画やTLSのハンドシェイクで音が途切れないように
void setup1() {
    SoundFunctions::SetupCore1();
}

void loop1() {
    //I2Sのバッファが埋まっていて何もすることが無ければ少し休む(バッファは約23ms分ある)。
    //スリープ中で何も鳴っていなければI2Sも止まっているので、長めに休む
    if(!SoundFunctions::LoopCore1()) delay(SoundFunctions::IdleDelayMs());
}
