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
    label->SetBorderColor(PICO_BLACK);
    label->BorderWidth(1);
    label->Visible(false);

    OSData::keyboard_eng = new KeyboardEng(label);
    OSData::keyboard_jpn = new Keyboard(label);
    WidgetFunctions::addDialog(OSData::keyboard_eng);
    WidgetFunctions::addDialog(OSData::keyboard_jpn);
}

void KeyboardFunctions::RegisterInputTarget(ITextInputTarget *target){
    static_cast<KeyboardEng*>(OSData::keyboard_eng)->SetInputTarget(target);
    static_cast<KeyboardEng*>(OSData::keyboard_jpn)->SetInputTarget(target);
}

void KeyboardFunctions::UnregisterInputTarget(ITextInputTarget *target){
    static_cast<KeyboardEng*>(OSData::keyboard_eng)->RemoveInputTarget(target);
    static_cast<KeyboardEng*>(OSData::keyboard_jpn)->RemoveInputTarget(target);
}