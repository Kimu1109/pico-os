// ウィジェットの種類ごとの「作り方・プロパティの読み方・書き方」と、それを1つにまとめた表(kTypes)。
// 種類を足すときは WidgetID.hpp の WidgetType と、ここに1か所(関数とkTypesの行)を足す。
// WidgetFactory(Create/TypeFromName)と WidgetProperty(Get/Set)はこの表を引くだけ。
#include "gui/widgets/WidgetTypes.hpp"

#include <cstring>
#include "gui/widgets/Widget.hpp"
#include "gui/widgets/WidgetFactory.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/Textbox.hpp"
#include "gui/widgets/NumberInput.hpp"
#include "gui/widgets/Checkbox.hpp"
#include "gui/widgets/Icon.hpp"
#include "gui/widgets/Image.hpp"
#include "gui/widgets/NumberSlider.hpp"
#include "gui/widgets/ScrollContainer.hpp"
#include "gui/widgets/ScrollList.hpp"
#include "gui/widgets/CanvasRaster.hpp"
#include "gui/widgets/LayoutContainer.hpp"
#include "gui/widgets/GridContainer.hpp"
#include "gui/widgets/TabBar.hpp"
#include "gui/widgets/DropdownMenu.hpp"
#include "gui/widgets/LuaCanvas.hpp"
#include "gui/widgets/RectShape.hpp"
#include "gui/widgets/EllipseShape.hpp"
#include "gui/widgets/LineShape.hpp"
#include "gui/widgets/TriangleShape.hpp"
#include "gui/widgets/ProgressBar.hpp"
#include "gui/widgets/TextView.hpp"
#include "gui/widgets/ImageView.hpp"
#include "gui/widgets/apps/MarkdownView.hpp"
#include "gui/widgets/apps/AnalogClock.hpp"
#include "gui/widgets/apps/DurationPicker.hpp"
#include "gui/widgets/apps/MonthGrid.hpp"
#include "gui/widgets/dialogs/InputDialog.hpp"
#include "gui/widgets/dialogs/FileSaveDialog.hpp"
#include "gui/widgets/dialogs/FileSelectDialog.hpp"
#include "gui/widgets/dialogs/ColorDialog.hpp"
#include "gui/widgets/dialogs/PickerDialog.hpp"

using WidgetProperty::Id;
using WidgetProperty::Type;
using WidgetProperty::Value;

