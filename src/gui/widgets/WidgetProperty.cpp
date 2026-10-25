#include "gui/widgets/WidgetProperty.hpp"
#include "gui/widgets/WidgetTypes.hpp"

#include <cstring>
#include "gui/widgets/Widget.hpp"

using WidgetProperty::Id;
using WidgetProperty::Type;
using WidgetProperty::Value;

namespace {
    // Widget基底が仮想で持つ、型を問わず常に読めるプロパティ。
    bool GetCommon(Widget* w, Id id, Value& out) {
        switch (id) {
            case Id::X: out = Value::MakeInt(w->getX()); return true;
            case Id::Y: out = Value::MakeInt(w->getY()); return true;
            case Id::W: out = Value::MakeInt(w->getW()); return true;
            case Id::H: out = Value::MakeInt(w->getH()); return true;
            case Id::Visible: out = Value::MakeBool(w->getVisible()); return true;
            case Id::BackgroundColor: out = Value::MakeInt(w->getBackgroundColor()); return true;
            case Id::HitTransparent: out = Value::MakeBool(w->getHitTransparent()); return true;
            case Id::Enabled: out = Value::MakeBool(w->getEnabled()); return true;
            case Id::Focusable: out = Value::MakeBool(w->isFocusable()); return true;
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
            case Id::HitTransparent:
                if (v.type != Type::Bool) return false;
                w->setHitTransparent(v.b); return true;
            case Id::Enabled:
                if (v.type != Type::Bool) return false;
                w->setEnabled(v.b); return true;
            case Id::Focusable:
                if (v.type != Type::Bool) return false;
                w->setFocusable(v.b); return true;
            default: return false;
        }
    }
}

bool WidgetProperty::Get(Widget* widget, Id id, Value& out) {
    if (!widget) return false;
    if (GetCommon(widget, id, out)) return true;
    const WidgetTypes::Info* info = WidgetTypes::Find(widget->getWidgetType());
    return info && info->get && info->get(widget, id, out);
}

bool WidgetProperty::Set(Widget* widget, Id id, const Value& value) {
    if (!widget) return false;
    if (SetCommon(widget, id, value)) return true;
    const WidgetTypes::Info* info = WidgetTypes::Find(widget->getWidgetType());
    return info && info->set && info->set(widget, id, value);
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
        {"undo_enabled", Id::UndoEnabled},

        {"gap", Id::Gap}, {"padding", Id::Padding}, {"direction", Id::Direction},
        {"cross_align", Id::CrossAlign}, {"cols", Id::Cols},
        {"h_align", Id::HAlign}, {"v_align", Id::VAlign},

        {"tab_selected", Id::TabSelected}, {"tab_count", Id::TabCount},

        {"enable_icon", Id::EnableIcon}, {"item_count", Id::ItemCount},

        {"filled", Id::Filled}, {"thickness", Id::Thickness},
        {"x1", Id::X1}, {"y1", Id::Y1}, {"x2", Id::X2}, {"y2", Id::Y2},
        {"x3", Id::X3}, {"y3", Id::Y3},

        {"scroll_x", Id::ScrollX}, {"max_scroll_x", Id::MaxScrollX}, {"max_scroll_y", Id::MaxScrollY},
        {"scroll_y", Id::ScrollY}, {"image_w", Id::ImageW}, {"image_h", Id::ImageH},
        {"row_count", Id::RowCount},

        {"hour", Id::Hour}, {"minute", Id::Minute}, {"second", Id::Second},
        {"hand_color", Id::HandColor}, {"second_hand_color", Id::SecondHandColor},
        {"total_ms", Id::TotalMs}, {"editable", Id::Editable},
        {"year", Id::Year}, {"month", Id::Month}, {"today", Id::Today},
        {"selected", Id::Selected},

        {"ecc", Id::Ecc}, {"qr_size", Id::QrSize},

        {"hit_transparent", Id::HitTransparent}, {"enabled", Id::Enabled},
        {"focusable", Id::Focusable},
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
