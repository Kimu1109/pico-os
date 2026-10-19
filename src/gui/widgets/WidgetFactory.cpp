#include "gui/widgets/WidgetFactory.hpp"
#include "gui/widgets/WidgetTypes.hpp"

// 種類ごとの作り方は WidgetTypes.cpp の表(kTypes)にある
Widget* WidgetFactory::Create(WidgetType type) {
    const WidgetTypes::Info* info = WidgetTypes::Find(type);
    // アプリ/OS専用ウィジェットとダイアログはここでは作らない(create が無い)
    return (info && info->create) ? info->create() : nullptr;
}

bool WidgetFactory::IsCreatable(WidgetType type) {
    const WidgetTypes::Info* info = WidgetTypes::Find(type);
    return info && info->create;
}

bool WidgetFactory::TypeFromName(const char* name, WidgetType& out) {
    const WidgetTypes::Info* info = WidgetTypes::FindByName(name);
    if (!info || !info->create) return false;
    out = info->type;
    return true;
}
