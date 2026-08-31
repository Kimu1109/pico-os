#include "functions/Keyboard_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "OS_Data.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/Keyboard.hpp"
#include "gui/widgets/KeyboardEng.hpp"

void KeyboardFunctions::Setup(){
    Label* label = new Label(10, 10, "");
    label->setMaxWidth(SCREEN_WIDTH - 20);

    label->setCursorVisible(true);
    label->setCursorBlink(true);
    label->setBackgroundColor(PICO_WHITE);
    label->setBorderColor(PICO_BLACK);
    label->setBorderWidth(1);
    label->setVisible(false);

    OSData::keyboard_eng = new KeyboardEng(label);
    OSData::keyboard_jpn = new Keyboard(label);
    WidgetFunctions::AddDialog(OSData::keyboard_eng);
    WidgetFunctions::AddDialog(OSData::keyboard_jpn);

    LOG_SYS_OK("Keyboard Setup has succeeded!");
}

void KeyboardFunctions::RegisterInputTarget(ITextInputTarget *target){
    static_cast<KeyboardEng*>(OSData::keyboard_eng)->setInputTarget(target);
    static_cast<KeyboardEng*>(OSData::keyboard_jpn)->setInputTarget(target);
}

void KeyboardFunctions::UnregisterInputTarget(ITextInputTarget *target){
    static_cast<KeyboardEng*>(OSData::keyboard_eng)->removeInputTarget(target);
    static_cast<KeyboardEng*>(OSData::keyboard_jpn)->removeInputTarget(target);
}