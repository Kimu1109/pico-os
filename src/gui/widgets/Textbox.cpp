#include "gui/widgets/Textbox.hpp"
#include "OS_Data.hpp"
#include "functions/Keyboard_Functions.hpp"
#include "consts.hpp"

template<size_t N>
void Textbox<N>::causeOnPressStart(){
    Widget::causeOnPressStart();

    KeyboardFunctions::RegisterInputTarget(this);
    OSData::keyboard_jpn->setVisible(true);
}

template<size_t N>
void Textbox<N>::onShow(ITextInputWidget* keyboard){
    FixedString<PICO_STR_LL> text;
    text.assign(*this->getText());
    keyboard->setText(text);
}
template<size_t N>
void Textbox<N>::onTextChanged(ITextInputWidget* keyboard){
    // 入力途中は背景のTextboxを更新せず、onHide(確定時)に反映する
}
template<size_t N>
void Textbox<N>::onHide(ITextInputWidget* keyboard){
    this->setText(keyboard->getText());
}

template<size_t N>
Textbox<N>::~Textbox(){
    KeyboardFunctions::UnregisterInputTarget(this);
}

template class Textbox<PICO_STR_LL>;
template class Textbox<PICO_PATH_LEN>;
