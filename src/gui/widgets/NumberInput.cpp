#include "gui/widgets/NumberInput.hpp"
#include "OS_Data.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Keyboard_Functions.hpp"

void NumberInput::render(){
    if(!this->visible) return;
    if(!this->needs_redraw) return;

    if(this->prev_l_rect != this->l_rect){
        PICO_GFX::MarkDirty(this->getScreenPrevRect());
    }

    const Rect g_rect = this->getScreenRect();

    OSData::frame->drawRect(g_rect.x, g_rect.y, g_rect.w, g_rect.h, this->border_color);
    
    this->fontApply();
    this->textColorApply();
    OSData::frame->setCursor(g_rect.x, g_rect.y + 2);
    OSData::frame->print(this->num);
    this->textColorDefault();
    this->fontDefault();

    PICO_GFX::MarkDirty(g_rect);
    this->prev_l_rect.copy(this->l_rect);

    this->needs_redraw = false;
}

void NumberInput::causeOnPressStart(){
    Widget::causeOnPressStart();

    KeyboardFunctions::RegisterInputTarget(this);
    OSData::keyboard_num->setVisible(true);
}

void NumberInput::onShow(ITextInputWidget* keyboard){
    keyboard->setText(this->num);
}
void NumberInput::onTextChanged(ITextInputWidget* keyboard){
    //リアルタイム更新は重いのでやめとく
}
void NumberInput::onHide(ITextInputWidget* keyboard){
    this->num = keyboard->getText();
    this->needsRender();
}

NumberInput::~NumberInput(){
    KeyboardFunctions::UnregisterInputTarget(this);
}