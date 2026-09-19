#include "lua/LuaEngine.hpp"

#include <cstdlib>
#include <cstring>

#include "gui/widgets/Widget.hpp"
#include "gui/widgets/WidgetRegistry.hpp"
#include "gui/widgets/WidgetFactory.hpp"
#include "gui/widgets/WidgetProperty.hpp"
#include "gui/widgets/LayoutContainer.hpp"
#include "gui/widgets/GridContainer.hpp"
#include "gui/widgets/ScrollContainer.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Error_Functions.hpp"
#include "functions/Log_Functions.hpp"

namespace {
    // WidgetIdは32bitで符号無しだが、Luaのlua_Integerは64bit符号付きなので
    // そのまま行き来させて問題ない(桁が全く足りている)。
    LuaEngine* Self(lua_State* L) {
        return static_cast<LuaEngine*>(lua_touserdata(L, lua_upvalueindex(1)));
    }

    // WidgetProperty::Value <-> Luaスタックの変換
    void PushPropertyValue(lua_State* L, const WidgetProperty::Value& v) {
        switch (v.type) {
            case WidgetProperty::Type::Int:   lua_pushinteger(L, v.i); break;
            case WidgetProperty::Type::Float: lua_pushnumber(L, v.f); break;
            case WidgetProperty::Type::Bool:  lua_pushboolean(L, v.b); break;
            case WidgetProperty::Type::Str:   lua_pushstring(L, v.s.c_str()); break;
        }
    }
}

// ---------------- メモリ予算 ----------------

void* LuaEngine::Alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    LuaEngine* self = static_cast<LuaEngine*>(ud);
    const size_t old = ptr ? osize : 0;

    if (nsize == 0) {
        if (ptr) {
            self->used_ -= old;
            free(ptr);
        }
        return nullptr;
    }

    if (self->used_ - old + nsize > self->budget_) {
        return nullptr; // 予算超過。呼び出し元(Lua本体)はLUA_ERRMEMとして扱う
    }

    void* np = realloc(ptr, nsize);
    if (!np) return nullptr;

    self->used_ = self->used_ - old + nsize;
    return np;
}

int LuaEngine::InitTrampoline(lua_State* L) {
    LuaEngine* self = static_cast<LuaEngine*>(lua_touserdata(L, 1));
    luaL_openlibs(L);
    self->registerApi();
    return 0;
}

LuaEngine::LuaEngine(size_t budget_bytes) : budget_(budget_bytes) {
    L = lua_newstate(Alloc, this);
    if (!L) {
        LOG_APP_FAIL("LuaEngine: lua_newstateに失敗しました(予算%zuB)", budget_bytes);
        return;
    }

    // luaL_openlibs()やregisterApi()の途中でOOMになった場合、pcallで保護せずに
    // 直接呼ぶとLuaは(保護フレームが無いため)abort()してしまう
    // (script/host_test/lua_alloc_budget_test.cppで確認済み)。
    // 必ずpcall越しに呼ぶことでLUA_ERRMEMとして安全に失敗させる。
    lua_pushcfunction(L, InitTrampoline);
    lua_pushlightuserdata(L, this);
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
        LOG_APP_FAIL("LuaEngine: 初期化に失敗しました(予算%zuB): %s",
                     budget_bytes, lua_tostring(L, -1));
        lua_close(L);
        L = nullptr;
        return;
    }
}

LuaEngine::~LuaEngine() {
    if (L) lua_close(L);
}

bool LuaEngine::Run(const char* script, const char* chunkname) {
    if (!L) return false;

    if (luaL_loadbuffer(L, script, strlen(script), chunkname) != LUA_OK) {
        const char* msg = lua_tostring(L, -1);
        ErrorFunctions::ShowFatal(msg ? msg : "Luaスクリプトの構文エラー");
        lua_pop(L, 1);
        return false;
    }

    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        const char* msg = lua_tostring(L, -1);
        ErrorFunctions::ShowFatal(msg ? msg : "Luaスクリプトの実行時エラー");
        lua_pop(L, 1);
        return false;
    }

    return true;
}

// ---------------- pico.* API登録 ----------------

void LuaEngine::registerFn(const char* name, lua_CFunction fn) {
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, fn, 1);
    lua_setfield(L, -2, name); // -2 = pushしてあるpicoテーブル
}

void LuaEngine::registerApi() {
    lua_newtable(L);
    registerFn("create", l_create);
    registerFn("destroy", l_destroy);
    registerFn("set", l_set);
    registerFn("get", l_get);
    registerFn("on", l_on);
    registerFn("add_child", l_add_child);
    registerFn("log", l_log);
    registerFn("show_error", l_show_error);
    lua_setglobal(L, "pico");
}

