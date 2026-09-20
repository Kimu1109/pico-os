#include "gui/widgets/WidgetProperty.hpp"

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

using WidgetProperty::Id;
using WidgetProperty::Type;
using WidgetProperty::Value;

namespace {
    // WidgetFactory::Create()が実際に生成する特殊化と揃える(食い違うと不正な
    // static_castになるため、リテラルで書き直さずWidgetFactory.hppの定数を使う)。
    using LabelT = Label<WidgetFactory::kLabelTextCapacity>;
    using TextboxT = Textbox<WidgetFactory::kTextboxCapacity>;

    // Widget基底が仮想で持つ、型を問わず常に読めるプロパティ。
    bool GetCommon(Widget* w, Id id, Value& out) {
        switch (id) {
            case Id::X: out = Value::MakeInt(w->getX()); return true;
            case Id::Y: out = Value::MakeInt(w->getY()); return true;
            case Id::W: out = Value::MakeInt(w->getW()); return true;
            case Id::H: out = Value::MakeInt(w->getH()); return true;
            case Id::Visible: out = Value::MakeBool(w->getVisible()); return true;
            case Id::BackgroundColor: out = Value::MakeInt(w->getBackgroundColor()); return true;
            default: return false;
        }
    }

    // Widget基底が仮想で持つ、型を問わず常に書けるプロパティ。
    // W/Hは基底にsetW/setHが無い(ウィジェットごとに意味・シグネチャが違う)ため、
    // 各ウィジェットのswitch側で対応しているものだけを扱う。
    bool SetCommon(Widget* w, Id id, const Value& v) {
        switch (id) {
            case Id::X:
                if (v.type != Type::Int) return false;
                w->setX(v.i); return true;
            case Id::Y:
                if (v.type != Type::Int) return false;
                w->setY(v.i); return true;
            case Id::Visible:
                if (v.type != Type::Bool) return false;
                w->setVisible(v.b); return true;
            case Id::BackgroundColor:
                if (v.type != Type::Int) return false;
                w->setBackgroundColor((int8_t)v.i); return true;
            default: return false;
        }
    }
}

