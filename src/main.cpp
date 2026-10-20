#include <Arduino.h>

#include "hal/SD_Functions.hpp"
#include "hal/Touch_Functions.hpp"
#include "hal/TouchCalib_Functions.hpp"
#include "gui/core/GFX_Functions.hpp"
#include "services/Display_Functions.hpp"
#include "services/Power_Functions.hpp"
#include "gui/core/Widget_Functions.hpp"
#include "gui/core/Scene_Functions.hpp"
#include "ime/IME_Functions.hpp"
#include "gui/core/Keyboard_Functions.hpp"
#include "hal/Network_Functions.hpp"
#include "hal/Sound_Functions.hpp"
#include "hal/Mic_Functions.hpp"
#include "hal/Battery_Functions.hpp"
#include "services/Alarm_Functions.hpp"
#include "services/Timer_Functions.hpp"
#include "services/Lock_Functions.hpp"
#include "system/Notification_Functions.hpp"
#include "services/Screenshot_Functions.hpp"
#include "system/Pad_Functions.hpp"
#include "system/KeyInput_Functions.hpp"
#include "hal/CardKB_Functions.hpp"
#include "hal/WiiClassic_Functions.hpp"
#include "task/Task_Functions.hpp"
#include "system/Time_Functions.hpp"
#include "devtools/Test_Functions.hpp"
#include "system/Mem_Functions.hpp"
#include "services/App_Functions.hpp"
#include "devtools/Profiler_Functions.hpp"
#include "devtools/CrashDump_Functions.hpp"
#include "devtools/DevTools_Functions.hpp"
#include "services/Firmware_Functions.hpp"
#include "devtools/HwTest_Functions.hpp"
#include "services/PowerMenu_Functions.hpp"
#include "system/Background_Functions.hpp"
#include "lua/LuaDebugger.hpp"
#include "lua/LuaDebugScreen.hpp"

#include "gui/widgets/systems/Statusbar.hpp"
#include "gui/widgets/systems/PerfOverlay.hpp"
#include "apps/home/HomeScene.hpp"
#include "apps/home/Home_Widgets.hpp"
#include "services/DeviceKey_Functions.hpp"

#include "OS_Data.hpp"
#include <SPI.h>

