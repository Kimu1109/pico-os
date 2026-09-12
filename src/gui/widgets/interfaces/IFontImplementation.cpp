#include "gui/widgets/interfaces/IFontImplementation.hpp"

// 既定の実装は値を保持するだけ。表示の更新まで必要なクラス(Label/Button/Checkbox/ScrollList)は
// これをoverrideして再描画やレイアウトの無効化も行う。
//
// 宣言だけで定義を持たないと、このクラスのvtable/typeinfoが未定義のままになり、
// ビルド設定(最適化レベルやRTTIの有無)によってはリンクが通らなくなる。
// 実際、Checkboxを他の翻訳単位から使うとリンクエラーになっていた。
void IFontImplementation::setFontSize(FontFn::FontSize size){
    this->f_size = size;
}
