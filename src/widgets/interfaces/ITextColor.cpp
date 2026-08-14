#include "OS_Data.hpp"
#include "widgets/interfaces/ITextColor.hpp"

void ITextColor::textColorApply(){
    OSData::frame->setTextColor(this->text_color);
}
void ITextColor::textColorDefault(){
    OSData::frame->setTextColor(PICO_FORECOLOR);
}