// ---------------- コールバック中継 ----------------

bool LuaEngine::EventKindFromName(const char* name, EventKind& out) {
    static const struct { const char* name; EventKind kind; } kTable[] = {
        {"press_start", EventKind::PressStart},
        {"press_end", EventKind::PressEnd},
        {"press_move", EventKind::PressMove},
        {"press_out", EventKind::PressOut},
    };
    for (const auto& e : kTable) {
        if (strcmp(e.name, name) == 0) { out = e.kind; return true; }
    }
    return false;
}

void LuaEngine::BindCallback(Widget* w, WidgetId id, EventKind kind, int ref) {
    for (auto& e : callbacks_) {
        if (e.id == id && e.kind == kind) {
            // 同じid+kindへ再度onした場合は古いrefを捨てて差し替える(リーク防止)。
            // Widget側のstd::functionは初回のBindCallbackで既に配線済みなので繋ぎ直し不要
            luaL_unref(L, LUA_REGISTRYINDEX, e.ref);
            e.ref = ref;
            return;
        }
    }
    callbacks_.push_back({id, kind, ref});

    // キャプチャするのはthis(LuaEngine*)とid(WidgetId=uint32_t)だけなので、
    // std::functionの小バッファに収まりヒープ確保は起きない(クラスコメント参照)
    switch (kind) {
        case EventKind::PressStart:
            w->setOnPressStart([this, id]() { this->Dispatch(id, EventKind::PressStart); });
            break;
        case EventKind::PressEnd:
            w->setOnPressEnd([this, id]() { this->Dispatch(id, EventKind::PressEnd); });
            break;
        case EventKind::PressMove:
            w->setOnPressMove([this, id]() { this->Dispatch(id, EventKind::PressMove); });
            break;
        case EventKind::PressOut:
            w->setOnPressOut([this, id]() { this->Dispatch(id, EventKind::PressOut); });
            break;
    }
}

void LuaEngine::PruneCallbacksFor(WidgetId id) {
    for (auto it = callbacks_.begin(); it != callbacks_.end(); ) {
        if (it->id == id) {
            luaL_unref(L, LUA_REGISTRYINDEX, it->ref);
            it = callbacks_.erase(it);
        } else {
            ++it;
        }
    }
}

void LuaEngine::Dispatch(WidgetId id, EventKind kind) {
    int ref = LUA_NOREF;
    for (const auto& e : callbacks_) {
        if (e.id == id && e.kind == kind) { ref = e.ref; break; }
    }
    if (ref == LUA_NOREF) return; // pico.destroy()等で既に外れている

    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    lua_pushinteger(L, (lua_Integer)id);
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
        const char* msg = lua_tostring(L, -1);
        ErrorFunctions::ShowFatal(msg ? msg : "Luaコールバックでエラーが発生しました");
        lua_pop(L, 1);
    }
}

// ---------------- pico.* 関数本体 ----------------

int LuaEngine::l_create(lua_State* L) {
    const char* type_name = luaL_checkstring(L, 1);

    WidgetType type;
    if (!WidgetFactory::TypeFromName(type_name, type)) {
        return luaL_error(L, "pico.create: 未知のウィジェット種別 '%s'", type_name);
    }

    Widget* w = WidgetFactory::Create(type);
    if (!w) {
        return luaL_error(L, "pico.create: '%s' の生成に失敗しました(メモリ不足の可能性)", type_name);
    }

    WidgetFunctions::Add(w);
    lua_pushinteger(L, (lua_Integer)w->getId());
    return 1;
}

int LuaEngine::l_destroy(lua_State* L) {
    LuaEngine* self = Self(L);
    const WidgetId id = (WidgetId)luaL_checkinteger(L, 1);

    Widget* w = WidgetRegistry::Resolve(id);
    if (!w) return 0; // 既に無効なIDは黙って無視(二重destroyを許容)

    self->PruneCallbacksFor(id);
    WidgetFunctions::DestroyLater(w);
    return 0;
}

