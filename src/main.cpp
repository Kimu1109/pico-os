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
#include "functions/Pad_Functions.hpp"
#include "functions/Task_Functions.hpp"
#include "functions/Time_Functions.hpp"
#include "functions/Test_Functions.hpp"
#include "functions/Mem_Functions.hpp"
#include "functions/App_Functions.hpp"

#include "gui/widgets/systems/Statusbar.hpp"
#include "gui/scenes/HomeScene.hpp"

#include "OS_Data.hpp"
#include <SPI.h>

//シーンをまたいで常駐させるウィジェットはオーバーレイ層に置く
static Statusbar* status;

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
    DisplayFunctions::Setup(); //display.cfgを読むのでSDより後
    PowerFunctions::Setup();   //同上(sleep-timeout)

    //以降のSetupがどれだけヒープを食うかを見るための基準点
    MemFunctions::Setup();

    PICO_Touch::Setup();
    PadFunctions::Setup();
    PICO_Task::Setup();
    WidgetFunctions::Setup();

    //--- 常駐(オーバーレイ層): シーン遷移で破棄されない ---
    status = new Statusbar();
    WidgetFunctions::AddOverlay(status);

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

    //ここまでの確保は全てOS常駐。シーンアリーナを導入する際の「永続領域」に相当する
    MemFunctions::SealPermanentBaseline();

    //--- ここから先はシーンの所有物 ---
    SceneFunctions::Setup(new HomeScene());

    pinMode(LED_BUILTIN, HIGH);

    LOG_SYS_OK("System setup has succeeded!");
}

void loop() {
    PICO_Touch::Update();
    //外部コントローラー(今はUSBシリアル経由のPCのキーボード)。
    //シーンのonUpdate()より前に読み、1フレームの間は同じ答えを返す
    PadFunctions::Update();

    //操作の有無を見て自動調光を掛ける/戻す(タッチ・パッドの状態が確定した直後)
    DisplayFunctions::Update();

    //さらに操作が無ければスリープへ入る/操作で戻す(起こしたタッチはここで握りつぶす)。
    //各画面のonUpdate()が呼ぶKeepAwake()は次のフレームのここで読まれる
    PowerFunctions::Update();

    //保留中のシーン遷移をフレーム境界で適用する(ウィジェット更新より前)
    SceneFunctions::Update();

    WidgetFunctions::UpdateAll();

    PICO_GFX::FlushDirty();

    //現シーン滞在中のピーク使用量を追う(mallinfoを読むだけ)
    MemFunctions::Update();

    PICO_Task::Update();
    LogFunctions::Update();
    TimeFunctions::Update();
    NetworkFunctions::Update();
    //アンプの抜き差しの検出(音そのものは2コア目が作って流す)
    SoundFunctions::Update();
    //VSYS電圧の読み取り(内部でkSampleIntervalMsごとに間引く)
    BatteryFunctions::Update();
    //アラームの時刻の見張り(時計アプリを閉じていても鳴らす)。TimeFunctions::Update()より後
    AlarmFunctions::Update();

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
