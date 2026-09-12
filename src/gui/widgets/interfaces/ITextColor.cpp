#include "OS_Data.hpp"
#include "gui/widgets/interfaces/ITextColor.hpp"

void ITextColor::textColorApply(){
    OSData::frame->setTextColor(this->text_color);
}
void ITextColor::textColorDefault(){
    OSData::frame->setTextColor(PICO_FORECOLOR);
}

// 既定の実装は値を保持するだけ(理由はIFontImplementation.cppのコメントを参照)。
// ScrollListのようにこれをoverrideしないクラスもあるため、基底側に定義が要る。
void ITextColor::setTextColor(int8_t palette_color){
    this->text_color = palette_color;
}