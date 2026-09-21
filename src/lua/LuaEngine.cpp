#include "lua/LuaEngine.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "gui/widgets/Widget.hpp"
#include "gui/widgets/WidgetRegistry.hpp"
#include "gui/widgets/WidgetFactory.hpp"
#include "gui/widgets/WidgetProperty.hpp"
#include "gui/widgets/LayoutContainer.hpp"
#include "gui/widgets/GridContainer.hpp"
#include "gui/widgets/ScrollContainer.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/LuaCanvas.hpp"
#include "gui/icons/icon_render.h"
#include "functions/Widget_Functions.hpp"
#include "functions/Error_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "gui/scenes/Scene.hpp"
#include "storage/SD_IO.hpp"
#include "OS_Data.hpp"
#include "consts.hpp"

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

void LuaEngine::callGlobalNoArgs(const char* name) {
    if (!L) return;

    lua_getglobal(L, name);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return;
    }

    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        const char* msg = lua_tostring(L, -1);
        ErrorFunctions::ShowFatal(msg ? msg : "Luaスクリプトの実行時エラー");
        lua_pop(L, 1);
    }
}

void LuaEngine::CallSetup() {
    callGlobalNoArgs("setup");
}

void LuaEngine::CallLoop(uint32_t dt_ms) {
    if (!L || loop_broken_) return;

    lua_getglobal(L, "loop");
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return;
    }

    lua_pushinteger(L, (lua_Integer)dt_ms);
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
        // 毎フレーム同じエラーダイアログが積まれ続けないよう、以降はloop()を呼ばない
        loop_broken_ = true;
        const char* msg = lua_tostring(L, -1);
        ErrorFunctions::ShowFatal(msg ? msg : "loop()の実行時エラー");
        lua_pop(L, 1);
    }
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
    registerFn("pop", l_pop);
    registerFn("content_rect", l_content_rect);
    registerFn("invalidate", l_invalidate);
    registerFn("mark_dirty", l_mark_dirty);
    registerFn("draw_pixel", l_draw_pixel);
    registerFn("draw_line", l_draw_line);
    registerFn("draw_rect", l_draw_rect);
    registerFn("fill_rect", l_fill_rect);
    registerFn("draw_circle", l_draw_circle);
    registerFn("fill_circle", l_fill_circle);
    registerFn("clear_rect", l_clear_rect);
    registerFn("draw_text", l_draw_text);
    registerFn("set_draw_area", l_set_draw_area);
    registerFn("clear_draw_area", l_clear_draw_area);
    registerFn("draw_image", l_draw_image);
    registerFn("image_load", l_image_load);
    registerFn("image_size", l_image_size);
    registerFn("image_free", l_image_free);
    registerFn("sd_exists", l_sd_exists);
    registerFn("sd_read", l_sd_read);
    registerFn("sd_write", l_sd_write);
    registerFn("sd_remove", l_sd_remove);
    registerFn("sd_mkdir", l_sd_mkdir);
    registerFn("sd_list", l_sd_list);
    lua_setglobal(L, "pico");
}

// ---------------- コールバック中継 ----------------

bool LuaEngine::EventKindFromName(const char* name, EventKind& out) {
    static const struct { const char* name; EventKind kind; } kTable[] = {
        {"press_start", EventKind::PressStart},
        {"press_end", EventKind::PressEnd},
        {"press_move", EventKind::PressMove},
        {"press_out", EventKind::PressOut},
        {"render", EventKind::Render},
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
        case EventKind::Render:
            // l_on()側でLuaCanvasにしか許していないので安全にstatic_castできる
            static_cast<LuaCanvas*>(w)->setOnRender([this, id]() { this->Dispatch(id, EventKind::Render); });
            break;
    }
}

// ---------------- 画像ハンドル ----------------

