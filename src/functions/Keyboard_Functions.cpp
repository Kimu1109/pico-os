#include "functions/Keyboard_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "OS_Data.hpp"
#include "widgets/Label.hpp"
#include "widgets/Keyboard.hpp"
#include "widgets/KeyboardEng.hpp"

void KeyboardFunctions::Setup(){
    Label* label = new Label(10, 10, "");
    label->MaxWidth(SCREEN_WIDTH - 20);

    label->CursorVisible(true);
    label->CursorBlink(true);
    label->BackgroundColor(PICO_WHITE);
    label->BorderColor(PICO_BLACK);
    label->BorderWidth(1);
    label->Visible(false);

    OSData::keyboard_eng = new KeyboardEng(label);
    OSData::keyboard_jpn = new Keyboard(label);
    WidgetFunctions::add(OSData::keyboard_eng);
    WidgetFunctions::add(OSData::keyboard_jpn);
}