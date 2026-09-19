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
#include "consts.hpp"

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
            return true;
        default:
            return false;
    }
}
