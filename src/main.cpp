#include <Arduino.h>

#include "functions/SD_Functions.hpp"
#include "functions/Touch_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/IME_Functions.hpp"
#include "functions/Keyboard_Functions.hpp"
#include "functions/Network_Functions.hpp"
#include "functions/Sound_Functions.hpp"
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

void setup() {
    pinMode(LED_BUILTIN, OUTPUT);

    PICO_GFX::Setup();
    PICO_SD::Setup();

    LogFunctions::Setup();

    //以降のSetupがどれだけヒープを食うかを見るための基準点
    MemFunctions::Setup();

    PICO_Touch::Setup();
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
}

//--- 2コア目: 音声専用 ---
//音源の計算とI2Sへの書き込みだけをする。1コア目の描画やTLSのハンドシェイクで音が途切れないように
void setup1() {
    SoundFunctions::SetupCore1();
}

void loop1() {
    //I2Sのバッファが埋まっていて何もすることが無ければ少し休む(バッファは約23ms分ある)
    if(!SoundFunctions::LoopCore1()) delay(1);
}