uint32_t LuaEngine::MakeImageHandle(size_t index, uint32_t generation) {
    // 下位8bit=index+1(1始まり。0はhandle全体を無効値にするため使わない)、
    // 上位24bit=generation。kMaxLuaImagesは4なので8bitで十分過ぎるほど余裕がある
    return (generation << 8) | static_cast<uint32_t>(index + 1);
}

bool LuaEngine::ResolveImageHandle(uint32_t handle, size_t& out_index) const {
    const uint32_t index1 = handle & 0xFF;
    if (index1 == 0 || index1 > kMaxLuaImages) return false;

    const size_t index = index1 - 1;
    const ImageSlot& slot = images_[index];
    const uint32_t generation = handle >> 8;
    // usedを見ずgenerationだけで判定すると、解放直後(まだ再利用されていない)スロットの
    // 「今のgeneration」と「解放された側のhandleが持つ古いgeneration」がたまたま
    // 一致するケースは無い(Unregister相当で必ず1つ進めるため)が、それとは別に
    // 「そもそも今使用中か」も見ておく方が安全なので両方チェックする
    if (!slot.used || slot.generation == 0 || slot.generation != generation) return false;

    out_index = index;
    return true;
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

    if (kind == EventKind::Render && w->getWidgetType() != WidgetType::LuaCanvas) {
        return luaL_error(L, "pico.on: 'render'イベントはCanvas(pico.create(\"Canvas\"))のみ対応");
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

int LuaEngine::l_pop(lua_State*) {
    // LuaSceneがアプリを起動する際はSceneFunctions::Pushなので、Popでランチャへ戻れる
    // (ClocksScene/CalculatorScene等、他のアプリの「戻る」ボタンと同じ仕組み)。
    // 要求を登録するだけで実際の遷移はフレーム境界(SceneFunctions::Update())まで保留される
    SceneFunctions::Pop();
    return 0;
}

int LuaEngine::l_content_rect(lua_State* L) {
    // ステータスバーを除いた、シーンが自由に使える領域。他のC++製アプリと同じ
    // Scene::contentRect()を使うので、Luaアプリだけ位置がずれることはない
    const Rect r = Scene::contentRect();
    lua_pushinteger(L, r.x);
    lua_pushinteger(L, r.y);
    lua_pushinteger(L, r.w);
    lua_pushinteger(L, r.h);
    return 4;
}

int LuaEngine::l_invalidate(lua_State* L) {
    const WidgetId id = (WidgetId)luaL_checkinteger(L, 1);

    Widget* w = WidgetRegistry::Resolve(id);
    if (!w) return luaL_error(L, "pico.invalidate: 無効なID");

    // needsRender()はそのウィジェットの画面矩形をPICO_GFX::MarkDirty()し、
    // 次のFlushDirty()でrenderForce()(=LuaCanvasならrenderコールバック)が
    // 呼ばれるようにする。LuaCanvas以外の任意のウィジェットにも使える汎用API
    w->needsRender();
    return 0;
}

int LuaEngine::l_mark_dirty(lua_State* L) {
    const int16_t x = (int16_t)luaL_checkinteger(L, 1);
    const int16_t y = (int16_t)luaL_checkinteger(L, 2);
    const int16_t w = (int16_t)luaL_checkinteger(L, 3);
    const int16_t h = (int16_t)luaL_checkinteger(L, 4);

    // PICO_GFX::MarkDirty()の生の下請け。ウィジェットを介さず任意の矩形を
    // 直接dirty化したい場合向けの低レベルAPI(クラスコメント「直接描画」参照)
    PICO_GFX::MarkDirty({x, y, w, h});
    return 0;
}

// ---------------- 直接描画 ----------------
// クラスコメント「直接描画」参照。ウィジェットを介さずOSData::frameへ直接描き、
// 描いた範囲だけPICO_GFX::MarkDirty()する(既存の各種render()実装と同じ流儀)。
// 色は既存プロパティと同じくPICO 4bitパレット番号をそのままint8_tへキャストするだけで、
// 範囲チェックはしない(WidgetProperty::Setの色プロパティと同じ)。

int LuaEngine::l_draw_pixel(lua_State* L) {
    const int16_t x = (int16_t)luaL_checkinteger(L, 1);
    const int16_t y = (int16_t)luaL_checkinteger(L, 2);
    const int8_t color = (int8_t)luaL_checkinteger(L, 3);

    OSData::frame->drawPixel(x, y, color);
    PICO_GFX::MarkDirty({x, y, 1, 1});
    return 0;
}

int LuaEngine::l_draw_line(lua_State* L) {
    const int16_t x0 = (int16_t)luaL_checkinteger(L, 1);
    const int16_t y0 = (int16_t)luaL_checkinteger(L, 2);
    const int16_t x1 = (int16_t)luaL_checkinteger(L, 3);
    const int16_t y1 = (int16_t)luaL_checkinteger(L, 4);
    const int8_t color = (int8_t)luaL_checkinteger(L, 5);

    OSData::frame->drawLine(x0, y0, x1, y1, color);
    PICO_GFX::MarkDirty({
        (int16_t)std::min(x0, x1), (int16_t)std::min(y0, y1),
        (int16_t)(std::abs(x1 - x0) + 1), (int16_t)(std::abs(y1 - y0) + 1)
    });
    return 0;
}

int LuaEngine::l_draw_rect(lua_State* L) {
    const int16_t x = (int16_t)luaL_checkinteger(L, 1);
    const int16_t y = (int16_t)luaL_checkinteger(L, 2);
    const int16_t w = (int16_t)luaL_checkinteger(L, 3);
    const int16_t h = (int16_t)luaL_checkinteger(L, 4);
    const int8_t color = (int8_t)luaL_checkinteger(L, 5);

    OSData::frame->drawRect(x, y, w, h, color);
    PICO_GFX::MarkDirty({x, y, w, h});
    return 0;
}

int LuaEngine::l_fill_rect(lua_State* L) {
    const int16_t x = (int16_t)luaL_checkinteger(L, 1);
    const int16_t y = (int16_t)luaL_checkinteger(L, 2);
    const int16_t w = (int16_t)luaL_checkinteger(L, 3);
    const int16_t h = (int16_t)luaL_checkinteger(L, 4);
    const int8_t color = (int8_t)luaL_checkinteger(L, 5);

    OSData::frame->fillRect(x, y, w, h, color);
    PICO_GFX::MarkDirty({x, y, w, h});
    return 0;
}

int LuaEngine::l_draw_circle(lua_State* L) {
    const int16_t x = (int16_t)luaL_checkinteger(L, 1);
    const int16_t y = (int16_t)luaL_checkinteger(L, 2);
    const int16_t r = (int16_t)luaL_checkinteger(L, 3);
    const int8_t color = (int8_t)luaL_checkinteger(L, 4);

    OSData::frame->drawCircle(x, y, r, color);
    PICO_GFX::MarkDirty({(int16_t)(x - r), (int16_t)(y - r), (int16_t)(r * 2 + 1), (int16_t)(r * 2 + 1)});
    return 0;
}

int LuaEngine::l_fill_circle(lua_State* L) {
    const int16_t x = (int16_t)luaL_checkinteger(L, 1);
    const int16_t y = (int16_t)luaL_checkinteger(L, 2);
    const int16_t r = (int16_t)luaL_checkinteger(L, 3);
    const int8_t color = (int8_t)luaL_checkinteger(L, 4);

    OSData::frame->fillCircle(x, y, r, color);
    PICO_GFX::MarkDirty({(int16_t)(x - r), (int16_t)(y - r), (int16_t)(r * 2 + 1), (int16_t)(r * 2 + 1)});
    return 0;
}

int LuaEngine::l_clear_rect(lua_State* L) {
    const int16_t x = (int16_t)luaL_checkinteger(L, 1);
    const int16_t y = (int16_t)luaL_checkinteger(L, 2);
    const int16_t w = (int16_t)luaL_checkinteger(L, 3);
    const int16_t h = (int16_t)luaL_checkinteger(L, 4);
    const int8_t color = (int8_t)luaL_optinteger(L, 5, PICO_BACKGROUND);

    OSData::frame->fillRect(x, y, w, h, color);
    PICO_GFX::MarkDirty({x, y, w, h});
    return 0;
}

int LuaEngine::l_draw_text(lua_State* L) {
    const int16_t x = (int16_t)luaL_checkinteger(L, 1);
    const int16_t y = (int16_t)luaL_checkinteger(L, 2);
    const char* text = luaL_checkstring(L, 3);
    const int8_t color = (int8_t)luaL_optinteger(L, 4, PICO_FORECOLOR);
    const FontFn::FontSize size = (FontFn::FontSize)luaL_optinteger(L, 5, (lua_Integer)FontFn::Normal);

    // 右端をはみ出さないよう、幅は残りスクリーン幅に自動で収める(AppGrid::drawName()等と
    // 同じ理由でmaxWidth=0以下はDrawPlain側がクリップ無しとして扱ってしまうため先に弾く)
    const int16_t max_w = (int16_t)(SCREEN_WIDTH - x);
    if (max_w <= 0) return 0;

    Label<PICO_STR_M>::DrawPlain(size, color, x, y, max_w, text);

    const int16_t line_h = (int16_t)Label<PICO_STR_M>::GetLineHeight(size);
    PICO_GFX::MarkDirty({x, y, max_w, line_h});
    return 0;
}

int LuaEngine::l_draw_image(lua_State* L) {
    LuaEngine* self = Self(L);
    const uint32_t handle = (uint32_t)luaL_checkinteger(L, 1);
    const int16_t x = (int16_t)luaL_checkinteger(L, 2);
    const int16_t y = (int16_t)luaL_checkinteger(L, 3);

    size_t index;
    if (!self->ResolveImageHandle(handle, index)) {
        return luaL_error(L, "pico.draw_image: 無効なイメージハンドル");
    }

    ImageSlot& slot = self->images_[index];
    IconRender::DrawPimgSprite(slot.sprite, x, y);
    PICO_GFX::MarkDirty({x, y, (int16_t)slot.sprite.width, (int16_t)slot.sprite.height});
    return 0;
}

// ---------------- 直接描画エリア ----------------
// クラスコメント(ヘッダ)参照。OSData::frameのクリップ矩形を差し替えるだけの薄いラッパー。

int LuaEngine::l_set_draw_area(lua_State* L) {
    const int32_t x = (int32_t)luaL_checkinteger(L, 1);
    const int32_t y = (int32_t)luaL_checkinteger(L, 2);
    const int32_t w = (int32_t)luaL_checkinteger(L, 3);
    const int32_t h = (int32_t)luaL_checkinteger(L, 4);

    OSData::frame->setClipRect(x, y, w, h);
    return 0;
}

int LuaEngine::l_clear_draw_area(lua_State*) {
    OSData::frame->clearClipRect();
    return 0;
}

// ---------------- 画像 ----------------
// ヘッダのクラスコメント「画像」参照。`.pimg`のデコードそのものは
// IconRender::LoadPimgToSprite()(Imageウィジェットのonram=trueと同じ経路)を
// そのまま使い、このLuaEngineインスタンスの固定長スロットで持つだけ。

int LuaEngine::l_image_load(lua_State* L) {
    LuaEngine* self = Self(L);
    const char* path = luaL_checkstring(L, 1);

    if (!OSData::SD_usable) { lua_pushnil(L); return 1; }

    size_t index = kMaxLuaImages;
    for (size_t i = 0; i < kMaxLuaImages; ++i) {
        if (!self->images_[i].used) { index = i; break; }
    }
    if (index == kMaxLuaImages) {
        LOG_APP_WARN("pico.image_load: 同時に保持できる画像数の上限(%zu枚)に達しています: %s",
            kMaxLuaImages, path);
        lua_pushnil(L);
        return 1;
    }

    FsFile f = OSData::SD.open(path, O_RDONLY);
    if (!f) { lua_pushnil(L); return 1; }

    IconRender::PimgHeader header;
    if (!IconRender::ReadPimgHeader(f, header)) {
        f.close();
        lua_pushnil(L);
        return 1;
    }

    // 4bpp(1ピクセル半バイト)なので端数切り上げでバイト数を見積もる。
    // LGFX_Sprite側の実際の確保量は多少前後し得るが、予算チェックとしては十分な精度
    const size_t need_bytes = (static_cast<size_t>(header.width) * header.height + 1) / 2;
    if (self->image_bytes_used_ + need_bytes > kMaxLuaImageBytes) {
        f.close();
        LOG_APP_WARN("pico.image_load: %s の読み込みで画像用メモリの上限(%uB)を超えます",
            path, (unsigned)kMaxLuaImageBytes);
        lua_pushnil(L);
        return 1;
    }

    ImageSlot& slot = self->images_[index];
    const bool ok = IconRender::LoadPimgToSprite(f, slot.sprite);
    f.close();
    if (!ok) { lua_pushnil(L); return 1; }

    slot.used = true;
    slot.bytes = need_bytes;
    self->image_bytes_used_ += need_bytes;

    // generation 0 は「一度も使われていないスロット」の予約値なので、初回使用時だけ
    // 1へ進める。2回目以降はimage_free()側で既に進めてあるのでそのまま使う
    // (WidgetRegistry::Register()と同じ考え方)
    if (slot.generation == 0) slot.generation = 1;

    lua_pushinteger(L, (lua_Integer)MakeImageHandle(index, slot.generation));
    return 1;
}

int LuaEngine::l_image_size(lua_State* L) {
    LuaEngine* self = Self(L);
    const uint32_t handle = (uint32_t)luaL_checkinteger(L, 1);

    size_t index;
    if (!self->ResolveImageHandle(handle, index)) {
        return luaL_error(L, "pico.image_size: 無効なイメージハンドル");
    }

    const ImageSlot& slot = self->images_[index];
    lua_pushinteger(L, slot.sprite.width);
    lua_pushinteger(L, slot.sprite.height);
    return 2;
}

int LuaEngine::l_image_free(lua_State* L) {
    LuaEngine* self = Self(L);
    const uint32_t handle = (uint32_t)luaL_checkinteger(L, 1);

    size_t index;
    // 既に無効なハンドル(未割り当て/解放済み)はpico.destroyと同じく黙って無視し、
    // 二重解放をエラーにしない
    if (!self->ResolveImageHandle(handle, index)) return 0;

    ImageSlot& slot = self->images_[index];
    slot.sprite.sprite.deleteSprite();
    slot.sprite.usable = false;
    self->image_bytes_used_ -= slot.bytes;
    slot.bytes = 0;
    slot.used = false;

    // WidgetRegistry::Unregister()と同じく、ここでgenerationを進めておく
    // (次にこのスロットが再利用されたとき、解放済みの古いハンドルが新しい画像を
    // 指してしまわないようにするため。generationを0へ戻すだけだと「初回使用」と
    // 区別できず同じハンドル値を再発行してしまう)
    slot.generation++;
    if (slot.generation == 0) slot.generation = 1; // 0は予約値なのでwrapしたら1へ飛ばす
    return 0;
}

// ---------------- SDカードアクセス ----------------
// ヘッダのクラスコメント参照。OSData::SD_usable==falseの間はどれも失敗(false/nil)を
// 返すだけでluaL_errorにはしない。

int LuaEngine::l_sd_exists(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    lua_pushboolean(L, OSData::SD_usable && OSData::SD.exists(path));
    return 1;
}

int LuaEngine::l_sd_read(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    if (!OSData::SD_usable) { lua_pushnil(L); return 1; }

    FsFile f = OSData::SD.open(path, O_RDONLY);
    if (!f) { lua_pushnil(L); return 1; }

    const size_t file_size = f.fileSize();
    if (file_size > kMaxSdReadBytes) {
        f.close();
        LOG_APP_WARN("pico.sd_read: %s が上限(%uB)を超えています(%uB)",
            path, (unsigned)kMaxSdReadBytes, (unsigned)file_size);
        lua_pushnil(L);
        return 1;
    }

    // MarkdownView::load()/LuaScene::loadAndRun()と同じく、ファイル全体ぶんの
    // 一時バッファをヒープへ一度に確保せず、スタック上の小さなチャンクで読み進める
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    char chunk[256];
    size_t remaining = file_size;
    bool ok = true;
    while (remaining > 0) {
        const size_t want = (remaining < sizeof(chunk)) ? remaining : sizeof(chunk);
        const int got = f.read((uint8_t*)chunk, want);
        if (got <= 0) { ok = false; break; } // 読み取り失敗。読めたところまでで打ち切る
        luaL_addlstring(&b, chunk, (size_t)got);
        remaining -= (size_t)got;
    }
    f.close();

    if (!ok) {
        // luaL_Bufferへ積んだ分は使わず捨てる(luaL_pushresultしないままリターンして良い。
        // Luaのスタック上のuserdataはGCが回収する)
        lua_pushnil(L);
        return 1;
    }

    luaL_pushresult(&b);
    return 1;
}

int LuaEngine::l_sd_write(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    size_t len = 0;
    const char* data = luaL_checklstring(L, 2, &len);
    const bool append = lua_toboolean(L, 3);

    if (!OSData::SD_usable) { lua_pushboolean(L, false); return 1; }

    FsFile f = OSData::SD.open(path, O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC));
    if (!f) { lua_pushboolean(L, false); return 1; }

    const bool ok = (len == 0) || (f.write(data, len) == len);
    f.close();
    lua_pushboolean(L, ok);
    return 1;
}

int LuaEngine::l_sd_remove(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    if (!OSData::SD_usable) { lua_pushboolean(L, false); return 1; }

    // FileExplorer::on_press_delete()と同じ判断(ディレクトリなら再帰削除)
    FsFile f = OSData::SD.open(path);
    bool ok;
    if (!f) {
        ok = false;
    } else if (f.isDir()) {
        f.close();
        ok = PICO_IO::removeRecursive(path);
    } else {
        f.close();
        ok = OSData::SD.remove(path);
    }
    lua_pushboolean(L, ok);
    return 1;
}

int LuaEngine::l_sd_mkdir(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    if (!OSData::SD_usable) { lua_pushboolean(L, false); return 1; }

    lua_pushboolean(L, OSData::SD.mkdir(path));
    return 1;
}

int LuaEngine::l_sd_list(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    if (!OSData::SD_usable) { lua_pushnil(L); return 1; }

    FsFile dir = OSData::SD.open(path, O_RDONLY);
    if (!dir || !dir.isDir()) {
        if (dir) dir.close();
        lua_pushnil(L);
        return 1;
    }

    // FileExplorer::update_list()と同じ走査方法。{name=..., is_dir=...}の配列を返す
    lua_newtable(L);
    int idx = 1;
    FsFile file;
    char name[128];
    while (file.openNext(&dir, O_RDONLY)) {
        if (file.getName(name, sizeof(name))) {
            lua_newtable(L);
            lua_pushstring(L, name);
            lua_setfield(L, -2, "name");
            lua_pushboolean(L, file.isDir());
            lua_setfield(L, -2, "is_dir");
            lua_rawseti(L, -2, idx++);
        }
        file.close();
    }
    dir.close();
    return 1;
}
