#pragma once
// LuaEngine を分けた .cpp(LuaEngine.cpp / LuaEngine_<分野>.cpp)だけが使う小さな道具。
// 外から LuaEngine を使う側は読まないこと
#include "lua/LuaEngine.hpp"
#include "gui/widgets/WidgetFactory.hpp"
#include "gui/widgets/WidgetProperty.hpp"
#include "gui/widgets/Textbox.hpp"
#include "gui/core/GFX_Functions.hpp"

namespace LuaInternal {
    // WidgetFactory::Create()が実際に生成する特殊化と揃える(WidgetProperty.cppの
    // 同名エイリアスと同じ理由。食い違うと不正なstatic_castになる)
    using TextboxT = Textbox<WidgetFactory::kTextboxCapacity>;

    // lua_State から持ち主の LuaEngine を引く(registerFn() の説明参照)
    inline LuaEngine* Self(lua_State* L) {
        return *static_cast<LuaEngine**>(lua_getextraspace(L));
    }

    // pico.draw_*が描いた範囲をdirtyにする。pico.image_target()で画像へ描いている間は
    // 画面に関係が無いので何もしない
    inline void LuaMarkDirty(const Rect& r) {
        if (!LuaOffscreen::active) PICO_GFX::MarkDirty(r);
    }

    // WidgetProperty::Value <-> Luaスタックの変換
    inline void PushPropertyValue(lua_State* L, const WidgetProperty::Value& v) {
        switch (v.type) {
            case WidgetProperty::Type::Int:   lua_pushinteger(L, v.i); break;
            case WidgetProperty::Type::Float: lua_pushnumber(L, v.f); break;
            case WidgetProperty::Type::Bool:  lua_pushboolean(L, v.b); break;
            case WidgetProperty::Type::Str:   lua_pushstring(L, v.s.c_str()); break;
        }
    }
}
