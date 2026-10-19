#pragma once

#include <cstddef>
#include "gui/widgets/WidgetID.hpp"
#include "gui/widgets/WidgetProperty.hpp"

class Widget;

// ウィジェットの種類ごとの情報を1つの表にまとめたもの(WidgetTypes.cpp)。
// WidgetFactory(作る・名前から引く)と WidgetProperty(種類ごとのプロパティ)はこれを引くだけなので、
// 種類を足すときに直すのは WidgetType の列挙と WidgetTypes.cpp の1か所だけ。
namespace WidgetTypes {
    struct Info {
        WidgetType type;
        // Luaの pico.create(名前) の名前。作れない種類(ダイアログ等)は nullptr
        const char* lua_name;
        // 仮の位置・大きさで作る(呼び出し側が置き直す)。作れない種類は nullptr
        Widget* (*create)();
        // 種類に固有のプロパティ(X/Y/Visible等の共通のものは WidgetProperty が先に見る)。無ければ nullptr
        bool (*get)(Widget* widget, WidgetProperty::Id id, WidgetProperty::Value& out);
        bool (*set)(Widget* widget, WidgetProperty::Id id, const WidgetProperty::Value& value);
    };

    extern const Info kTypes[];
    extern const size_t kTypeCount;

    // 無ければ nullptr
    const Info* Find(WidgetType type);
    const Info* FindByName(const char* lua_name);
}
