#include <Arduino.h>

#include "functions/SD_Functions.hpp"
#include "functions/Touch_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/IME_Functions.hpp"
#include "functions/Keyboard_Functions.hpp"
#include "functions/Network_Functions.hpp"
#include "functions/Task_Functions.hpp"
#include "functions/Time_Functions.hpp"
#include "functions/Test_Functions.hpp"

#include "gui/widgets/MarkdownView.hpp"
#include "gui/widgets/Textbox.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/systems/Statusbar.hpp"

#include "OS_Data.hpp"
#include <SPI.h>

//デバッグ用
static Statusbar* status;

static MarkdownView* markdown;
static Textbox* textbox;
static Button* wifi_test;

void setup() {
    pinMode(LED_BUILTIN, OUTPUT);

    PICO_GFX::Setup();
    PICO_SD::Setup();

    LogFunctions::Setup();

    PICO_Touch::Setup();
    PICO_Task::Setup();

    NetworkFunctions::Setup();
    KeyboardFunctions::Setup();
    IME_Functions::Setup();
    TimeFunctions::Setup();
    
    TestFunctions::Setup();

    wifi_test = new Button(0, 30, "Wifi-scan");
    wifi_test->setOnPressStart([](){
        NetworkFunctions::ScanAsync();
    });

    markdown = new MarkdownView(0, 100, 240, 220);
    markdown->load("tmp/doc.md");

    status = new Statusbar();
    textbox = new Textbox("", 0, 60, 100, 30, true);

    WidgetFunctions::AddDialog(status);
    WidgetFunctions::Add(markdown);
    WidgetFunctions::Add(wifi_test);
    WidgetFunctions::Add(textbox);

    pinMode(LED_BUILTIN, HIGH);

    LOG_SYS_OK("System setup has succeeded!");
}

void loop() {
    PICO_Touch::Update();
    
    WidgetFunctions::UpdateAll();

    PICO_GFX::FlushDirty();

    PICO_Task::Update();
    LogFunctions::Update();
    TimeFunctions::Update();
    NetworkFunctions::Update();
}