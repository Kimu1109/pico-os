#include <Arduino.h>

#include "functions/SD_Functions.hpp"
#include "functions/Touch_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/IME_Functions.hpp"
#include "functions/Keyboard_Functions.hpp"

#include "widgets/MarkdownView.hpp"

#include "OS_Data.hpp"
#include <SPI.h>

//デバッグ用
static MarkdownView* markdown;

void setup() {
    pinMode(LED_BUILTIN, OUTPUT);

    PICO_GFX::Setup();
    PICO_Touch::Setup();
    PICO_SD::Setup();

    KeyboardFunctions::Setup();
    IME_Functions::Setup();

    markdown = new MarkdownView(0, 0, 240, 320);
    markdown->load("doc/doc.md");
    WidgetFunctions::Add(markdown);

    pinMode(LED_BUILTIN, HIGH);
}

void loop() {
    PICO_Touch::Update();
    
    WidgetFunctions::UpdateAll();

    PICO_GFX::FlushDirty();
}