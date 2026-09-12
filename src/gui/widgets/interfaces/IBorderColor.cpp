#include "gui/widgets/interfaces/IBorderColor.hpp"

// 既定の実装は値を保持するだけ(理由はIFontImplementation.cppのコメントを参照)。
// ScrollListのようにこれをoverrideしないクラスもあるため、基底側に定義が要る。
void IBorderColor::setBorderColor(int8_t palette_color){
    this->border_color = palette_color;
}