//シーンをまたいで常駐させるウィジェットはオーバーレイ層に置く
static Statusbar* status;
//ステータスバーを押し始めた時刻と、長押しで電源メニューを開いたか(開いたら離したときのタップは無視する)
static unsigned long status_press_ms = 0;
static bool status_long_pressed = false;
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
    //OTPの端末の鍵。暗号化された設定(wifi.cfg・lock.cfg等)を読むものより前に
    DeviceKeyFunctions::Setup();
    DisplayFunctions::Setup(); //display.cfgを読むのでSDより後
    PowerFunctions::Setup();   //同上(sleep-timeout)

    //以降のSetupがどれだけヒープを食うかを見るための基準点
    MemFunctions::Setup();

    PICO_Touch::Setup();
    TouchCalibFunctions::Setup();
    PadFunctions::Setup();
    KeyInputFunctions::Setup();
    CardKbFunctions::Setup();   //あれば使うだけ(CardKB2。無くても何も出さない)
    WiiClassicFunctions::Setup();   //同じI2Cのバス。あれば外部コントローラーとして使う
    PICO_Task::Setup();
    WidgetFunctions::Setup();

    //--- 常駐(オーバーレイ層): シーン遷移で破棄されない ---
    status = new Statusbar();
    WidgetFunctions::AddOverlay(status);
    //ステータスバーの右端のカメラはスクリーンショット、それ以外のタップは通知センターを開く。
    //長押し(PowerMenuFunctions::kLongPressMs)は電源メニュー(loop()で見張る)
    status->setOnPressStart([](){
        status_press_ms = millis();
        status_long_pressed = false;
    });
    status->setOnPressEnd([](){
        if(status_long_pressed) return;
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
    MicFunctions::Setup();   //mic.cfg(マイクのI2Sは録音/メーターを使うときだけ2コア目が開始する)
    BatteryFunctions::Setup();
    AlarmFunctions::Setup(); //alarm.cfgを読むのでSDより後
    TimerFunctions::Setup(); //タイマー/ストップウォッチ(時計アプリを閉じても動く)

    TestFunctions::Setup();

    //ランチャに並べるアプリを登録する(一覧は App_List.cpp)
    AppFunctions::Setup();
    //ホーム画面のウィジェット(/sys/home.cfg)。並びはアプリの並び(launcher.cfg)に重ねるので AppFunctions::Setup() より後
    HomeWidgets::Setup();

    //通知(予約の読み込みとトースト)。予約の送り主を登録簿と突き合わせるのでAppFunctionsより後、
    //トーストを全オーバーレイの一番上に置くのでキーボードより後
    NotificationFunctions::Setup();

    //バックグラウンドの仕事(ミュージックの再生・チャットの未読・Luaアプリの定期スクリプト)。
    //Luaアプリの仕事を登録簿から拾うので AppFunctions::Setup() より後
    BackgroundFunctions::Setup();
    //録音中はバックグラウンドの通信(TLSのハンドシェイクで1〜2秒止まる)を止め、マイクの列を溢れさせない
    BackgroundFunctions::suspend_query = [](){ return MicFunctions::IsRecording(); };

    //--- 開発者向けの道具(/sys/debug.cfg) ---
    perf_overlay = new PerfOverlay();
    WidgetFunctions::AddOverlay(perf_overlay); //トーストよりさらに上
    DevToolsFunctions::perf_overlay_visibility = [](bool visible){
        if(perf_overlay) perf_overlay->setVisible(visible);
    };

    //画面ロック(PIN)。全オーバーレイの一番上に置くので最後に作る。PINがあれば起動時からロックする
    LockFunctions::ringing_query = [](){ return AlarmFunctions::IsRinging() || TimerFunctions::TimerRinging(); };
    LockFunctions::ringing_stop = [](){
        if(AlarmFunctions::IsRinging()) AlarmFunctions::Dismiss();
        if(TimerFunctions::TimerRinging()) TimerFunctions::TimerReset();
    };
    LockFunctions::Setup();
#if !defined(__EMSCRIPTEN__)
    LuaDebugger::SetFrontend(&lua_debug_screen);
#endif
    LuaDebugger::SetErrorReporter(&CrashDumpFunctions::SaveLuaError);
    LuaDebugger::SetActivityHook(&CrashDumpFunctions::SetLua);
    //シリアルの"dbg ..."(Luaデバッガ)と"hw ..."(実機の確認。docs/HARDWARE_CHECKLIST.md)
    PadFunctions::extra_line_handler = [](const char* line){
        return LuaDebugger::FeedSerialLine(line) || HwTestFunctions::FeedSerialLine(line);
    };
    DevToolsFunctions::Setup();
    //前回のクラッシュを知らせる(タップでダンプを開く)
    CrashDumpFunctions::PostPendingNotice();
    //ファームの更新の結果を知らせ、置き場(内蔵フラッシュのLittleFS)を片付ける。通知より後
    FirmwareFunctions::Setup();

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
    //Wiiクラシックコントローラー(I2C)。読んだボタンは次のPadFunctions::Update()で反映される
    WiiClassicFunctions::Update();
    //外部コントローラー(Wiiクラシック + USBシリアル経由のPCのキーボード)。
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
    //スリープに入ったらロック。ロック中はコントローラーの入力を後ろの画面へ渡さない
    LockFunctions::Update();
    ProfilerFunctions::Mark(ProfilerFunctions::Section::Power);

    //保留中のシーン遷移をフレーム境界で適用する(ウィジェット更新より前)
    SceneFunctions::Update();
    {
        Scene* cur = SceneFunctions::Current();
        CrashDumpFunctions::SetScene(cur ? cur->getName() : nullptr);
        //画面が望む音の出力の周波数(0なら設定のまま)。変わったときだけ切り替わる。
        //画面が何も望まなければ、バックグラウンドで鳴っているもの(ミュージック)の望みを使う
        //(閉じたアプリの曲が周波数の切り替えで止まらないように)
        uint32_t rate = cur ? cur->preferredSampleRate() : 0;
        if(rate == 0) rate = BackgroundFunctions::PreferredSampleRate();
        SoundFunctions::RequestSampleRate(rate);
    }
    //Luaデバッガで止まっていた間に液晶へ直接描いたので、全体を描き直す
    //(半透明のダイアログの下も。MarkDirty()だとFlushDirty()がそこを描き直さない)
    if(LuaDebugger::TakeRedrawRequest()) PICO_GFX::MarkDirtyBelow({0, 0, SCREEN_WIDTH, SCREEN_HEIGHT});

    //電源メニューで頼まれた再起動(今の画面の後片付けをしてから。戻らない)
    PowerMenuFunctions::Update();

    //物理キーボードの打鍵を今の画面/開いているキー盤へ配る(遷移の適用後、ウィジェット更新の前)
    KeyInputFunctions::Update();
    ProfilerFunctions::Mark(ProfilerFunctions::Section::Scene);

    WidgetFunctions::UpdateAll();
    //ステータスバーの長押しで電源メニュー(指を離す前に開く)
    if(status->is_pressing && !status_long_pressed
        && millis() - status_press_ms >= PowerMenuFunctions::kLongPressMs){
        status_long_pressed = true;
        PowerMenuFunctions::Open();
    }
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
    MicFunctions::Update();     //録音: 2コア目が積んだ音をSDのWAVへ
    //VSYS電圧の読み取り(内部でkSampleIntervalMsごとに間引く)
    BatteryFunctions::Update();
    //アラームの時刻の見張り(時計アプリを閉じていても鳴らす)。TimeFunctions::Update()より後
    AlarmFunctions::Update();
    TimerFunctions::Update();
    //通知の予約の見張りとトーストの出し入れ。TimeFunctions::Update()より後
    NotificationFunctions::Update();
    //バックグラウンドの仕事(曲の終わりで次の曲へ・チャットの未読・Luaの定期スクリプト)。SoundFunctions::Update()より後
    BackgroundFunctions::Update();
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
    bool busy = SoundFunctions::LoopCore1();
    //マイク(SPH0645)の受け取り。I2Sが動いている間は休みを1msにする(DMAのバッファは約42ms)
    busy |= MicFunctions::LoopCore1();
    if(!busy) delay(MicFunctions::Core1Busy() ? 1 : SoundFunctions::IdleDelayMs());
}