int LuaEngine::l_set(lua_State* L) {
    const WidgetId id = (WidgetId)luaL_checkinteger(L, 1);
    const char* name = luaL_checkstring(L, 2);

    Widget* w = WidgetRegistry::Resolve(id);
    if (!w) return luaL_error(L, "pico.set: 無効なID");

    WidgetProperty::Id pid;
    if (!WidgetProperty::IdFromName(name, pid)) {
        return luaL_error(L, "pico.set: 未知のプロパティ '%s'", name);
    }

    bool ok = false;
    switch (lua_type(L, 3)) {
        case LUA_TBOOLEAN:
            ok = WidgetProperty::Set(w, pid, WidgetProperty::Value::MakeBool(lua_toboolean(L, 3)));
            break;
        case LUA_TSTRING:
            ok = WidgetProperty::Set(w, pid, WidgetProperty::Value::MakeStr(lua_tostring(L, 3)));
            break;
        case LUA_TNUMBER: {
            const lua_Number n = lua_tonumber(L, 3);
            // Lua側は1と1.0を書き分けたがらないことが多いが、WidgetProperty側は
            // プロパティごとに期待する型(Int/Float)が固定なので、整数値に見えるなら
            // まずIntとして試し、駄目ならFloatとして試す
            if (n == (lua_Number)(int32_t)n &&
                WidgetProperty::Set(w, pid, WidgetProperty::Value::MakeInt((int32_t)n))) {
                ok = true;
            } else {
                ok = WidgetProperty::Set(w, pid, WidgetProperty::Value::MakeFloat((float)n));
            }
            break;
        }
        default:
            return luaL_error(L, "pico.set: '%s' に対応していない値の型です", name);
    }

    if (!ok) {
        return luaL_error(L, "pico.set: '%s' へは設定できません(型不一致または非対応)", name);
    }
    return 0;
}

int LuaEngine::l_get(lua_State* L) {
    const WidgetId id = (WidgetId)luaL_checkinteger(L, 1);
    const char* name = luaL_checkstring(L, 2);

    Widget* w = WidgetRegistry::Resolve(id);
    if (!w) return luaL_error(L, "pico.get: 無効なID");

    WidgetProperty::Id pid;
    if (!WidgetProperty::IdFromName(name, pid)) {
        return luaL_error(L, "pico.get: 未知のプロパティ '%s'", name);
    }

    WidgetProperty::Value v;
    if (!WidgetProperty::Get(w, pid, v)) {
        lua_pushnil(L); // 非対応の組み合わせは例外にせずnil(「無ければnil」というLuaの慣習に合わせる)
        return 1;
    }

    PushPropertyValue(L, v);
    return 1;
}

int LuaEngine::l_on(lua_State* L) {
    LuaEngine* self = Self(L);
    const WidgetId id = (WidgetId)luaL_checkinteger(L, 1);
    const char* ev = luaL_checkstring(L, 2);
    luaL_checktype(L, 3, LUA_TFUNCTION);

    Widget* w = WidgetRegistry::Resolve(id);
    if (!w) return luaL_error(L, "pico.on: 無効なID");

    EventKind kind;
    if (!EventKindFromName(ev, kind)) {
        return luaL_error(L, "pico.on: 未知のイベント '%s'", ev);
    }

    lua_pushvalue(L, 3);
    const int ref = luaL_ref(L, LUA_REGISTRYINDEX);

    self->BindCallback(w, id, kind, ref);
    return 0;
}

int LuaEngine::l_add_child(lua_State* L) {
    const WidgetId container_id = (WidgetId)luaL_checkinteger(L, 1);
    const WidgetId child_id = (WidgetId)luaL_checkinteger(L, 2);

    Widget* container = WidgetRegistry::Resolve(container_id);
    Widget* child = WidgetRegistry::Resolve(child_id);
    if (!container || !child) return luaL_error(L, "pico.add_child: 無効なID");

    // 一旦フラットな管理リスト(WidgetFunctions::widgets)から外し、コンテナのadd()が
    // 立てるneeds_children_updateによる次フレームの再登録で、コンテナの子として
    // 正しい位置(=コンテナより後、つまり上)へ入り直す。
    // これをしないと、生成順(child→containerの順で作った場合)によっては
    // 子がフラットリスト中で親より手前になり、親の描画で覆い隠されてしまう。
    WidgetFunctions::Remove(child);

    bool ok = true;
    switch (container->getWidgetType()) {
        case WidgetType::LayoutContainer:
            static_cast<LayoutContainer*>(container)->add(child);
            break;
        case WidgetType::GridContainer:
            static_cast<GridContainer*>(container)->add(child);
            break;
        case WidgetType::ScrollContainer:
            static_cast<ScrollContainer*>(container)->add(child);
            break;
        default:
            ok = false;
            break;
    }

    if (!ok) {
        // 対応外のコンテナ種別。取り外したままにせず元通り登録し直す
        WidgetFunctions::Add(child);
        return luaL_error(L, "pico.add_child: このウィジェット種別は子を追加できません");
    }
    return 0;
}

int LuaEngine::l_log(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);
    LOG_APP_MSG("%s", msg);
    return 0;
}

int LuaEngine::l_show_error(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);
    ErrorFunctions::ShowFatal(msg);
    return 0;
}