namespace {
    // WidgetFactory::Create()が実際に生成する特殊化と揃える(食い違うと不正な
    // static_castになるため、リテラルで書き直さずWidgetFactory.hppの定数を使う)。
    using LabelT = Label<WidgetFactory::kLabelTextCapacity>;
    using TextboxT = Textbox<WidgetFactory::kTextboxCapacity>;

// ---- Button ----
Widget* CreateButton() { return new Button(0, 0, ""); }
bool GetButton(Widget* widget, Id id, Value& out) {
        Button* b = static_cast<Button*>(widget);
        switch (id) {
            case Id::Text: out = Value::MakeStr(b->getText().c_str()); return true;
            case Id::FontSize: out = Value::MakeInt((int32_t)b->getFontSize()); return true;
            case Id::TextColor: out = Value::MakeInt(b->getTextColor()); return true;
            case Id::BorderColor: out = Value::MakeInt(b->getBorderColor()); return true;
            case Id::IconId: out = Value::MakeInt((int32_t)b->getIconId()); return true;
            case Id::IconSize: out = Value::MakeInt((int32_t)b->getIconSize()); return true;
            default: return false;
        }
}
bool SetButton(Widget* widget, Id id, const Value& value) {
        Button* b = static_cast<Button*>(widget);
        switch (id) {
            case Id::Text:
                if (value.type != Type::Str) return false;
                b->setText(value.s.c_str()); return true;
            case Id::W:
                if (value.type != Type::Int) return false;
                b->setW(value.i); return true;
            case Id::H:
                if (value.type != Type::Int) return false;
                b->setH(value.i); return true;
            case Id::FontSize:
                if (value.type != Type::Int) return false;
                b->setFontSize((FontFn::FontSize)value.i); return true;
            case Id::TextColor:
                if (value.type != Type::Int) return false;
                b->setTextColor((int8_t)value.i); return true;
            case Id::BorderColor:
                if (value.type != Type::Int) return false;
                b->setBorderColor((int8_t)value.i); return true;
            // アイコンボタン化(スクラッチパッド実装時の細部の穴埋め、2026-09-23追加)。
            // Button::setIcon(id, size)は元々C++側にあったが、id/sizeの片方だけを
            // 差し替えたい場合もあるので、それぞれ「もう片方は現在値のまま」で
            // setIcon()を呼び直す(初回呼び出し時、icon_sizeは既定Px16のまま)
            case Id::IconId:
                if (value.type != Type::Int) return false;
                b->setIcon((IconID)value.i, b->getIconSize()); return true;
            case Id::IconSize:
                if (value.type != Type::Int) return false;
                b->setIcon(b->getIconId(), (IconSize)value.i); return true;
            default: return false;
        }
}

// ---- Label ----
Widget* CreateLabel() { return new Label<WidgetFactory::kLabelTextCapacity>(0, 0, ""); }
bool GetLabel(Widget* widget, Id id, Value& out) {
        LabelT* l = static_cast<LabelT*>(widget);
        switch (id) {
            case Id::Text: out = Value::MakeStr(l->getText()->c_str()); return true;
            case Id::Placeholder: out = Value::MakeStr(l->getPlaceholder()->c_str()); return true;
            case Id::FontSize: out = Value::MakeInt((int32_t)l->getFontSize()); return true;
            case Id::TextColor: out = Value::MakeInt(l->getTextColor()); return true;
            case Id::BorderColor: out = Value::MakeInt(l->getBorderColor()); return true;
            case Id::MaxWidth: out = Value::MakeInt(l->getMaxWidth()); return true;
            case Id::MaxHeight: out = Value::MakeInt(l->getMaxHeight()); return true;
            case Id::TextAlign: out = Value::MakeInt((int32_t)l->getTextAlign()); return true;
            default: return false;
        }
}
bool SetLabel(Widget* widget, Id id, const Value& value) {
        LabelT* l = static_cast<LabelT*>(widget);
        switch (id) {
            case Id::Text:
                if (value.type != Type::Str) return false;
                l->setText(value.s.c_str()); return true;
            case Id::Placeholder:
                if (value.type != Type::Str) return false;
                l->setPlaceholder(value.s.c_str()); return true;
            case Id::FontSize:
                if (value.type != Type::Int) return false;
                l->setFontSize((FontFn::FontSize)value.i); return true;
            case Id::TextColor:
                if (value.type != Type::Int) return false;
                l->setTextColor((int8_t)value.i); return true;
            case Id::BorderColor:
                if (value.type != Type::Int) return false;
                l->setBorderColor((int8_t)value.i); return true;
            case Id::MaxWidth:
                if (value.type != Type::Int) return false;
                l->setMaxWidth(value.i); return true;
            case Id::MaxHeight:
                if (value.type != Type::Int) return false;
                l->setMaxHeight(value.i); return true;
            case Id::TextAlign:
                if (value.type != Type::Int) return false;
                l->setTextAlign((TextAlign)value.i); return true;
            default: return false;
        }
}

// ---- Textbox ----
Widget* CreateTextbox() { return new Textbox<WidgetFactory::kTextboxCapacity>("", 0, 0, 100, 24, true); }
bool GetTextbox(Widget* widget, Id id, Value& out) {
        TextboxT* t = static_cast<TextboxT*>(widget);
        switch (id) {
            case Id::Text: out = Value::MakeStr(t->getText()->c_str()); return true;
            case Id::Placeholder: out = Value::MakeStr(t->getPlaceholder()->c_str()); return true;
            case Id::FontSize: out = Value::MakeInt((int32_t)t->getFontSize()); return true;
            case Id::TextColor: out = Value::MakeInt(t->getTextColor()); return true;
            case Id::BorderColor: out = Value::MakeInt(t->getBorderColor()); return true;
            case Id::MaxWidth: out = Value::MakeInt(t->getMaxWidth()); return true;
            case Id::MaxHeight: out = Value::MakeInt(t->getMaxHeight()); return true;
            case Id::IsSingleLine: out = Value::MakeBool(t->getIsSingleLine()); return true;
            default: return false;
        }
}
bool SetTextbox(Widget* widget, Id id, const Value& value) {
        TextboxT* t = static_cast<TextboxT*>(widget);
        switch (id) {
            case Id::Text:
                if (value.type != Type::Str) return false;
                t->setText(value.s.c_str()); return true;
            case Id::Placeholder:
                if (value.type != Type::Str) return false;
                t->setPlaceholder(value.s.c_str()); return true;
            case Id::FontSize:
                if (value.type != Type::Int) return false;
                t->setFontSize((FontFn::FontSize)value.i); return true;
            case Id::TextColor:
                if (value.type != Type::Int) return false;
                t->setTextColor((int8_t)value.i); return true;
            case Id::BorderColor:
                if (value.type != Type::Int) return false;
                t->setBorderColor((int8_t)value.i); return true;
            case Id::MaxWidth:
                if (value.type != Type::Int) return false;
                t->setMaxWidth(value.i); return true;
            case Id::MaxHeight:
                if (value.type != Type::Int) return false;
                t->setMaxHeight(value.i); return true;
            case Id::IsSingleLine:
                if (value.type != Type::Bool) return false;
                t->setIsSingleLine(value.b); return true;
            default: return false;
        }
}

// ---- NumberInput ----
Widget* CreateNumberInput() { return new NumberInput(0, 0, 60); }
bool GetNumberInput(Widget* widget, Id id, Value& out) {
        NumberInput* n = static_cast<NumberInput*>(widget);
        switch (id) {
            // 数字専用キーボードで入力された文字列そのもの(数値への変換は
            // 呼び出し側の責任)。他の入力系ウィジェットと同じ"text"の名で読む
            case Id::Text: out = Value::MakeStr(n->getNum()->c_str()); return true;
            case Id::FontSize: out = Value::MakeInt((int32_t)n->getFontSize()); return true;
            case Id::TextColor: out = Value::MakeInt(n->getTextColor()); return true;
            case Id::BorderColor: out = Value::MakeInt(n->getBorderColor()); return true;
            default: return false;
        }
}
bool SetNumberInput(Widget* widget, Id id, const Value& value) {
        NumberInput* n = static_cast<NumberInput*>(widget);
        switch (id) {
            case Id::Text:
                if (value.type != Type::Str) return false;
                n->setNum(value.s.c_str()); return true;
            case Id::FontSize:
                if (value.type != Type::Int) return false;
                n->setFontSize((FontFn::FontSize)value.i); return true;
            case Id::TextColor:
                if (value.type != Type::Int) return false;
                n->setTextColor((int8_t)value.i); return true;
            case Id::BorderColor:
                if (value.type != Type::Int) return false;
                n->setBorderColor((int8_t)value.i); return true;
            default: return false;
        }
}

// ---- Checkbox ----
Widget* CreateCheckbox() { return new Checkbox(0, 0, ""); }
bool GetCheckbox(Widget* widget, Id id, Value& out) {
        Checkbox* c = static_cast<Checkbox*>(widget);
        switch (id) {
            case Id::Text: out = Value::MakeStr(c->getText()->c_str()); return true;
            case Id::Checked: out = Value::MakeBool(c->getIsChecked()); return true;
            case Id::FontSize: out = Value::MakeInt((int32_t)c->getFontSize()); return true;
            case Id::TextColor: out = Value::MakeInt(c->getTextColor()); return true;
            default: return false;
        }
}
bool SetCheckbox(Widget* widget, Id id, const Value& value) {
        Checkbox* c = static_cast<Checkbox*>(widget);
        switch (id) {
            case Id::Text:
                if (value.type != Type::Str) return false;
                c->setText(value.s.c_str()); return true;
            case Id::Checked:
                if (value.type != Type::Bool) return false;
                c->setIsChecked(value.b); return true;
            case Id::FontSize:
                if (value.type != Type::Int) return false;
                c->setFontSize((FontFn::FontSize)value.i); return true;
            case Id::TextColor:
                if (value.type != Type::Int) return false;
                c->setTextColor((int8_t)value.i); return true;
            default: return false;
        }
}

// ---- Icon ----
Widget* CreateIcon() { return new Icon(0, 0, IconID::AppBox, IconSize::Px16); }
bool GetIcon(Widget* widget, Id id, Value& out) {
        Icon* ic = static_cast<Icon*>(widget);
        switch (id) {
            case Id::IconId: out = Value::MakeInt((int32_t)ic->getIconId()); return true;
            case Id::IconSize: out = Value::MakeInt((int32_t)ic->getIconSize()); return true;
            case Id::Color: out = Value::MakeInt(ic->getColor()); return true;
            case Id::IconOpaque: out = Value::MakeBool(ic->getOpaque()); return true;
            default: return false;
        }
}
bool SetIcon(Widget* widget, Id id, const Value& value) {
        Icon* ic = static_cast<Icon*>(widget);
        switch (id) {
            case Id::IconId:
                if (value.type != Type::Int) return false;
                ic->setIconId((IconID)value.i); return true;
            case Id::IconSize:
                if (value.type != Type::Int) return false;
                ic->setIconSize((IconSize)value.i); return true;
            case Id::Color:
                if (value.type != Type::Int) return false;
                ic->setColor((int8_t)value.i); return true;
            case Id::IconOpaque:
                if (value.type != Type::Bool) return false;
                ic->setOpaque(value.b); return true;
            default: return false;
        }
}

// ---- Image ----
// パス未指定の間は何も描かない(MarkdownViewの表示プールと同じ扱い)。
// setPath()で後から実体を指すまでは安全に放置できる。
Widget* CreateImage() { return new Image("", 0, 0, false); }
bool GetImage(Widget* widget, Id id, Value& out) {
        Image* im = static_cast<Image*>(widget);
        switch (id) {
            case Id::Path: out = Value::MakeStr(im->getPath()->c_str()); return true;
            default: return false;
        }
}
bool SetImage(Widget* widget, Id id, const Value& value) {
        Image* im = static_cast<Image*>(widget);
        switch (id) {
            case Id::Path:
                if (value.type != Type::Str) return false;
                im->setPath(value.s.c_str()); return true;
            default: return false;
        }
}

// ---- NumberSlider ----
Widget* CreateNumberSlider() { return new NumberSlider(0, 0, 100); }
bool GetNumberSlider(Widget* widget, Id id, Value& out) {
        NumberSlider* ns = static_cast<NumberSlider*>(widget);
        switch (id) {
            case Id::Value: out = Value::MakeFloat(ns->getValue()); return true;
            case Id::MinValue: out = Value::MakeFloat(ns->getMinValue()); return true;
            case Id::MaxValue: out = Value::MakeFloat(ns->getMaxValue()); return true;
            case Id::Color: out = Value::MakeInt(ns->getColor()); return true;
            case Id::VisibleNum: out = Value::MakeBool(ns->getVisibleNum()); return true;
            case Id::DecimalPlaces: out = Value::MakeInt(ns->getDecimalPlacesNum()); return true;
            default: return false;
        }
}
bool SetNumberSlider(Widget* widget, Id id, const Value& value) {
        NumberSlider* ns = static_cast<NumberSlider*>(widget);
        switch (id) {
            case Id::Value:
                if (value.type != Type::Float && value.type != Type::Int) return false;
                ns->setValue(value.type == Type::Float ? value.f : (float)value.i); return true;
            case Id::MinValue:
                if (value.type != Type::Float && value.type != Type::Int) return false;
                ns->setMinValue(value.type == Type::Float ? value.f : (float)value.i); return true;
            case Id::MaxValue:
                if (value.type != Type::Float && value.type != Type::Int) return false;
                ns->setMaxValue(value.type == Type::Float ? value.f : (float)value.i); return true;
            case Id::W:
                if (value.type != Type::Int) return false;
                ns->setW((int16_t)value.i); return true;
            case Id::H:
                if (value.type != Type::Int) return false;
                ns->setH((int16_t)value.i); return true;
            case Id::Color:
                if (value.type != Type::Int) return false;
                ns->setColor((int8_t)value.i); return true;
            case Id::VisibleNum:
                if (value.type != Type::Bool) return false;
                ns->setVisibleNum(value.b); return true;
            case Id::DecimalPlaces:
                if (value.type != Type::Int) return false;
                ns->setDecimalPlacesNum(value.i); return true;
            default: return false;
        }
}

// ---- ScrollContainer ----
Widget* CreateScrollContainer() { return new ScrollContainer(0, 0, 100, 100); }
bool GetScrollContainer(Widget* widget, Id id, Value& out) {
        ScrollContainer* sc = static_cast<ScrollContainer*>(widget);
        switch (id) {
            case Id::BorderColor: out = Value::MakeInt(sc->getBorderColor()); return true;
            case Id::ScrollX: out = Value::MakeInt(sc->getScrollX()); return true;
            case Id::ScrollY: out = Value::MakeInt(sc->getScrollY()); return true;
            case Id::MaxScrollX: out = Value::MakeInt(sc->getMaxScrollX()); return true;
            case Id::MaxScrollY: out = Value::MakeInt(sc->getMaxScrollY()); return true;
            default: return false;
        }
}
bool SetScrollContainer(Widget* widget, Id id, const Value& value) {
        ScrollContainer* sc = static_cast<ScrollContainer*>(widget);
        switch (id) {
            case Id::BorderColor:
                if (value.type != Type::Int) return false;
                sc->setBorderColor((int8_t)value.i); return true;
            case Id::W:
                if (value.type != Type::Int) return false;
                sc->setSize((int16_t)value.i, (int16_t)sc->getH()); return true;
            case Id::H:
                if (value.type != Type::Int) return false;
                sc->setSize((int16_t)sc->getW(), (int16_t)value.i); return true;
            case Id::ScrollX:
                if (value.type != Type::Int) return false;
                sc->setScroll(value.i, sc->getScrollY()); return true;
            case Id::ScrollY:
                if (value.type != Type::Int) return false;
                sc->setScroll(sc->getScrollX(), value.i); return true;
            default: return false;
        }
}

// ---- ScrollList ----
Widget* CreateScrollList() { return new ScrollList(0, 0, 100, 100); }
bool GetScrollList(Widget* widget, Id id, Value& out) {
        ScrollList* sl = static_cast<ScrollList*>(widget);
        switch (id) {
            case Id::ScrollY: out = Value::MakeInt(sl->getScrollY()); return true;
            case Id::FontSize: out = Value::MakeInt((int32_t)sl->getFontSize()); return true;
            case Id::TextColor: out = Value::MakeInt(sl->getTextColor()); return true;
            case Id::BorderColor: out = Value::MakeInt(sl->getBorderColor()); return true;
            case Id::SelectedIndex: out = Value::MakeInt(sl->getSelectedIndex()); return true;
            case Id::EnableIcon: out = Value::MakeBool(sl->getEnableIcon()); return true;
            case Id::ItemCount: out = Value::MakeInt(sl->getItemCount()); return true;
            default: return false;
        }
}
bool SetScrollList(Widget* widget, Id id, const Value& value) {
        ScrollList* sl = static_cast<ScrollList*>(widget);
        switch (id) {
            case Id::W:
                if (value.type != Type::Int) return false;
                sl->setW(value.i); return true;
            case Id::H:
                if (value.type != Type::Int) return false;
                sl->setH(value.i); return true;
            case Id::FontSize:
                if (value.type != Type::Int) return false;
                sl->setFontSize((FontFn::FontSize)value.i); return true;
            case Id::TextColor:
                if (value.type != Type::Int) return false;
                sl->setTextColor((int8_t)value.i); return true;
            case Id::BorderColor:
                if (value.type != Type::Int) return false;
                sl->setBorderColor((int8_t)value.i); return true;
            case Id::SelectedIndex:
                if (value.type != Type::Int) return false;
                sl->setSelectedIndex(value.i); return true;
            case Id::EnableIcon:
                if (value.type != Type::Bool) return false;
                sl->setEnableIcon(value.b); return true;
            default: return false;
        }
}

// ---- CanvasRaster ----
Widget* CreateCanvasRaster() { return new CanvasRaster(0, 0, 100, 100); }
bool GetCanvasRaster(Widget* widget, Id id, Value& out) {
        CanvasRaster* cr = static_cast<CanvasRaster*>(widget);
        switch (id) {
            case Id::Color: out = Value::MakeInt(cr->getBrushColor()); return true;
            case Id::BrushRadius: out = Value::MakeFloat(cr->getBrushRadius()); return true;
            case Id::CanvasMode: out = Value::MakeInt((int32_t)cr->getMode()); return true;
            case Id::Filled: out = Value::MakeBool(cr->getFillShape()); return true;
            case Id::UndoEnabled: out = Value::MakeBool(cr->getUndoEnabled()); return true;
            default: return false;
        }
}
bool SetCanvasRaster(Widget* widget, Id id, const Value& value) {
        CanvasRaster* cr = static_cast<CanvasRaster*>(widget);
        switch (id) {
            // w/hを変えるとcreateSprite()を呼び直すため、これまでの描画内容は
            // 消える(CanvasRaster::resize()参照)。生成直後にサイズを決める
            // 用途を想定しており、描き始めた後のリサイズには向かない
            case Id::W:
                if (value.type != Type::Int) return false;
                cr->setW((int16_t)value.i); return true;
            case Id::H:
                if (value.type != Type::Int) return false;
                cr->setH((int16_t)value.i); return true;
            case Id::Color:
                if (value.type != Type::Int) return false;
                cr->setBrushColor((int8_t)value.i); return true;
            case Id::BrushRadius:
                if (value.type != Type::Float && value.type != Type::Int) return false;
                cr->setBrushRadius(value.type == Type::Float ? value.f : (float)value.i); return true;
            case Id::CanvasMode:
                if (value.type != Type::Int) return false;
                if (value.i < Canvas::Mode::Line || value.i > Canvas::Mode::Fill) return false;
                cr->setMode((Canvas::Mode)value.i); return true;
            // 四角形/楕円を塗りつぶしで描くか
            case Id::Filled:
                if (value.type != Type::Bool) return false;
                cr->setFillShape(value.b); return true;
            // 「元に戻す」(pico.canvas_undo)用のバッファを持つか。
            // 有効にするとキャンバスと同じ大きさ(4bppでw*h/2バイト)をヒープから取る
            case Id::UndoEnabled:
                if (value.type != Type::Bool) return false;
                // 確保に失敗しても設定の書き間違いではないのでtrueを返す
                // (pico.setがエラーにする)。確保できたかはgetで確かめる
                cr->setUndoEnabled(value.b); return true;
            default: return false;
        }
}

// ---- LayoutContainer ----
Widget* CreateLayoutContainer() { return new LayoutContainer(0, 0, 100, 100); }
bool GetLayoutContainer(Widget* widget, Id id, Value& out) {
        LayoutContainer* lc = static_cast<LayoutContainer*>(widget);
        switch (id) {
            case Id::Direction: out = Value::MakeInt((int32_t)lc->getDirection()); return true;
            case Id::CrossAlign: out = Value::MakeInt((int32_t)lc->getCrossAlign()); return true;
            case Id::Gap: out = Value::MakeInt(lc->getGap()); return true;
            case Id::Padding: out = Value::MakeInt(lc->getPadding()); return true;
            default: return false;
        }
}
bool SetLayoutContainer(Widget* widget, Id id, const Value& value) {
        LayoutContainer* lc = static_cast<LayoutContainer*>(widget);
        switch (id) {
            case Id::W:
                if (value.type != Type::Int) return false;
                lc->setW(value.i); return true;
            case Id::H:
                if (value.type != Type::Int) return false;
                lc->setH(value.i); return true;
            case Id::Direction:
                if (value.type != Type::Int) return false;
                lc->setDirection((LayoutContainerTools::Direction)value.i); return true;
            case Id::CrossAlign:
                if (value.type != Type::Int) return false;
                lc->setCrossAlign((LayoutContainerTools::CrossAlign)value.i); return true;
            case Id::Gap:
                if (value.type != Type::Int) return false;
                lc->setGap((int16_t)value.i); return true;
            case Id::Padding:
                if (value.type != Type::Int) return false;
                lc->setPadding((int16_t)value.i); return true;
            default: return false;
        }
}

// ---- GridContainer ----
Widget* CreateGridContainer() { return new GridContainer(0, 0, 100, 100, 2); }
bool GetGridContainer(Widget* widget, Id id, Value& out) {
        GridContainer* gc = static_cast<GridContainer*>(widget);
        switch (id) {
            case Id::Cols: out = Value::MakeInt(gc->getCols()); return true;
            case Id::Gap: out = Value::MakeInt(gc->getGap()); return true;
            case Id::Padding: out = Value::MakeInt(gc->getPadding()); return true;
            case Id::HAlign: out = Value::MakeInt((int32_t)gc->getHAlign()); return true;
            case Id::VAlign: out = Value::MakeInt((int32_t)gc->getVAlign()); return true;
            default: return false;
        }
}
bool SetGridContainer(Widget* widget, Id id, const Value& value) {
        GridContainer* gc = static_cast<GridContainer*>(widget);
        switch (id) {
            case Id::W:
                if (value.type != Type::Int) return false;
                gc->setW(value.i); return true;
            case Id::H:
                if (value.type != Type::Int) return false;
                gc->setH(value.i); return true;
            case Id::Cols:
                if (value.type != Type::Int) return false;
                gc->setCols(value.i); return true;
            case Id::Gap:
                if (value.type != Type::Int) return false;
                gc->setGap((int16_t)value.i); return true;
            case Id::Padding:
                if (value.type != Type::Int) return false;
                gc->setPadding((int16_t)value.i); return true;
            case Id::HAlign:
                if (value.type != Type::Int) return false;
                gc->setHAlign((GridContainerTools::Align)value.i); return true;
            case Id::VAlign:
                if (value.type != Type::Int) return false;
                gc->setVAlign((GridContainerTools::Align)value.i); return true;
            default: return false;
        }
}

// ---- TabBar ----
Widget* CreateTabBar() { return new TabBar(0, 0, 100, 24); }
bool GetTabBar(Widget* widget, Id id, Value& out) {
        TabBar* tb = static_cast<TabBar*>(widget);
        switch (id) {
            case Id::TabSelected: out = Value::MakeInt(tb->getSelected()); return true;
            case Id::TabCount: out = Value::MakeInt(tb->getTabCount()); return true;
            case Id::FontSize: out = Value::MakeInt((int32_t)tb->getFontSize()); return true;
            case Id::BorderColor: out = Value::MakeInt(tb->getBorderColor()); return true;
            default: return false;
        }
}
bool SetTabBar(Widget* widget, Id id, const Value& value) {
        TabBar* tb = static_cast<TabBar*>(widget);
        switch (id) {
            case Id::W:
                if (value.type != Type::Int) return false;
                tb->setW(value.i); return true;
            case Id::H:
                if (value.type != Type::Int) return false;
                tb->setH(value.i); return true;
            case Id::TabSelected:
                if (value.type != Type::Int) return false;
                // notify=trueでタップ経由と同じ扱いにする(setOnChanged()が拾える)
                tb->setSelected(value.i, true); return true;
            case Id::FontSize:
                if (value.type != Type::Int) return false;
                tb->setFontSize((FontFn::FontSize)value.i); return true;
            case Id::BorderColor:
                if (value.type != Type::Int) return false;
                tb->setBorderColor((int8_t)value.i); return true;
            default: return false;
        }
}

// ---- DropdownMenu ----
Widget* CreateDropdownMenu() { return new DropdownMenu(0, 0, 100); }
bool GetDropdownMenu(Widget* widget, Id id, Value& out) {
        DropdownMenu* dm = static_cast<DropdownMenu*>(widget);
        switch (id) {
            case Id::SelectedIndex: out = Value::MakeInt(dm->getSelectedIndex()); return true;
            case Id::ItemCount: out = Value::MakeInt(dm->getItemCount()); return true;
            default: return false;
        }
}
bool SetDropdownMenu(Widget* widget, Id id, const Value& value) {
        DropdownMenu* dm = static_cast<DropdownMenu*>(widget);
        switch (id) {
            case Id::W:
                if (value.type != Type::Int) return false;
                dm->setW(value.i); return true;
            case Id::SelectedIndex:
                if (value.type != Type::Int) return false;
                dm->setSelectedIndex(value.i); return true;
            default: return false;
        }
}

// ---- LuaCanvas ----
Widget* CreateLuaCanvas() { return new LuaCanvas(0, 0, 50, 50); }
bool SetLuaCanvas(Widget* widget, Id id, const Value& value) {
        LuaCanvas* c = static_cast<LuaCanvas*>(widget);
        switch (id) {
            case Id::W:
                if (value.type != Type::Int) return false;
                c->setW(value.i); return true;
            case Id::H:
                if (value.type != Type::Int) return false;
                c->setH(value.i); return true;
            default: return false;
        }
}

// ---- RectShape ----
Widget* CreateRectShape() { return new RectShape(0, 0, 40, 24); }
bool GetRectShape(Widget* widget, Id id, Value& out) {
        RectShape* rs = static_cast<RectShape*>(widget);
        switch (id) {
            case Id::Color: out = Value::MakeInt(rs->getColor()); return true;
            case Id::Filled: out = Value::MakeBool(rs->getFilled()); return true;
            case Id::Thickness: out = Value::MakeInt(rs->getThickness()); return true;
            default: return false;
        }
}
bool SetRectShape(Widget* widget, Id id, const Value& value) {
        RectShape* rs = static_cast<RectShape*>(widget);
        switch (id) {
            case Id::W:
                if (value.type != Type::Int) return false;
                rs->setW(value.i); return true;
            case Id::H:
                if (value.type != Type::Int) return false;
                rs->setH(value.i); return true;
            case Id::Color:
                if (value.type != Type::Int) return false;
                rs->setColor((int8_t)value.i); return true;
            case Id::Filled:
                if (value.type != Type::Bool) return false;
                rs->setFilled(value.b); return true;
            case Id::Thickness:
                if (value.type != Type::Int) return false;
                rs->setThickness(value.i); return true;
            default: return false;
        }
}

// ---- EllipseShape ----
Widget* CreateEllipseShape() { return new EllipseShape(0, 0, 40, 24); }
bool GetEllipseShape(Widget* widget, Id id, Value& out) {
        EllipseShape* es = static_cast<EllipseShape*>(widget);
        switch (id) {
            case Id::Color: out = Value::MakeInt(es->getColor()); return true;
            case Id::Filled: out = Value::MakeBool(es->getFilled()); return true;
            case Id::Thickness: out = Value::MakeInt(es->getThickness()); return true;
            default: return false;
        }
}
bool SetEllipseShape(Widget* widget, Id id, const Value& value) {
        EllipseShape* es = static_cast<EllipseShape*>(widget);
        switch (id) {
            case Id::W:
                if (value.type != Type::Int) return false;
                es->setW(value.i); return true;
            case Id::H:
                if (value.type != Type::Int) return false;
                es->setH(value.i); return true;
            case Id::Color:
                if (value.type != Type::Int) return false;
                es->setColor((int8_t)value.i); return true;
            case Id::Filled:
                if (value.type != Type::Bool) return false;
                es->setFilled(value.b); return true;
            case Id::Thickness:
                if (value.type != Type::Int) return false;
                es->setThickness(value.i); return true;
            default: return false;
        }
}

// ---- LineShape ----
Widget* CreateLineShape() { return new LineShape(0, 0, 40, 24); }
bool GetLineShape(Widget* widget, Id id, Value& out) {
        LineShape* ls = static_cast<LineShape*>(widget);
        switch (id) {
            case Id::Color: out = Value::MakeInt(ls->getColor()); return true;
            case Id::Thickness: out = Value::MakeInt(ls->getThickness()); return true;
            case Id::X1: out = Value::MakeInt(ls->getX1()); return true;
            case Id::Y1: out = Value::MakeInt(ls->getY1()); return true;
            case Id::X2: out = Value::MakeInt(ls->getX2()); return true;
            case Id::Y2: out = Value::MakeInt(ls->getY2()); return true;
            default: return false;
        }
}
bool SetLineShape(Widget* widget, Id id, const Value& value) {
        LineShape* ls = static_cast<LineShape*>(widget);
        switch (id) {
            case Id::Color:
                if (value.type != Type::Int) return false;
                ls->setColor((int8_t)value.i); return true;
            case Id::Thickness:
                if (value.type != Type::Int) return false;
                ls->setThickness(value.i); return true;
            case Id::X1:
                if (value.type != Type::Int) return false;
                ls->setX1(value.i); return true;
            case Id::Y1:
                if (value.type != Type::Int) return false;
                ls->setY1(value.i); return true;
            case Id::X2:
                if (value.type != Type::Int) return false;
                ls->setX2(value.i); return true;
            case Id::Y2:
                if (value.type != Type::Int) return false;
                ls->setY2(value.i); return true;
            default: return false;
        }
}

// ---- TriangleShape ----
Widget* CreateTriangleShape() { return new TriangleShape(0, 20, 20, 0, 40, 20); }
bool GetTriangleShape(Widget* widget, Id id, Value& out) {
        TriangleShape* ts = static_cast<TriangleShape*>(widget);
        switch (id) {
            case Id::Color: out = Value::MakeInt(ts->getColor()); return true;
            case Id::Filled: out = Value::MakeBool(ts->getFilled()); return true;
            case Id::Thickness: out = Value::MakeInt(ts->getThickness()); return true;
            case Id::X1: out = Value::MakeInt(ts->getX1()); return true;
            case Id::Y1: out = Value::MakeInt(ts->getY1()); return true;
            case Id::X2: out = Value::MakeInt(ts->getX2()); return true;
            case Id::Y2: out = Value::MakeInt(ts->getY2()); return true;
            case Id::X3: out = Value::MakeInt(ts->getX3()); return true;
            case Id::Y3: out = Value::MakeInt(ts->getY3()); return true;
            default: return false;
        }
}
bool SetTriangleShape(Widget* widget, Id id, const Value& value) {
        TriangleShape* ts = static_cast<TriangleShape*>(widget);
        switch (id) {
            case Id::Color:
                if (value.type != Type::Int) return false;
                ts->setColor((int8_t)value.i); return true;
            case Id::Filled:
                if (value.type != Type::Bool) return false;
                ts->setFilled(value.b); return true;
            case Id::Thickness:
                if (value.type != Type::Int) return false;
                ts->setThickness(value.i); return true;
            case Id::X1:
                if (value.type != Type::Int) return false;
                ts->setX1(value.i); return true;
            case Id::Y1:
                if (value.type != Type::Int) return false;
                ts->setY1(value.i); return true;
            case Id::X2:
                if (value.type != Type::Int) return false;
                ts->setX2(value.i); return true;
            case Id::Y2:
                if (value.type != Type::Int) return false;
                ts->setY2(value.i); return true;
            case Id::X3:
                if (value.type != Type::Int) return false;
                ts->setX3(value.i); return true;
            case Id::Y3:
                if (value.type != Type::Int) return false;
                ts->setY3(value.i); return true;
            default: return false;
        }
}

// ---- ProgressBar ----
Widget* CreateProgressBar() { return new ProgressBar(0, 0, 100, 12); }
bool GetProgressBar(Widget* widget, Id id, Value& out) {
        ProgressBar* pb = static_cast<ProgressBar*>(widget);
        switch (id) {
            case Id::Value: out = Value::MakeFloat(pb->getValue()); return true;
            case Id::MinValue: out = Value::MakeFloat(pb->getMinValue()); return true;
            case Id::MaxValue: out = Value::MakeFloat(pb->getMaxValue()); return true;
            case Id::Color: out = Value::MakeInt(pb->getColor()); return true;
            case Id::BorderColor: out = Value::MakeInt(pb->getBorderColor()); return true;
            default: return false;
        }
}
bool SetProgressBar(Widget* widget, Id id, const Value& value) {
        ProgressBar* pb = static_cast<ProgressBar*>(widget);
        switch (id) {
            case Id::W:
                if (value.type != Type::Int) return false;
                pb->setW(value.i); return true;
            case Id::H:
                if (value.type != Type::Int) return false;
                pb->setH(value.i); return true;
            case Id::Value:
                if (value.type != Type::Float && value.type != Type::Int) return false;
                pb->setValue(value.type == Type::Float ? value.f : (float)value.i); return true;
            case Id::MinValue:
                if (value.type != Type::Float && value.type != Type::Int) return false;
                pb->setMinValue(value.type == Type::Float ? value.f : (float)value.i); return true;
            case Id::MaxValue:
                if (value.type != Type::Float && value.type != Type::Int) return false;
                pb->setMaxValue(value.type == Type::Float ? value.f : (float)value.i); return true;
            case Id::Color:
                if (value.type != Type::Int) return false;
                pb->setColor((int8_t)value.i); return true;
            case Id::BorderColor:
                if (value.type != Type::Int) return false;
                pb->setBorderColor((int8_t)value.i); return true;
            default: return false;
        }
}

// ---- TextView ----
Widget* CreateTextView() { return new TextView(0, 0, 100, 100); }
bool GetTextView(Widget* widget, Id id, Value& out) {
        TextView* tv = static_cast<TextView*>(widget);
        switch (id) {
            case Id::Text: out = Value::MakeStr(tv->getText()); return true;
            case Id::ScrollY: out = Value::MakeInt(tv->getScrollY()); return true;
            case Id::RowCount: out = Value::MakeInt(tv->getRowCount()); return true;
            default: return false;
        }
}
bool SetTextView(Widget* widget, Id id, const Value& value) {
        TextView* tv = static_cast<TextView*>(widget);
        switch (id) {
            case Id::W:
                if (value.type != Type::Int) return false;
                tv->setW(value.i); return true;
            case Id::H:
                if (value.type != Type::Int) return false;
                tv->setH(value.i); return true;
            case Id::Text:
                // 255Bまで。長い文書はpico.text_set()(LuaEngine)で渡す
                if (value.type != Type::Str) return false;
                tv->setOwnedText(value.s.c_str(), value.s.length()); return true;
            case Id::ScrollY:
                if (value.type != Type::Int) return false;
                tv->setScrollY(value.i); return true;
            default: return false;
        }
}

// ---- ImageView ----
Widget* CreateImageView() { return new ImageView(0, 0, 100, 100); }
bool GetImageView(Widget* widget, Id id, Value& out) {
        ImageView* iv = static_cast<ImageView*>(widget);
        switch (id) {
            case Id::Path: out = Value::MakeStr(iv->getPath()); return true;
            case Id::ImageW: out = Value::MakeInt(iv->getImageW()); return true;
            case Id::ImageH: out = Value::MakeInt(iv->getImageH()); return true;
            default: return false;
        }
}
bool SetImageView(Widget* widget, Id id, const Value& value) {
        ImageView* iv = static_cast<ImageView*>(widget);
        switch (id) {
            case Id::W:
                if (value.type != Type::Int) return false;
                iv->setW(value.i); return true;
            case Id::H:
                if (value.type != Type::Int) return false;
                iv->setH(value.i); return true;
            case Id::Path:
                // 読み込めなければfalse(表示は空になる)
                if (value.type != Type::Str) return false;
                if (value.s.empty()) { iv->unload(); return true; }
                return iv->load(value.s.c_str());
            default: return false;
        }
}

// ---- MarkdownView ----
Widget* CreateMarkdownView() { return new MarkdownView(0, 0, 100, 100); }
bool GetMarkdownView(Widget* widget, Id id, Value& out) {
        MarkdownView* mv = static_cast<MarkdownView*>(widget);
        switch (id) {
            case Id::ScrollY: out = Value::MakeInt(mv->getScrollY()); return true;
            default: return false;
        }
}
bool SetMarkdownView(Widget* widget, Id id, const Value& value) {
        MarkdownView* mv = static_cast<MarkdownView*>(widget);
        switch (id) {
            case Id::W:
                if (value.type != Type::Int) return false;
                mv->setW(value.i); return true;
            case Id::H:
                if (value.type != Type::Int) return false;
                mv->setH(value.i); return true;
            case Id::Path:
                if (value.type != Type::Str) return false;
                return mv->load(value.s.c_str());
            case Id::Text:
                // 255Bまで。長い文書はpico.text_set()で渡す
                if (value.type != Type::Str) return false;
                return mv->loadText(value.s.c_str(), value.s.length());
            case Id::ScrollY:
                if (value.type != Type::Int) return false;
                mv->setScrollY(value.i); return true;
            default: return false;
        }
}

// ---- AnalogClock ----
Widget* CreateAnalogClock() { return new AnalogClock(0, 0, 100); }
bool GetAnalogClock(Widget* widget, Id id, Value& out) {
        AnalogClock* ac = static_cast<AnalogClock*>(widget);
        switch (id) {
            case Id::Hour: out = Value::MakeInt(ac->getHour()); return true;
            case Id::Minute: out = Value::MakeInt(ac->getMinute()); return true;
            case Id::Second: out = Value::MakeInt(ac->getSecond()); return true;
            default: return false;
        }
}
bool SetAnalogClock(Widget* widget, Id id, const Value& value) {
        AnalogClock* ac = static_cast<AnalogClock*>(widget);
        switch (id) {
            case Id::W:
            case Id::H:
                if (value.type != Type::Int) return false;
                ac->setDiameter(value.i); return true;
            case Id::Hour:
                if (value.type != Type::Int) return false;
                ac->setTime(value.i, ac->getMinute(), ac->getSecond()); return true;
            case Id::Minute:
                if (value.type != Type::Int) return false;
                ac->setTime(ac->getHour(), value.i, ac->getSecond()); return true;
            case Id::Second:
                if (value.type != Type::Int) return false;
                ac->setTime(ac->getHour(), ac->getMinute(), value.i); return true;
            case Id::HandColor:
                if (value.type != Type::Int) return false;
                ac->setHandColor((int8_t)value.i); return true;
            case Id::SecondHandColor:
                if (value.type != Type::Int) return false;
                ac->setSecondHandColor((int8_t)value.i); return true;
            default: return false;
        }
}

// ---- DurationPicker ----
Widget* CreateDurationPicker() { return new DurationPicker(0, 0, 150, 60); }
bool GetDurationPicker(Widget* widget, Id id, Value& out) {
        DurationPicker* dp = static_cast<DurationPicker*>(widget);
        switch (id) {
            case Id::TotalMs: out = Value::MakeInt((int32_t)dp->getTotalMs()); return true;
            case Id::Editable: out = Value::MakeBool(dp->getEditable()); return true;
            default: return false;
        }
}
bool SetDurationPicker(Widget* widget, Id id, const Value& value) {
        DurationPicker* dp = static_cast<DurationPicker*>(widget);
        switch (id) {
            case Id::W:
                if (value.type != Type::Int) return false;
                dp->setW(value.i); return true;
            case Id::H:
                if (value.type != Type::Int) return false;
                dp->setH(value.i); return true;
            case Id::TotalMs:
                if (value.type != Type::Int || value.i < 0) return false;
                dp->setTotalMs((uint32_t)value.i); return true;
            case Id::Editable:
                if (value.type != Type::Bool) return false;
                dp->setEditable(value.b); return true;
            case Id::FontSize:
                if (value.type != Type::Int) return false;
                dp->setFontSize((FontFn::FontSize)value.i); return true;
            case Id::TextColor:
                if (value.type != Type::Int) return false;
                dp->setTextColor((int8_t)value.i); return true;
            case Id::BorderColor:
                if (value.type != Type::Int) return false;
                dp->setBorderColor((int8_t)value.i); return true;
            default: return false;
        }
}

// ---- MonthGrid ----
Widget* CreateMonthGrid() { return new MonthGrid(0, 0, 210, 150); }
bool GetMonthGrid(Widget* widget, Id id, Value& out) {
        MonthGrid* mg = static_cast<MonthGrid*>(widget);
        switch (id) {
            case Id::Year: out = Value::MakeInt(mg->getYear()); return true;
            case Id::Month: out = Value::MakeInt(mg->getMonth()); return true;
            case Id::Selected: out = Value::MakeInt(mg->getSelected()); return true;
            case Id::ItemCount: out = Value::MakeInt(mg->getDaysInMonth()); return true;
            default: return false;
        }
}
bool SetMonthGrid(Widget* widget, Id id, const Value& value) {
        MonthGrid* mg = static_cast<MonthGrid*>(widget);
        switch (id) {
            case Id::W:
                if (value.type != Type::Int) return false;
                mg->setW(value.i); return true;
            case Id::H:
                if (value.type != Type::Int) return false;
                mg->setH(value.i); return true;
            case Id::Year:
                if (value.type != Type::Int) return false;
                mg->setMonth(value.i, mg->getMonth()); return true;
            case Id::Month:
                if (value.type != Type::Int || value.i < 1 || value.i > 12) return false;
                mg->setMonth(mg->getYear(), value.i); return true;
            case Id::Today:
                if (value.type != Type::Int) return false;
                mg->setToday(value.i); return true;
            case Id::Selected:
                if (value.type != Type::Int) return false;
                mg->setSelected(value.i); return true;
            case Id::BorderColor:
                if (value.type != Type::Int) return false;
                mg->setBorderColor((int8_t)value.i); return true;
            default: return false;
        }
}

// ---- PickerDialog ----
bool GetPickerDialog(Widget* widget, Id id, Value& out) {
        PickerDialog* pd = static_cast<PickerDialog*>(widget);
        switch (id) {
            case Id::SelectedIndex: out = Value::MakeInt(pd->getSelectedIndex()); return true;
            case Id::Text: out = Value::MakeStr(pd->getResultText().c_str()); return true;
            case Id::Value: out = Value::MakeFloat(pd->getProgress()); return true;
            case Id::Year: out = Value::MakeInt(pd->getYear()); return true;
            case Id::Month: out = Value::MakeInt(pd->getMonth()); return true;
            case Id::Selected: out = Value::MakeInt(pd->getDay()); return true;
            case Id::ItemCount: out = Value::MakeInt(pd->getChoiceCount()); return true;
            default: return false;
        }
}
bool SetPickerDialog(Widget* widget, Id id, const Value& value) {
        PickerDialog* pd = static_cast<PickerDialog*>(widget);
        switch (id) {
            // 進捗ダイアログ: 0〜100の進み具合と、題名(メッセージ)の差し替え
            case Id::Value:
                if (value.type != Type::Float && value.type != Type::Int) return false;
                pd->setProgress(value.type == Type::Float ? value.f : (float)value.i); return true;
            case Id::Text:
                if (value.type != Type::Str) return false;
                pd->setMessage(value.s.c_str()); return true;
            default: return false;
        }
}

// ---- InputDialog ----
    // 以下4種はWidgetFactory非対応(ダイアログ。コンストラクタが必須引数を取るため
    // pico.show_xxx()経由でのみ生成される。上のクラスコメント参照)だが、
    // 閉じた結果をpico.get()で読めるようにするためGet/Set両方の対象にしてある
bool GetInputDialog(Widget* widget, Id id, Value& out) {
        InputDialog* d = static_cast<InputDialog*>(widget);
        switch (id) {
            // pico.show_input()で開いたダイアログの入力文字列(is_ok==trueのときのみ
            // 意味を持つ。setOnClosed()のbool単体では読めないためこちらで受け取る)
            case Id::Text: out = Value::MakeStr(d->getInput().c_str()); return true;
            default: return false;
        }
}
bool SetInputDialog(Widget* widget, Id id, const Value& value) {
        InputDialog* d = static_cast<InputDialog*>(widget);
        switch (id) {
            case Id::Text:
                if (value.type != Type::Str) return false;
                d->setInput(value.s.c_str()); return true;
            case Id::IsSingleLine:
                if (value.type != Type::Bool) return false;
                d->setIsSingleLine(value.b); return true;
            default: return false;
        }
}

// ---- FileSaveDialog ----
bool GetFileSaveDialog(Widget* widget, Id id, Value& out) {
        FileSaveDialog* d = static_cast<FileSaveDialog*>(widget);
        switch (id) {
            case Id::Path: out = Value::MakeStr(d->getSavePath()); return true;
            default: return false;
        }
}

// ---- FileSelectDialog ----
bool GetFileSelectDialog(Widget* widget, Id id, Value& out) {
        FileSelectDialog* d = static_cast<FileSelectDialog*>(widget);
        switch (id) {
            case Id::Path: {
                const char* p = d->getSelectedPath();
                if (!p) return false; // 未選択のままOKされた場合はnil(Luaの慣習)
                out = Value::MakeStr(p);
                return true;
            }
            default: return false;
        }
}

// ---- ColorDialog ----
bool GetColorDialog(Widget* widget, Id id, Value& out) {
        ColorDialog* d = static_cast<ColorDialog*>(widget);
        switch (id) {
            // 未選択は-1(ColorDialog側の仕様通りそのまま返す)
            case Id::Value: out = Value::MakeInt(d->getSelectedColor()); return true;
            default: return false;
        }
}

}  // namespace

