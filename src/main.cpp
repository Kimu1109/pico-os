#include <Arduino.h>
#include "functions/SD_Functions.hpp"
#include "functions/Touch_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/IME_Functions.hpp"
#include "widgets/Label.hpp"
#include "widgets/Button.hpp"
#include "widgets/Keyboard.hpp"
#include "OS_Data.hpp"
#include <SPI.h>

//デバッグ用
static Label* sd_label;
static Button* hi_button;
static Keyboard* keyboard;

void setup() {
    pinMode(LED_BUILTIN, OUTPUT);

    PICO_GFX::Setup();
    PICO_Touch::Setup();

    hi_button = new Button(160, 100, "HI!");
    sd_label = new Label(0, 50, "sd-failed");
    keyboard = new Keyboard();

    WidgetFunctions::add(hi_button);
    WidgetFunctions::add(sd_label);
    WidgetFunctions::add(keyboard);
    WidgetFunctions::add(keyboard->dev_label); //!TODO REMOVE

    if(PICO_SD::Setup()){
        sd_label->Text(PICO_SD::ReadTextFileFast("/test.txt"));
    }

    IME_Functions::setup();

    pinMode(LED_BUILTIN, HIGH);
}

void loop() {
    PICO_Touch::Update();
    
    WidgetFunctions::updateAll();

    PICO_GFX::flushDirty();
}