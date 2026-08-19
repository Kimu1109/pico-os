#include "widgets/Textbox.hpp"
#include "OS_Data.hpp"
#include "functions/Keyboard_Functions.hpp"

void Textbox::onPressStart(){
    if(this->on_press_start) this->on_press_start();

    KeyboardFunctions::RegisterInputTarget(this);
    OSData::keyboard_jpn->Visible(true);
}

void Textbox::onShow(ITextInputWidget* keyboard){
    keyboard->SetText(this->Text());
}
void Textbox::onTextChanged(ITextInputWidget* keyboard){
    // 入力途中は背景のTextboxを更新せず、onHide(確定時)に反映する
}
void Textbox::onHide(ITextInputWidget* keyboard){
    this->Text(keyboard->GetText());
}

Textbox::~Textbox(){
    KeyboardFunctions::UnregisterInputTarget(this);
}