// FileSaveDialog/FileSelectDialog/ColorDialogは開始ディレクトリ/選択色を生成後に差し替えるsetterを
// 持たない(コンストラクタでのみ決まる)ため、Setは無い(Getだけ)。
// lua_name: Luaの pico.create(名前) で作れる種類だけ名前を持つ(C++のクラス名とずらしているものがある:
// LuaCanvas は CanvasRaster.hpp の namespace Canvas と、RectShape は util/Rect.hpp の Rect とぶつかるため)
const WidgetTypes::Info WidgetTypes::kTypes[] = {
    { WidgetType::Button, "Button", CreateButton, GetButton, SetButton },
    { WidgetType::Label, "Label", CreateLabel, GetLabel, SetLabel },
    { WidgetType::Textbox, "Textbox", CreateTextbox, GetTextbox, SetTextbox },
    { WidgetType::NumberInput, "NumberInput", CreateNumberInput, GetNumberInput, SetNumberInput },
    { WidgetType::Checkbox, "Checkbox", CreateCheckbox, GetCheckbox, SetCheckbox },
    { WidgetType::Icon, "Icon", CreateIcon, GetIcon, SetIcon },
    { WidgetType::Image, "Image", CreateImage, GetImage, SetImage },
    { WidgetType::NumberSlider, "NumberSlider", CreateNumberSlider, GetNumberSlider, SetNumberSlider },
    { WidgetType::ScrollContainer, "ScrollContainer", CreateScrollContainer, GetScrollContainer, SetScrollContainer },
    { WidgetType::ScrollList, "ScrollList", CreateScrollList, GetScrollList, SetScrollList },
    { WidgetType::CanvasRaster, "CanvasRaster", CreateCanvasRaster, GetCanvasRaster, SetCanvasRaster },
    { WidgetType::LayoutContainer, "LayoutContainer", CreateLayoutContainer, GetLayoutContainer, SetLayoutContainer },
    { WidgetType::GridContainer, "GridContainer", CreateGridContainer, GetGridContainer, SetGridContainer },
    { WidgetType::TabBar, "TabBar", CreateTabBar, GetTabBar, SetTabBar },
    { WidgetType::DropdownMenu, "DropdownMenu", CreateDropdownMenu, GetDropdownMenu, SetDropdownMenu },
    { WidgetType::LuaCanvas, "Canvas", CreateLuaCanvas, nullptr, SetLuaCanvas },
    { WidgetType::RectShape, "Rect", CreateRectShape, GetRectShape, SetRectShape },
    { WidgetType::EllipseShape, "Ellipse", CreateEllipseShape, GetEllipseShape, SetEllipseShape },
    { WidgetType::LineShape, "Line", CreateLineShape, GetLineShape, SetLineShape },
    { WidgetType::TriangleShape, "Triangle", CreateTriangleShape, GetTriangleShape, SetTriangleShape },
    { WidgetType::ProgressBar, "ProgressBar", CreateProgressBar, GetProgressBar, SetProgressBar },
    { WidgetType::TextView, "TextView", CreateTextView, GetTextView, SetTextView },
    { WidgetType::ImageView, "ImageView", CreateImageView, GetImageView, SetImageView },
    { WidgetType::MarkdownView, "MarkdownView", CreateMarkdownView, GetMarkdownView, SetMarkdownView },
    { WidgetType::AnalogClock, "AnalogClock", CreateAnalogClock, GetAnalogClock, SetAnalogClock },
    { WidgetType::DurationPicker, "DurationPicker", CreateDurationPicker, GetDurationPicker, SetDurationPicker },
    { WidgetType::MonthGrid, "MonthGrid", CreateMonthGrid, GetMonthGrid, SetMonthGrid },
    { WidgetType::PickerDialog, nullptr, nullptr, GetPickerDialog, SetPickerDialog },
    { WidgetType::InputDialog, nullptr, nullptr, GetInputDialog, SetInputDialog },
    { WidgetType::FileSaveDialog, nullptr, nullptr, GetFileSaveDialog, nullptr },
    { WidgetType::FileSelectDialog, nullptr, nullptr, GetFileSelectDialog, nullptr },
    { WidgetType::ColorDialog, nullptr, nullptr, GetColorDialog, nullptr },
};
const size_t WidgetTypes::kTypeCount = sizeof(WidgetTypes::kTypes) / sizeof(WidgetTypes::kTypes[0]);

const WidgetTypes::Info* WidgetTypes::Find(WidgetType type) {
    for (size_t i = 0; i < kTypeCount; i++) {
        if (kTypes[i].type == type) return &kTypes[i];
    }
    return nullptr;
}

const WidgetTypes::Info* WidgetTypes::FindByName(const char* lua_name) {
    if (!lua_name) return nullptr;
    for (size_t i = 0; i < kTypeCount; i++) {
        if (kTypes[i].lua_name && std::strcmp(kTypes[i].lua_name, lua_name) == 0) return &kTypes[i];
    }
    return nullptr;
}