bool WidgetProperty::Get(Widget* widget, Id id, Value& out) {
    if (!widget) return false;
    if (GetCommon(widget, id, out)) return true;

    switch (widget->getWidgetType()) {
        case WidgetType::Button: {
            Button* b = static_cast<Button*>(widget);
            switch (id) {
                case Id::Text: out = Value::MakeStr(b->getText().c_str()); return true;
                case Id::FontSize: out = Value::MakeInt((int32_t)b->getFontSize()); return true;
                case Id::TextColor: out = Value::MakeInt(b->getTextColor()); return true;
                case Id::BorderColor: out = Value::MakeInt(b->getBorderColor()); return true;
                case Id::IconId: out = Value::MakeInt((int32_t)b->getIconId()); return true;
                default: return false;
            }
        }
        case WidgetType::Label: {
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
        case WidgetType::Textbox: {
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
        case WidgetType::NumberInput: {
            NumberInput* n = static_cast<NumberInput*>(widget);
            switch (id) {
                case Id::FontSize: out = Value::MakeInt((int32_t)n->getFontSize()); return true;
                case Id::TextColor: out = Value::MakeInt(n->getTextColor()); return true;
                case Id::BorderColor: out = Value::MakeInt(n->getBorderColor()); return true;
                default: return false;
                // 注意: NumberInputは入力値(num)そのものを読み書きする公開APIを持たない
                // (onShow/onHide経由でITextInputWidget側とやり取りするだけ)。
                // Valueプロパティは既存ウィジェット側の対応がないため未対応。
            }
        }
        case WidgetType::Checkbox: {
            Checkbox* c = static_cast<Checkbox*>(widget);
            switch (id) {
                case Id::Text: out = Value::MakeStr(c->getText()->c_str()); return true;
                case Id::Checked: out = Value::MakeBool(c->getIsChecked()); return true;
                case Id::FontSize: out = Value::MakeInt((int32_t)c->getFontSize()); return true;
                case Id::TextColor: out = Value::MakeInt(c->getTextColor()); return true;
                default: return false;
            }
        }
        case WidgetType::Icon: {
            Icon* ic = static_cast<Icon*>(widget);
            switch (id) {
                case Id::IconId: out = Value::MakeInt((int32_t)ic->getIconId()); return true;
                case Id::IconSize: out = Value::MakeInt((int32_t)ic->getIconSize()); return true;
                case Id::Color: out = Value::MakeInt(ic->getColor()); return true;
                default: return false;
                // IconOpaqueはIcon側にgetterが無いため未対応(setのみ)。
            }
        }
        case WidgetType::Image: {
            Image* im = static_cast<Image*>(widget);
            switch (id) {
                case Id::Path: out = Value::MakeStr(im->getPath()->c_str()); return true;
                default: return false;
            }
        }
        case WidgetType::NumberSlider: {
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
        case WidgetType::ScrollContainer: {
            ScrollContainer* sc = static_cast<ScrollContainer*>(widget);
            switch (id) {
                case Id::BorderColor: out = Value::MakeInt(sc->getBorderColor()); return true;
                default: return false;
            }
        }
        case WidgetType::ScrollList: {
            ScrollList* sl = static_cast<ScrollList*>(widget);
            switch (id) {
                case Id::FontSize: out = Value::MakeInt((int32_t)sl->getFontSize()); return true;
                case Id::TextColor: out = Value::MakeInt(sl->getTextColor()); return true;
                case Id::BorderColor: out = Value::MakeInt(sl->getBorderColor()); return true;
                case Id::SelectedIndex: out = Value::MakeInt(sl->getSelectedIndex()); return true;
                case Id::EnableIcon: out = Value::MakeBool(sl->getEnableIcon()); return true;
                default: return false;
            }
        }
        case WidgetType::CanvasRaster: {
            CanvasRaster* cr = static_cast<CanvasRaster*>(widget);
            switch (id) {
                case Id::Color: out = Value::MakeInt(cr->getBrushColor()); return true;
                case Id::BrushRadius: out = Value::MakeFloat(cr->getBrushRadius()); return true;
                case Id::CanvasMode: out = Value::MakeInt((int32_t)cr->getMode()); return true;
                default: return false;
            }
        }
        case WidgetType::LayoutContainer: {
            LayoutContainer* lc = static_cast<LayoutContainer*>(widget);
            switch (id) {
                case Id::Direction: out = Value::MakeInt((int32_t)lc->getDirection()); return true;
                case Id::CrossAlign: out = Value::MakeInt((int32_t)lc->getCrossAlign()); return true;
                case Id::Gap: out = Value::MakeInt(lc->getGap()); return true;
                case Id::Padding: out = Value::MakeInt(lc->getPadding()); return true;
                default: return false;
            }
        }
        case WidgetType::GridContainer: {
            GridContainer* gc = static_cast<GridContainer*>(widget);
            switch (id) {
                case Id::Cols: out = Value::MakeInt(gc->getCols()); return true;
                case Id::Gap: out = Value::MakeInt(gc->getGap()); return true;
                case Id::Padding: out = Value::MakeInt(gc->getPadding()); return true;
                default: return false;
                // HAlign/VAlignはGridContainer側にgetterが無いため未対応(setのみ)。
            }
        }
        case WidgetType::TabBar: {
            TabBar* tb = static_cast<TabBar*>(widget);
            switch (id) {
                case Id::TabSelected: out = Value::MakeInt(tb->getSelected()); return true;
                case Id::TabCount: out = Value::MakeInt(tb->getTabCount()); return true;
                case Id::FontSize: out = Value::MakeInt((int32_t)tb->getFontSize()); return true;
                case Id::BorderColor: out = Value::MakeInt(tb->getBorderColor()); return true;
                default: return false;
            }
        }
        case WidgetType::DropdownMenu: {
            DropdownMenu* dm = static_cast<DropdownMenu*>(widget);
            switch (id) {
                case Id::SelectedIndex: out = Value::MakeInt(dm->getSelectedIndex()); return true;
                default: return false;
            }
        }
        default:
            return false;
    }
}

bool WidgetProperty::Set(Widget* widget, Id id, const Value& value) {
    if (!widget) return false;
    if (SetCommon(widget, id, value)) return true;

    switch (widget->getWidgetType()) {
        case WidgetType::Button: {
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
                default: return false;
            }
        }
        case WidgetType::Label: {
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
        case WidgetType::Textbox: {
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
        case WidgetType::NumberInput: {
            NumberInput* n = static_cast<NumberInput*>(widget);
            switch (id) {
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
        case WidgetType::Checkbox: {
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
        case WidgetType::Icon: {
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
        case WidgetType::Image: {
            Image* im = static_cast<Image*>(widget);
            switch (id) {
                case Id::Path:
                    if (value.type != Type::Str) return false;
                    im->setPath(value.s.c_str()); return true;
                default: return false;
            }
        }
        case WidgetType::NumberSlider: {
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
        case WidgetType::ScrollContainer: {
            ScrollContainer* sc = static_cast<ScrollContainer*>(widget);
            switch (id) {
                case Id::BorderColor:
                    if (value.type != Type::Int) return false;
                    sc->setBorderColor((int8_t)value.i); return true;
                default: return false;
            }
        }
        case WidgetType::ScrollList: {
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
        case WidgetType::CanvasRaster: {
            CanvasRaster* cr = static_cast<CanvasRaster*>(widget);
            switch (id) {
                case Id::Color:
                    if (value.type != Type::Int) return false;
                    cr->setBrushColor((int8_t)value.i); return true;
                case Id::BrushRadius:
                    if (value.type != Type::Float && value.type != Type::Int) return false;
                    cr->setBrushRadius(value.type == Type::Float ? value.f : (float)value.i); return true;
                case Id::CanvasMode:
                    if (value.type != Type::Int) return false;
                    cr->setMode((Canvas::Mode)value.i); return true;
                default: return false;
            }
        }
        case WidgetType::LayoutContainer: {
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
        case WidgetType::GridContainer: {
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
        case WidgetType::TabBar: {
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
        case WidgetType::DropdownMenu: {
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
        case WidgetType::LuaCanvas: {
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
        default:
            return false;
    }
}

namespace {
    struct NameEntry { const char* name; Id id; };

    // snake_case。Idを増やしたらここへも1行足すこと(片方だけ更新すると
    // 「Lua側からは見えない/NameFromIdがログに"?"を出す」というずれ方をする)
    constexpr NameEntry kNameTable[] = {
        {"x", Id::X}, {"y", Id::Y}, {"w", Id::W}, {"h", Id::H},
        {"visible", Id::Visible}, {"background_color", Id::BackgroundColor},

        {"text", Id::Text}, {"placeholder", Id::Placeholder}, {"font_size", Id::FontSize},
        {"text_color", Id::TextColor}, {"border_color", Id::BorderColor},
        {"max_width", Id::MaxWidth}, {"max_height", Id::MaxHeight},
        {"text_align", Id::TextAlign}, {"is_single_line", Id::IsSingleLine},

        {"value", Id::Value}, {"min_value", Id::MinValue}, {"max_value", Id::MaxValue},
        {"checked", Id::Checked}, {"selected_index", Id::SelectedIndex},
        {"decimal_places", Id::DecimalPlaces}, {"visible_num", Id::VisibleNum},

        {"icon_id", Id::IconId}, {"icon_size", Id::IconSize},
        {"icon_opaque", Id::IconOpaque}, {"color", Id::Color},

        {"path", Id::Path},

        {"brush_radius", Id::BrushRadius}, {"canvas_mode", Id::CanvasMode},

        {"gap", Id::Gap}, {"padding", Id::Padding}, {"direction", Id::Direction},
        {"cross_align", Id::CrossAlign}, {"cols", Id::Cols},
        {"h_align", Id::HAlign}, {"v_align", Id::VAlign},

        {"tab_selected", Id::TabSelected}, {"tab_count", Id::TabCount},

        {"enable_icon", Id::EnableIcon},
    };
}

bool WidgetProperty::IdFromName(const char* name, Id& out) {
    if (!name) return false;
    for (const auto& entry : kNameTable) {
        if (std::strcmp(entry.name, name) == 0) {
            out = entry.id;
            return true;
        }
    }
    return false;
}

const char* WidgetProperty::NameFromId(Id id) {
    for (const auto& entry : kNameTable) {
        if (entry.id == id) return entry.name;
    }
    return "?";
}
