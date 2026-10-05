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
#include "consts.hpp"
#include <cstring>

Widget* WidgetFactory::Create(WidgetType type) {
    switch (type) {
        case WidgetType::Button:
            return new Button(0, 0, "");
        case WidgetType::Label:
            return new Label<kLabelTextCapacity>(0, 0, "");
        case WidgetType::Textbox:
            return new Textbox<kTextboxCapacity>("", 0, 0, 100, 24, true);
        case WidgetType::NumberInput:
            return new NumberInput(0, 0, 60);
        case WidgetType::Checkbox:
            return new Checkbox(0, 0, "");
        case WidgetType::Icon:
            return new Icon(0, 0, IconID::AppBox, IconSize::Px16);
        case WidgetType::Image:
            // パス未指定の間は何も描かない(MarkdownViewの表示プールと同じ扱い)。
            // setPath()で後から実体を指すまでは安全に放置できる。
            return new Image("", 0, 0, false);
        case WidgetType::NumberSlider:
            return new NumberSlider(0, 0, 100);
        case WidgetType::ScrollContainer:
            return new ScrollContainer(0, 0, 100, 100);
        case WidgetType::ScrollList:
            return new ScrollList(0, 0, 100, 100);
        case WidgetType::CanvasRaster:
            return new CanvasRaster(0, 0, 100, 100);
        case WidgetType::LayoutContainer:
            return new LayoutContainer(0, 0, 100, 100);
        case WidgetType::GridContainer:
            return new GridContainer(0, 0, 100, 100, 2);
        case WidgetType::TabBar:
            return new TabBar(0, 0, 100, 24);
        case WidgetType::DropdownMenu:
            return new DropdownMenu(0, 0, 100);
        case WidgetType::LuaCanvas:
            return new LuaCanvas(0, 0, 50, 50);
        case WidgetType::RectShape:
            return new RectShape(0, 0, 40, 24);
        case WidgetType::EllipseShape:
            return new EllipseShape(0, 0, 40, 24);
        case WidgetType::LineShape:
            return new LineShape(0, 0, 40, 24);
        case WidgetType::TriangleShape:
            return new TriangleShape(0, 20, 20, 0, 40, 20);
        case WidgetType::ProgressBar:
            return new ProgressBar(0, 0, 100, 12);
        case WidgetType::TextView:
            return new TextView(0, 0, 100, 100);
        case WidgetType::ImageView:
            return new ImageView(0, 0, 100, 100);
        case WidgetType::MarkdownView:
            return new MarkdownView(0, 0, 100, 100);
        case WidgetType::AnalogClock:
            return new AnalogClock(0, 0, 100);
        case WidgetType::DurationPicker:
            return new DurationPicker(0, 0, 150, 60);
        case WidgetType::MonthGrid:
            return new MonthGrid(0, 0, 210, 150);
        default:
            // アプリ/OS専用ウィジェットとダイアログはここでは作らない
            return nullptr;
    }
}

bool WidgetFactory::IsCreatable(WidgetType type) {
    switch (type) {
        case WidgetType::Button:
        case WidgetType::Label:
        case WidgetType::Textbox:
        case WidgetType::NumberInput:
        case WidgetType::Checkbox:
        case WidgetType::Icon:
        case WidgetType::Image:
        case WidgetType::NumberSlider:
        case WidgetType::ScrollContainer:
        case WidgetType::ScrollList:
        case WidgetType::CanvasRaster:
        case WidgetType::LayoutContainer:
        case WidgetType::GridContainer:
        case WidgetType::TabBar:
        case WidgetType::DropdownMenu:
        case WidgetType::LuaCanvas:
        case WidgetType::RectShape:
        case WidgetType::EllipseShape:
        case WidgetType::LineShape:
        case WidgetType::TriangleShape:
        case WidgetType::ProgressBar:
        case WidgetType::TextView:
        case WidgetType::ImageView:
        case WidgetType::MarkdownView:
        case WidgetType::AnalogClock:
        case WidgetType::DurationPicker:
        case WidgetType::MonthGrid:
            return true;
        default:
            return false;
    }
}

bool WidgetFactory::TypeFromName(const char* name, WidgetType& out) {
    if (!name) return false;

    // WidgetType列挙子と同じ表記(PascalCase)。IsCreatable()の一覧と必ず揃えること
    static const struct { const char* name; WidgetType type; } kTable[] = {
        {"Button", WidgetType::Button},
        {"Label", WidgetType::Label},
        {"Textbox", WidgetType::Textbox},
        {"NumberInput", WidgetType::NumberInput},
        {"Checkbox", WidgetType::Checkbox},
        {"Icon", WidgetType::Icon},
        {"Image", WidgetType::Image},
        {"NumberSlider", WidgetType::NumberSlider},
        {"ScrollContainer", WidgetType::ScrollContainer},
        {"ScrollList", WidgetType::ScrollList},
        {"CanvasRaster", WidgetType::CanvasRaster},
        {"LayoutContainer", WidgetType::LayoutContainer},
        {"GridContainer", WidgetType::GridContainer},
        {"TabBar", WidgetType::TabBar},
        {"DropdownMenu", WidgetType::DropdownMenu},
        // C++側のクラス名はLuaCanvas(CanvasRaster.hppの`namespace Canvas`との
        // 衝突回避)だが、Lua側からは単に"Canvas"として見せる
        {"Canvas", WidgetType::LuaCanvas},
        // 図形ウィジェット。RectShapeはutil/Rect.hppのstruct Rectとの衝突回避で
        // C++側クラス名をずらしてあるが、Lua側からは単に"Rect"として見せる
        {"Rect", WidgetType::RectShape},
        {"Ellipse", WidgetType::EllipseShape},
        {"Line", WidgetType::LineShape},
        {"Triangle", WidgetType::TriangleShape},
        {"ProgressBar", WidgetType::ProgressBar},
        {"TextView", WidgetType::TextView},
        {"ImageView", WidgetType::ImageView},
        {"MarkdownView", WidgetType::MarkdownView},
        {"AnalogClock", WidgetType::AnalogClock},
        {"DurationPicker", WidgetType::DurationPicker},
        {"MonthGrid", WidgetType::MonthGrid},
    };

    for (const auto& entry : kTable) {
        if (std::strcmp(entry.name, name) == 0) {
            out = entry.type;
            return true;
        }
    }
    return false;
}
