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
#include "gui/widgets/FileExplorer.hpp"
#include "gui/widgets/systems/Statusbar.hpp"

#include "gui/widgets/dialogs/FileSelectDialog.hpp"

#include "OS_Data.hpp"
#include <SPI.h>

//デバッグ用
static Statusbar* status;

static MarkdownView* markdown;
static FileExplorer* explorer;

static FileSelectDialog* dia;

void setup() {
    pinMode(LED_BUILTIN, OUTPUT);

    PICO_GFX::Setup();
    PICO_SD::Setup();

    LogFunctions::Setup();

    PICO_Touch::Setup();
    PICO_Task::Setup();

    status = new Statusbar();
    WidgetFunctions::AddOverlay(status);

    NetworkFunctions::Setup();
    KeyboardFunctions::Setup();
    IME_Functions::Setup();
    TimeFunctions::Setup();
    
    TestFunctions::Setup();

    dia = new FileSelectDialog("/");
    dia->setVisible(true);
    dia->setOnClose([](bool is_ok){
        if(is_ok){
            Serial.println(dia->getSelectedPath());
            WidgetFunctions::RemoveDialog(dia);
        }
    });
    WidgetFunctions::AddDialog(dia);

    explorer = new FileExplorer(0, 20, 240, 120);

    markdown = new MarkdownView(0, 150, 240, 170);
    markdown->load("tmp/doc.md");

    WidgetFunctions::Add(markdown);
    WidgetFunctions::Add(explorer);

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