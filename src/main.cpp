#include <Arduino.h>

#include "functions/SD_Functions.hpp"
#include "functions/Touch_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/IME_Functions.hpp"
#include "functions/Keyboard_Functions.hpp"

#include "widgets/Label.hpp"
#include "widgets/Button.hpp"
#include "widgets/ScrollList.hpp"

#include "OS_Data.hpp"
#include <SPI.h>

//デバッグ用
static Label* sd_label;
static Button* hi_button;
static ScrollList* scroll;

void setup() {
    pinMode(LED_BUILTIN, OUTPUT);

    PICO_GFX::Setup();
    PICO_Touch::Setup();

    hi_button = new Button(160, 100, "HI!");
    sd_label = new Label(0, 50, "sd-failed");
    scroll = new ScrollList(10, 80, 150, 100, 2);
    scroll->Add("こんにちは");
    scroll->Add("Hello!");
    scroll->Add("グーデンターク");
    scroll->Add("コンギョサムニダ");
    scroll->Add("ニーハオ");
    scroll->Add("チマチョゴリ");
    scroll->Add("グーパンダック");
    scroll->Add("こんちくは!");
    WidgetFunctions::add(hi_button);
    WidgetFunctions::add(sd_label);
    WidgetFunctions::add(scroll);

    if(PICO_SD::Setup()){
        sd_label->Text(PICO_SD::ReadTextFileFast("/test.txt"));
    }

    hi_button->onPressStart([]() {
        OSData::keyboard_jpn->Visible(!OSData::keyboard_jpn->Visible());
    });

    KeyboardFunctions::Setup();
    IME_Functions::setup();

    pinMode(LED_BUILTIN, HIGH);
}

void loop() {
    PICO_Touch::Update();
    
    WidgetFunctions::updateAll();

    PICO_GFX::flushDirty();
}