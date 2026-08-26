#include "widgets/Textbox.hpp"
#include "OS_Data.hpp"
#include "functions/Keyboard_Functions.hpp"

void Textbox::causeOnPressStart(){
    Widget::causeOnPressStart();

    KeyboardFunctions::RegisterInputTarget(this);
    OSData::keyboard_jpn->setVisible(true);
}

void Textbox::onShow(ITextInputWidget* keyboard){
    keyboard->setText(this->getText());
}
void Textbox::onTextChanged(ITextInputWidget* keyboard){
    // 入力途中は背景のTextboxを更新せず、onHide(確定時)に反映する
}
void Textbox::onHide(ITextInputWidget* keyboard){
    this->setText(keyboard->getText());
}

Textbox::~Textbox(){
    KeyboardFunctions::UnregisterInputTarget(this);
}