// LuaEngine の拡張API(2026-10-05(2))。LuaEngine.cpp が大きくなったので、
//   - 見送っていたウィジェット(TextView/ImageView/MarkdownView/AnalogClock/DurationPicker/MonthGrid)の
//     イベントと補助関数
//   - 長押し/ダブルタップ/スワイプの判定、pico.off、ツリー探索・名前、リスト/タブの操作、スクロール
//   - Z順・矩形・有効/無効、トースト、戻る操作
//   - 描画の補助(get_pixel・揃え・折り返し・オフスクリーン画像)
//   - 暗号化以外のユーティリティ(パス・時刻・URL・base64・アプリ設定・メモリ情報)
// をここへ置く。LuaEngine の private メンバは friend struct LuaEngineExt 経由で触る。
// 暗号化は LuaEngine_Crypto.cpp。

#include "lua/LuaEngine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "gui/widgets/Widget.hpp"
#include "gui/widgets/WidgetRegistry.hpp"
#include "gui/widgets/WidgetFactory.hpp"
#include "gui/widgets/WidgetProperty.hpp"
#include "gui/widgets/Textbox.hpp"
#include "gui/widgets/NumberInput.hpp"
#include "gui/widgets/ScrollContainer.hpp"
#include "gui/widgets/ScrollList.hpp"
#include "gui/widgets/TabBar.hpp"
#include "gui/widgets/DropdownMenu.hpp"
#include "gui/widgets/CanvasRaster.hpp"
#include "gui/widgets/TextView.hpp"
#include "gui/widgets/ImageView.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/dialogs/PickerDialog.hpp"
#include "gui/widgets/apps/MarkdownView.hpp"
#include "gui/widgets/apps/DurationPicker.hpp"
#include "gui/widgets/apps/MonthGrid.hpp"
#include "gui/icons/icon_render.h"
#include "functions/GFX_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Keyboard_Functions.hpp"
#include "functions/Notification_Functions.hpp"
#include "functions/Power_Functions.hpp"
#include "functions/Network_Functions.hpp"
#include "functions/Mem_Functions.hpp"
#include "functions/Config_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Pad_Functions.hpp"
#include "storage/SD_IO.hpp"
#include "util/Base64.hpp"
#include "OS_Data.hpp"
#include "consts.hpp"
#include <Arduino.h>

using TextboxT = Textbox<WidgetFactory::kTextboxCapacity>;

struct LuaEngineExt {
    static LuaEngine* Self(lua_State* L) {
        return static_cast<LuaEngine*>(lua_touserdata(L, lua_upvalueindex(1)));
    }

    static Widget* WidgetArg(lua_State* L, int idx, const char* api) {
        const WidgetId id = (WidgetId)luaL_checkinteger(L, idx);
        Widget* w = WidgetRegistry::Resolve(id);
        if (!w) luaL_error(L, "%s: 無効なID", api);
        return w;
    }

    // ---------------- イベント ----------------
    static int l_off(lua_State* L);

    // ---------------- ウィジェットの補助 ----------------
    static int l_text_set(lua_State* L);
    static int l_set_dots(lua_State* L);
    static int l_set_name(lua_State* L);
    static int l_find(lua_State* L);
    static int l_parent(lua_State* L);
    static int l_children(lua_State* L);
    static int l_get_rect(lua_State* L);
    static int l_bring_to_front(lua_State* L);
    static int l_send_to_back(lua_State* L);
    static int l_show_keyboard(lua_State* L);
    static int l_hide_keyboard(lua_State* L);
    static int l_scroll_to(lua_State* L);

    // ---------------- 選ぶ・入れるダイアログ ----------------
    static int l_show_choice(lua_State* L);
    static int l_show_date(lua_State* L);
    static int l_show_time(lua_State* L);
    static int l_show_number(lua_State* L);
    static int l_show_progress(lua_State* L);
    static int FinishPicker(lua_State* L, PickerDialog* dialog, const char* api);

    // ---------------- リスト・タブ ----------------
    static int l_list_add(lua_State* L);
    static int l_list_insert(lua_State* L);
    static int l_list_remove(lua_State* L);
    static int l_list_get(lua_State* L);
    static int l_list_select(lua_State* L);
    static int l_list_scroll_to(lua_State* L);
    static int l_tab_set_label(lua_State* L);
    static int l_tab_label(lua_State* L);
    static int l_tab_remove(lua_State* L);
    static int l_tab_clear(lua_State* L);
    static int l_tab_link(lua_State* L);
    static int l_tab_unlink(lua_State* L);

    // ---------------- 通知・戻る ----------------
    static int l_toast(lua_State* L);
    static int l_keep_awake(lua_State* L);
    static int l_on_back(lua_State* L);
    static int l_go_back(lua_State* L);

    // ---------------- 描画の補助 ----------------
    static int l_get_pixel(lua_State* L);
    static int l_set_palette(lua_State* L);
    static int l_get_palette(lua_State* L);
    static int l_reset_palette(lua_State* L);
    static void SyncImagePalettes(LuaEngine* self);
    static int l_canvas_get_pixel(lua_State* L);
    static int l_draw_text_wrapped(lua_State* L);
    static int l_measure_text(lua_State* L);
    static int l_image_create(lua_State* L);
    static int l_image_target(lua_State* L);
    static int l_image_clear(lua_State* L);
    static int l_draw_tilemap(lua_State* L);
    static int l_image_rotate(lua_State* L);
    static int l_draw_rotated(lua_State* L);
    static int AllocImageSlot(LuaEngine* self, int w, int h, bool transparent, const char* fn);

    // ---------------- ユーティリティ ----------------
    static int l_app_dir(lua_State* L);
    static int l_path_join(lua_State* L);
    static int l_time(lua_State* L);
    static int l_wifi_status(lua_State* L);
    static int l_url_encode(lua_State* L);
    static int l_url_decode(lua_State* L);
    static int l_base64_encode(lua_State* L);
    static int l_base64_decode(lua_State* L);
    static int l_settings_get(lua_State* L);
    static int l_settings_set(lua_State* L);
    static int l_settings_all(lua_State* L);
    static int l_memory_info(lua_State* L);
};

// ===================================================================
// イベント
// ===================================================================

bool LuaEngine::BeginDispatch(WidgetId id, EventKind kind) {
    CallbackBinding* e = FindCallback(id, kind);
    if (!e) return false;
    lua_rawgeti(L, LUA_REGISTRYINDEX, e->ref);
    lua_pushinteger(L, (lua_Integer)id);
    return true;
}

void LuaEngine::EndDispatch(int extra_args) {
    if (ProtectedCall(1 + extra_args) != LUA_OK) {
        ReportError("Luaコールバックでエラーが発生しました");
    }
}

bool LuaEngine::CheckExtEventTarget(EventKind kind, Widget* w, const char** why) const {
    const WidgetType t = w->getWidgetType();
    switch (kind) {
        case EventKind::DurationChanged:
            if (t == WidgetType::DurationPicker) return true;
            *why = "DurationPicker"; return false;
        case EventKind::DaySelected:
            if (t == WidgetType::MonthGrid) return true;
            *why = "MonthGrid"; return false;
        case EventKind::LinkTap:
            if (t == WidgetType::MarkdownView) return true;
            *why = "MarkdownView"; return false;
        case EventKind::TextTap:
            if (t == WidgetType::TextView) return true;
            *why = "TextView"; return false;
        case EventKind::TextInput:
            if (t == WidgetType::Textbox) return true;
            *why = "Textbox"; return false;
        case EventKind::Scrolled:
            if (t == WidgetType::ScrollContainer) return true;
            *why = "ScrollContainer"; return false;
        default:
            return true; // ほかは既存の検査(LuaEngine.cpp)か、どのウィジェットでもよい(ジェスチャー)
    }
}

void LuaEngine::BindExtCallback(Widget* w, WidgetId id, EventKind kind) {
    // どれもキャプチャはthis(LuaEngine*)とid(4B)だけなので、std::functionの小バッファに収まる
    switch (kind) {
        case EventKind::DurationChanged:
            static_cast<DurationPicker*>(w)->setOnChanged([this, id](uint32_t ms) {
                if (!BeginDispatch(id, EventKind::DurationChanged)) return;
                lua_pushinteger(L, (lua_Integer)ms);
                EndDispatch(1);
            });
            break;
        case EventKind::DaySelected:
            static_cast<MonthGrid*>(w)->setOnSelectDay([this, id](int day) {
                if (!BeginDispatch(id, EventKind::DaySelected)) return;
                lua_pushinteger(L, day);
                EndDispatch(1);
            });
            break;
        case EventKind::LinkTap:
            static_cast<MarkdownView*>(w)->setOnLinkTap([this, id](FixedString<PICO_PATH_LEN> path) {
                if (!BeginDispatch(id, EventKind::LinkTap)) return;
                lua_pushstring(L, path.c_str());
                EndDispatch(1);
            });
            break;
        case EventKind::TextTap:
            static_cast<TextView*>(w)->setOnTap([this, id](size_t offset) {
                if (!BeginDispatch(id, EventKind::TextTap)) return;
                lua_pushinteger(L, (lua_Integer)offset);
                EndDispatch(1);
            });
            break;
        case EventKind::TextInput:
            static_cast<TextboxT*>(w)->setOnTextInput([this, id]() {
                if (BeginDispatch(id, EventKind::TextInput)) EndDispatch(0);
            });
            break;
        case EventKind::Scrolled:
            static_cast<ScrollContainer*>(w)->setOnScrolled([this, id]() {
                if (!BeginDispatch(id, EventKind::Scrolled)) return;
                Widget* sc = WidgetRegistry::Resolve(id);
                lua_pushinteger(L, sc ? static_cast<ScrollContainer*>(sc)->getScrollY() : 0);
                EndDispatch(1);
            });
            break;
        default:
            break; // ジェスチャーはUpdateGestures()が判定する(ウィジェット側の配線は無い)
    }
}

void LuaEngine::OnTabChanged(WidgetId tab) {
    ApplyTabLinks(tab);
    Dispatch(tab, EventKind::TabChanged);
}

void LuaEngine::ApplyTabLinks(WidgetId tab) {
    Widget* tw = WidgetRegistry::Resolve(tab);
    if (!tw || tw->getWidgetType() != WidgetType::TabBar) return;
    const int sel = static_cast<TabBar*>(tw)->getSelected();
    for (const TabLink& l : tab_links_) {
        if (l.tab != tab) continue;
        if (Widget* target = WidgetRegistry::Resolve(l.target)) target->setVisible(l.index == sel);
    }
}

int LuaEngineExt::l_off(lua_State* L) {
    LuaEngine* self = Self(L);
    const WidgetId id = (WidgetId)luaL_checkinteger(L, 1);
    const char* ev = luaL_checkstring(L, 2);
    LuaEngine::EventKind kind;
    if (!LuaEngine::EventKindFromName(ev, kind)) return luaL_error(L, "pico.off: 未知のイベント '%s'", ev);

    auto it = std::lower_bound(self->callbacks_.begin(), self->callbacks_.end(), id,
        [](const LuaEngine::CallbackBinding& e, WidgetId key) { return e.id < key; });
    for (; it != self->callbacks_.end() && it->id == id; ++it) {
        if (it->kind == kind) {
            luaL_unref(L, LUA_REGISTRYINDEX, it->ref);
            self->callbacks_.erase(it);
            lua_pushboolean(L, true);
            return 1;
        }
    }
    lua_pushboolean(L, false);
    return 1;
}

// ===================================================================
// 長押し・ダブルタップ・スワイプ
// ===================================================================

void LuaEngine::UpdateGestures() {
    if (!L) return;

    // ジェスチャーのコールバックが1つも無ければ何もしない(大半のアプリ)
    bool any = false;
    for (const CallbackBinding& b : callbacks_) {
        if (b.kind == EventKind::LongPress || b.kind == EventKind::DoubleTap || b.kind == EventKind::Swipe) {
            any = true;
            break;
        }
    }
    if (!any) { gesture_.active = false; return; }

    constexpr int kSlopPx = 10;            // これ以内の動きは「動いていない」
    constexpr uint32_t kLongPressMs = 500;
    constexpr uint32_t kTapMaxMs = 400;
    constexpr uint32_t kDoubleTapGapMs = 400;
    constexpr int kDoubleTapDistPx = 24;
    constexpr int kSwipeMinPx = 24;
    constexpr uint32_t kSwipeMaxMs = 700;

    const uint32_t now = (uint32_t)millis();
    GestureState& g = gesture_;

    // 発火先を先に集める(コールバックがpico.on/offでcallbacks_を変えても壊れないように)
    struct Target { WidgetId id; };
    auto collect = [this](EventKind kind, int x, int y, Target* out, int max) {
        int n = 0;
        for (const CallbackBinding& b : callbacks_) {
            if (b.kind != kind || n >= max) continue;
            Widget* w = WidgetRegistry::Resolve(b.id);
            if (!w || !w->getVisible() || !w->isEffectivelyEnabled()) continue;
            // hit_transparentのウィジェットでも、矩形の中ならジェスチャーは受ける
            const Rect r = w->clippedScreenRect();
            if (x < r.x || x >= r.x + r.w || y < r.y || y >= r.y + r.h) continue;
            out[n++] = {b.id};
        }
        return n;
    };
    auto fire_point = [this](EventKind kind, WidgetId id, int x, int y) {
        if (!BeginDispatch(id, kind)) return;
        int lx = x, ly = y;
        if (Widget* w = WidgetRegistry::Resolve(id)) {
            const Rect r = w->getScreenRect();
            lx = x - r.x;
            ly = y - r.y;
        }
        lua_pushinteger(L, x); lua_pushinteger(L, y);
        lua_pushinteger(L, lx); lua_pushinteger(L, ly);
        EndDispatch(4);
    };

    if (OSData::isTouchStart) {
        g.active = true;
        g.long_fired = false;
        g.moved = false;
        g.sx = OSData::touchX;
        g.sy = OSData::touchY;
        g.t0 = now;
    }
    if (!g.active) return;

    const int dx = (int)OSData::touchX - g.sx;
    const int dy = (int)OSData::touchY - g.sy;
    if (std::abs(dx) > kSlopPx || std::abs(dy) > kSlopPx) g.moved = true;

    // 長押し: 動かさずに押し続けた
    if (!g.long_fired && !g.moved && OSData::isTouched && !OSData::isTouchEnd && now - g.t0 >= kLongPressMs) {
        g.long_fired = true;
        Target t[8];
        const int n = collect(EventKind::LongPress, g.sx, g.sy, t, 8);
        for (int i = 0; i < n; i++) fire_point(EventKind::LongPress, t[i].id, g.sx, g.sy);
    }

    const bool ended = OSData::isTouchEnd || !OSData::isTouched;
    if (!ended) return;
    g.active = false;
    if (g.long_fired) return; // 長押しで消費した

    const uint32_t dur = now - g.t0;
    if (g.moved) {
        // スワイプ: 十分な距離を、素早く動かした
        const int ax = std::abs(dx), ay = std::abs(dy);
        if (std::max(ax, ay) >= kSwipeMinPx && dur <= kSwipeMaxMs) {
            const char* dir = ax >= ay ? (dx > 0 ? "right" : "left") : (dy > 0 ? "down" : "up");
            Target t[8];
            const int n = collect(EventKind::Swipe, g.sx, g.sy, t, 8);
            for (int i = 0; i < n; i++) {
                if (!BeginDispatch(t[i].id, EventKind::Swipe)) continue;
                lua_pushstring(L, dir);
                lua_pushinteger(L, dx); lua_pushinteger(L, dy);
                lua_pushinteger(L, OSData::touchX); lua_pushinteger(L, OSData::touchY);
                EndDispatch(5);
            }
        }
        g.last_tap_ms = 0;
        return;
    }

    // ダブルタップ: 短いタップが近い位置で2回続いた
    if (dur <= kTapMaxMs) {
        if (g.last_tap_ms != 0 && now - g.last_tap_ms <= kDoubleTapGapMs
            && std::abs(g.sx - g.last_tap_x) <= kDoubleTapDistPx
            && std::abs(g.sy - g.last_tap_y) <= kDoubleTapDistPx) {
            g.last_tap_ms = 0;
            Target t[8];
            const int n = collect(EventKind::DoubleTap, g.sx, g.sy, t, 8);
            for (int i = 0; i < n; i++) fire_point(EventKind::DoubleTap, t[i].id, g.sx, g.sy);
        } else {
            g.last_tap_ms = now ? now : 1;
            g.last_tap_x = g.sx;
            g.last_tap_y = g.sy;
        }
    } else {
        g.last_tap_ms = 0;
    }
}

// ===================================================================
// 戻る操作
// ===================================================================

int LuaEngineExt::l_on_back(lua_State* L) {
    LuaEngine* self = Self(L);
    if (lua_isnoneornil(L, 1)) {
        if (self->back_callback_ref_ != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, self->back_callback_ref_);
        self->back_callback_ref_ = LUA_NOREF;
        return 0;
    }
    luaL_checktype(L, 1, LUA_TFUNCTION);
    if (self->back_callback_ref_ != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, self->back_callback_ref_);
    lua_pushvalue(L, 1);
    self->back_callback_ref_ = luaL_ref(L, LUA_REGISTRYINDEX);
    return 0;
}

bool LuaEngine::DispatchBack() {
    if (!L || back_callback_ref_ == LUA_NOREF) return false;
    lua_rawgeti(L, LUA_REGISTRYINDEX, back_callback_ref_);
    if (ProtectedCall(0, 1) != LUA_OK) {
        luaL_unref(L, LUA_REGISTRYINDEX, back_callback_ref_);
        back_callback_ref_ = LUA_NOREF;
        ReportError("on_back()の実行時エラー");
        return true;
    }
    // falseを返したら「取らなかった」(既定の戻る動作に任せる)。それ以外(nilも)は取った扱い
    const bool declined = lua_isboolean(L, -1) && !lua_toboolean(L, -1);
    lua_pop(L, 1);
    return !declined;
}

int LuaEngineExt::l_go_back(lua_State* L) {
    LuaEngine* self = Self(L);
    if (self->DispatchBack()) return 0;
    SceneFunctions::Pop();
    return 0;
}

// ===================================================================
// ウィジェットの補助
// ===================================================================

int LuaEngineExt::l_text_set(lua_State* L) {
    Widget* w = WidgetArg(L, 1, "pico.text_set");
    size_t len = 0;
    const char* text = luaL_checklstring(L, 2, &len);
    switch (w->getWidgetType()) {
        case WidgetType::TextView:
            // 16KiBまで。超えた分は切れる(falseを返す)
            lua_pushboolean(L, static_cast<TextView*>(w)->setOwnedText(text, len));
            return 1;
        case WidgetType::MarkdownView:
            // 8KiBまで(超えた分は切れる)。画像の相対参照はルート基準になる
            lua_pushboolean(L, static_cast<MarkdownView*>(w)->loadText(text, len));
            return 1;
        default:
            return luaL_error(L, "pico.text_set: TextView/MarkdownViewのみ対応");
    }
}

int LuaEngineExt::l_set_dots(lua_State* L) {
    Widget* w = WidgetArg(L, 1, "pico.set_dots");
    if (w->getWidgetType() != WidgetType::MonthGrid) return luaL_error(L, "pico.set_dots: MonthGridのみ対応");
    luaL_checktype(L, 2, LUA_TTABLE);

    MonthGrid::DayDots dots[32];
    for (int day = 1; day <= 31; day++) {
        lua_rawgeti(L, 2, day);
        if (lua_istable(L, -1)) {
            const int n = (int)std::min<lua_Unsigned>(lua_rawlen(L, -1), MonthGrid::kMaxDots);
            for (int i = 0; i < n; i++) {
                lua_rawgeti(L, -1, i + 1);
                dots[day].colors[i] = (int8_t)lua_tointeger(L, -1);
                lua_pop(L, 1);
            }
            dots[day].count = (uint8_t)n;
        } else if (lua_isinteger(L, -1)) {
            dots[day].colors[0] = (int8_t)lua_tointeger(L, -1);
            dots[day].count = 1;
        }
        lua_pop(L, 1);
    }
    static_cast<MonthGrid*>(w)->setDots(dots);
    return 0;
}

int LuaEngineExt::l_set_name(lua_State* L) {
    LuaEngine* self = Self(L);
    Widget* w = WidgetArg(L, 1, "pico.set_name");
    const char* name = luaL_checkstring(L, 2);
    if (!name[0] || strlen(name) >= PICO_STR_S) {
        return luaL_error(L, "pico.set_name: 名前は1〜%d文字です", PICO_STR_S - 1);
    }
    const WidgetId id = w->getId();
    // 名前は一意。同じ名前が既にあれば付け替える
    for (auto& e : self->names_) {
        if (e.name == name) { e.id = id; return 0; }
    }
    for (auto& e : self->names_) {
        if (e.id == id) { e.name.assign(name); return 0; }
    }
    if (self->names_.size() >= LuaEngine::kMaxNames) return luaL_error(L, "pico.set_name: 名前は%zu個までです", LuaEngine::kMaxNames);
    LuaEngine::NameEntry ne;
    ne.id = id;
    ne.name.assign(name);
    self->names_.push_back(ne);
    return 0;
}

int LuaEngineExt::l_find(lua_State* L) {
    LuaEngine* self = Self(L);
    const char* name = luaL_checkstring(L, 1);
    for (const auto& e : self->names_) {
        if (e.name == name) {
            if (WidgetRegistry::Resolve(e.id)) { lua_pushinteger(L, (lua_Integer)e.id); return 1; }
        }
    }
    lua_pushnil(L);
    return 1;
}

int LuaEngineExt::l_parent(lua_State* L) {
    Widget* w = WidgetArg(L, 1, "pico.parent");
    Widget* p = w->getParent();
    if (p) lua_pushinteger(L, (lua_Integer)p->getId());
    else lua_pushnil(L);
    return 1;
}

int LuaEngineExt::l_children(lua_State* L) {
    Widget* w = WidgetArg(L, 1, "pico.children");
    const std::vector<Widget*>& ch = w->getChildren();
    lua_createtable(L, (int)ch.size(), 0);
    int n = 0;
    for (Widget* c : ch) {
        if (!c) continue;
        lua_pushinteger(L, (lua_Integer)c->getId());
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}

int LuaEngineExt::l_get_rect(lua_State* L) {
    Widget* w = WidgetArg(L, 1, "pico.get_rect");
    const Rect r = w->getScreenRect();
    lua_pushinteger(L, r.x); lua_pushinteger(L, r.y);
    lua_pushinteger(L, r.w); lua_pushinteger(L, r.h);
    return 4;
}

int LuaEngineExt::l_bring_to_front(lua_State* L) {
    Widget* w = WidgetArg(L, 1, "pico.bring_to_front");
    // 通常レイヤの根だけが対象(コンテナの子の重なりはコンテナの中の並びで決まる)
    if (w->getParent()) { lua_pushboolean(L, false); return 1; }
    WidgetFunctions::BringToFrontTree(w);
    lua_pushboolean(L, true);
    return 1;
}

int LuaEngineExt::l_send_to_back(lua_State* L) {
    Widget* w = WidgetArg(L, 1, "pico.send_to_back");
    if (w->getParent()) { lua_pushboolean(L, false); return 1; }
    WidgetFunctions::SendToBackTree(w);
    lua_pushboolean(L, true);
    return 1;
}

int LuaEngineExt::l_show_keyboard(lua_State* L) {
    Widget* w = WidgetArg(L, 1, "pico.show_keyboard");
    switch (w->getWidgetType()) {
        case WidgetType::Textbox:
        case WidgetType::NumberInput:
            w->causeOnPressStart(); // タップしたときと同じ(キーボードを開く)
            return 0;
        default:
            return luaL_error(L, "pico.show_keyboard: Textbox/NumberInputのみ対応");
    }
}

int LuaEngineExt::l_hide_keyboard(lua_State* L) {
    (void)L;
    KeyboardFunctions::HideAll();
    return 0;
}

int LuaEngineExt::l_scroll_to(lua_State* L) {
    Widget* w = WidgetArg(L, 1, "pico.scroll_to");
    if (w->getWidgetType() != WidgetType::ScrollContainer) return luaL_error(L, "pico.scroll_to: ScrollContainerのみ対応");
    Widget* child = WidgetArg(L, 2, "pico.scroll_to");
    lua_pushboolean(L, static_cast<ScrollContainer*>(w)->scrollToChild(child));
    return 1;
}


// ===================================================================
// 選ぶ・入れるダイアログ(PickerDialog)
// ===================================================================

int LuaEngineExt::FinishPicker(lua_State* L, PickerDialog* dialog, const char* api) {
    if (!dialog) return luaL_error(L, "%s: 生成に失敗しました(メモリ不足の可能性)", api);
    dialog->layout();
    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    const WidgetId id = dialog->getId();
    Self(L)->WireDialogClosed(dialog, id);
    lua_pushinteger(L, (lua_Integer)id);
    return 1;
}

// pico.show_choice(title, items [, cancel_text]) -> id
//   itemsは文字列の配列。1回タップで選んで閉じ、closed(id, true, 選んだ番号(0始まり))。
//   キャンセルで closed(id, false)。cancel_textを""にするとキャンセルボタンを出さない
int LuaEngineExt::l_show_choice(lua_State* L) {
    const char* title = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    const char* cancel_text = luaL_optstring(L, 3, "キャンセル");
    PickerDialog* d = new PickerDialog(PickerDialog::Mode::Choice, title, "", cancel_text);
    if (!d) return luaL_error(L, "pico.show_choice: 生成に失敗しました(メモリ不足の可能性)");
    const int n = (int)std::min<lua_Unsigned>(lua_rawlen(L, 2), 64);
    for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, 2, i);
        const char* s = lua_tostring(L, -1);
        d->addChoice(s ? s : "");
        lua_pop(L, 1);
    }
    return FinishPicker(L, d, "pico.show_choice");
}

// pico.show_date(title, year, month, day [, ok_text [, cancel_text]]) -> id
//   closed(id, true, "YYYY-MM-DD")
int LuaEngineExt::l_show_date(lua_State* L) {
    const char* title = luaL_checkstring(L, 1);
    const int y = (int)luaL_checkinteger(L, 2);
    const int m = (int)luaL_checkinteger(L, 3);
    const int day = (int)luaL_checkinteger(L, 4);
    const char* ok_text = luaL_optstring(L, 5, "決定");
    const char* cancel_text = luaL_optstring(L, 6, "キャンセル");
    PickerDialog* d = new PickerDialog(PickerDialog::Mode::Date, title, ok_text, cancel_text);
    if (!d) return luaL_error(L, "pico.show_date: 生成に失敗しました(メモリ不足の可能性)");
    d->setDate(y, m, day);
    return FinishPicker(L, d, "pico.show_date");
}

// pico.show_time(title, hour, minute [, second [, ok_text [, cancel_text]]]) -> id
//   closed(id, true, "HH:MM:SS")
int LuaEngineExt::l_show_time(lua_State* L) {
    const char* title = luaL_checkstring(L, 1);
    const int h = (int)luaL_checkinteger(L, 2);
    const int m = (int)luaL_checkinteger(L, 3);
    const int s = (int)luaL_optinteger(L, 4, 0);
    const char* ok_text = luaL_optstring(L, 5, "決定");
    const char* cancel_text = luaL_optstring(L, 6, "キャンセル");
    PickerDialog* d = new PickerDialog(PickerDialog::Mode::Time, title, ok_text, cancel_text);
    if (!d) return luaL_error(L, "pico.show_time: 生成に失敗しました(メモリ不足の可能性)");
    d->setTime(h, m, s);
    return FinishPicker(L, d, "pico.show_time");
}

// pico.show_number(title [, initial [, ok_text [, cancel_text]]]) -> id
//   数字専用のキーボードで入れる。closed(id, true, "入力した文字列")(tonumberで数値にする)
int LuaEngineExt::l_show_number(lua_State* L) {
    const char* title = luaL_checkstring(L, 1);
    const char* initial = "";
    char num[48];
    if (lua_isnumber(L, 2)) {
        if (lua_isinteger(L, 2)) snprintf(num, sizeof(num), "%lld", (long long)lua_tointeger(L, 2));
        else snprintf(num, sizeof(num), "%g", (double)lua_tonumber(L, 2));
        initial = num;
    } else if (lua_isstring(L, 2)) {
        initial = lua_tostring(L, 2);
    }
    const char* ok_text = luaL_optstring(L, 3, "決定");
    const char* cancel_text = luaL_optstring(L, 4, "キャンセル");
    PickerDialog* d = new PickerDialog(PickerDialog::Mode::Number, title, ok_text, cancel_text);
    if (!d) return luaL_error(L, "pico.show_number: 生成に失敗しました(メモリ不足の可能性)");
    d->setNumber(initial);
    return FinishPicker(L, d, "pico.show_number");
}

// pico.show_progress(message [, cancel_text]) -> id
//   進捗バー付きのダイアログ。pico.set(id, "value", 0〜100)で進め、pico.set(id, "text", ...)でメッセージを変える。
//   閉じるのは pico.destroy(id)(終わったとき)か、キャンセル(cancel_textを渡したときだけボタンが出る。closed(id,false))
int LuaEngineExt::l_show_progress(lua_State* L) {
    const char* message = luaL_checkstring(L, 1);
    const char* cancel_text = luaL_optstring(L, 2, "");
    PickerDialog* d = new PickerDialog(PickerDialog::Mode::Progress, message, "", cancel_text);
    if (!d) return luaL_error(L, "pico.show_progress: 生成に失敗しました(メモリ不足の可能性)");
    return FinishPicker(L, d, "pico.show_progress");
}

// ===================================================================
// リスト・ドロップダウン(インデックスは0始まり。selected_indexと同じ)
// ===================================================================

namespace {
    // 項目の追加情報: opts={icon=IconID, color=パレット番号}
    void ItemOptions(lua_State* L, int idx, ScrollListTools::Item& item) {
        if (!lua_istable(L, idx)) return;
        lua_getfield(L, idx, "icon");
        if (lua_isinteger(L, -1)) {
            const lua_Integer v = lua_tointeger(L, -1);
            if (v >= 0 && v < (lua_Integer)IconID::IconCount) item.icon = (IconID)v;
        }
        lua_pop(L, 1);
        lua_getfield(L, idx, "color");
        if (lua_isinteger(L, -1)) item.color = (int8_t)lua_tointeger(L, -1);
        lua_pop(L, 1);
    }
}

int LuaEngineExt::l_list_add(lua_State* L) {
    Widget* w = WidgetArg(L, 1, "pico.list_add");
    const char* text = luaL_checkstring(L, 2);
    switch (w->getWidgetType()) {
        case WidgetType::ScrollList: {
            ScrollListTools::Item item;
            item.text.assign(text);
            ItemOptions(L, 3, item);
            static_cast<ScrollList*>(w)->add(item);
            return 0;
        }
        case WidgetType::DropdownMenu:
            static_cast<DropdownMenu*>(w)->add(text);
            return 0;
        default:
            return luaL_error(L, "pico.list_add: ScrollList/DropdownMenuのみ対応");
    }
}

int LuaEngineExt::l_list_insert(lua_State* L) {
    Widget* w = WidgetArg(L, 1, "pico.list_insert");
    const int index = (int)luaL_checkinteger(L, 2);
    const char* text = luaL_checkstring(L, 3);
    if (w->getWidgetType() != WidgetType::ScrollList) return luaL_error(L, "pico.list_insert: ScrollListのみ対応");
    ScrollListTools::Item item;
    item.text.assign(text);
    ItemOptions(L, 4, item);
    static_cast<ScrollList*>(w)->insertAt(index, item);
    return 0;
}

int LuaEngineExt::l_list_remove(lua_State* L) {
    Widget* w = WidgetArg(L, 1, "pico.list_remove");
    const int index = (int)luaL_checkinteger(L, 2);
    switch (w->getWidgetType()) {
        case WidgetType::ScrollList:
            lua_pushboolean(L, static_cast<ScrollList*>(w)->removeAt(index));
            return 1;
        case WidgetType::DropdownMenu:
            lua_pushboolean(L, static_cast<DropdownMenu*>(w)->removeAt(index));
            return 1;
        default:
            return luaL_error(L, "pico.list_remove: ScrollList/DropdownMenuのみ対応");
    }
}

int LuaEngineExt::l_list_get(lua_State* L) {
    Widget* w = WidgetArg(L, 1, "pico.list_get");
    const int index = (int)luaL_checkinteger(L, 2);
    ScrollListTools::Item* item = nullptr;
    switch (w->getWidgetType()) {
        case WidgetType::ScrollList: item = static_cast<ScrollList*>(w)->itemAt(index); break;
        case WidgetType::DropdownMenu: item = static_cast<DropdownMenu*>(w)->itemAt(index); break;
        default: return luaL_error(L, "pico.list_get: ScrollList/DropdownMenuのみ対応");
    }
    if (!item) { lua_pushnil(L); return 1; }
    lua_pushstring(L, item->text.c_str());
    if (item->color >= 0) lua_pushinteger(L, item->color); else lua_pushnil(L);
    lua_pushinteger(L, (lua_Integer)item->icon);
    return 3;
}

int LuaEngineExt::l_list_select(lua_State* L) {
    Widget* w = WidgetArg(L, 1, "pico.list_select");
    const int index = (int)luaL_checkinteger(L, 2);
    switch (w->getWidgetType()) {
        case WidgetType::ScrollList: {
            ScrollList* sl = static_cast<ScrollList*>(w);
            if (index < 0) { sl->clearSelectedIndex(); sl->needsRender(); return 0; }
            if (!sl->itemAt(index)) return luaL_error(L, "pico.list_select: 範囲外のインデックス");
            sl->setSelectedIndex(index);
            return 0;
        }
        case WidgetType::DropdownMenu: {
            DropdownMenu* dm = static_cast<DropdownMenu*>(w);
            if (index < 0) { dm->clearSelectedIndex(); return 0; }
            if (!dm->itemAt(index)) return luaL_error(L, "pico.list_select: 範囲外のインデックス");
            dm->setSelectedIndex(index);
            return 0;
        }
        default:
            return luaL_error(L, "pico.list_select: ScrollList/DropdownMenuのみ対応");
    }
}

int LuaEngineExt::l_list_scroll_to(lua_State* L) {
    Widget* w = WidgetArg(L, 1, "pico.list_scroll_to");
    const int index = (int)luaL_checkinteger(L, 2);
    if (w->getWidgetType() != WidgetType::ScrollList) return luaL_error(L, "pico.list_scroll_to: ScrollListのみ対応");
    static_cast<ScrollList*>(w)->scrollToIndex(index);
    return 0;
}

// ===================================================================
// タブ
// ===================================================================

int LuaEngineExt::l_tab_set_label(lua_State* L) {
    Widget* w = WidgetArg(L, 1, "pico.tab_set_label");
    const int index = (int)luaL_checkinteger(L, 2);
    const char* label = luaL_checkstring(L, 3);
    if (w->getWidgetType() != WidgetType::TabBar) return luaL_error(L, "pico.tab_set_label: TabBarのみ対応");
    lua_pushboolean(L, static_cast<TabBar*>(w)->setLabel(index, label));
    return 1;
}

int LuaEngineExt::l_tab_label(lua_State* L) {
    Widget* w = WidgetArg(L, 1, "pico.tab_label");
    const int index = (int)luaL_checkinteger(L, 2);
    if (w->getWidgetType() != WidgetType::TabBar) return luaL_error(L, "pico.tab_label: TabBarのみ対応");
    const char* s = static_cast<TabBar*>(w)->getLabel(index);
    if (s) lua_pushstring(L, s); else lua_pushnil(L);
    return 1;
}

int LuaEngineExt::l_tab_remove(lua_State* L) {
    LuaEngine* self = Self(L);
    Widget* w = WidgetArg(L, 1, "pico.tab_remove");
    const int index = (int)luaL_checkinteger(L, 2);
    if (w->getWidgetType() != WidgetType::TabBar) return luaL_error(L, "pico.tab_remove: TabBarのみ対応");
    const WidgetId id = w->getId();
    const bool ok = static_cast<TabBar*>(w)->removeTab(index);
    if (ok) {
        // 連動の番号を詰める(消したタブの連動は外す)
        auto& links = self->tab_links_;
        links.erase(std::remove_if(links.begin(), links.end(),
            [id, index](const LuaEngine::TabLink& l) { return l.tab == id && l.index == index; }), links.end());
        for (auto& l : links) if (l.tab == id && l.index > index) l.index--;
        self->ApplyTabLinks(id);
    }
    lua_pushboolean(L, ok);
    return 1;
}

int LuaEngineExt::l_tab_clear(lua_State* L) {
    LuaEngine* self = Self(L);
    Widget* w = WidgetArg(L, 1, "pico.tab_clear");
    if (w->getWidgetType() != WidgetType::TabBar) return luaL_error(L, "pico.tab_clear: TabBarのみ対応");
    const WidgetId id = w->getId();
    static_cast<TabBar*>(w)->clearTabs();
    auto& links = self->tab_links_;
    links.erase(std::remove_if(links.begin(), links.end(),
        [id](const LuaEngine::TabLink& l) { return l.tab == id; }), links.end());
    return 0;
}

int LuaEngineExt::l_tab_link(lua_State* L) {
    LuaEngine* self = Self(L);
    Widget* tw = WidgetArg(L, 1, "pico.tab_link");
    const int index = (int)luaL_checkinteger(L, 2);
    Widget* target = WidgetArg(L, 3, "pico.tab_link");
    if (tw->getWidgetType() != WidgetType::TabBar) return luaL_error(L, "pico.tab_link: 1番目はTabBarです");
    const WidgetId tab = tw->getId();
    const WidgetId tgt = target->getId();
    bool exists = false;
    for (auto& l : self->tab_links_) {
        if (l.tab == tab && l.index == index && l.target == tgt) { exists = true; break; }
    }
    if (!exists) {
        if (self->tab_links_.size() >= LuaEngine::kMaxTabLinks) {
            return luaL_error(L, "pico.tab_link: 連動は%zu個までです", LuaEngine::kMaxTabLinks);
        }
        self->tab_links_.push_back({tab, index, tgt});
    }
    // タブが切り替わったら連動を適用する(pico.on(id,"tab_changed")の有無に関わらず)
    static_cast<TabBar*>(tw)->setOnChanged([self, tab](int) { self->OnTabChanged(tab); });
    self->ApplyTabLinks(tab);
    return 0;
}

int LuaEngineExt::l_tab_unlink(lua_State* L) {
    LuaEngine* self = Self(L);
    Widget* tw = WidgetArg(L, 1, "pico.tab_unlink");
    const WidgetId tab = tw->getId();
    const bool has_index = !lua_isnoneornil(L, 2);
    const int index = has_index ? (int)luaL_checkinteger(L, 2) : 0;
    auto& links = self->tab_links_;
    links.erase(std::remove_if(links.begin(), links.end(),
        [&](const LuaEngine::TabLink& l) { return l.tab == tab && (!has_index || l.index == index); }), links.end());
    return 0;
}

// ===================================================================
// トースト
// ===================================================================

int LuaEngineExt::l_toast(lua_State* L) {
    const char* text = luaL_checkstring(L, 1);
    // 連打でトーストの列と履歴が埋まらないよう、短い間隔の呼び出しは断る
    static unsigned long last_ms = 0;
    const unsigned long now = millis();
    if (last_ms != 0 && now - last_ms < 300) { lua_pushboolean(L, false); return 1; }
    last_ms = now ? now : 1;

    NotificationFunctions::Content c;
    NotificationFunctions::Sanitize(c.title, text);
    c.sound = false;
    lua_pushboolean(L, NotificationFunctions::Post(c) != 0);
    return 1;
}

// pico.keep_awake(): このフレームはスリープ(省電力)に入らない。スリープ中なら起きる。
// 「操作が無くても動き続ける画面」(ゲームの進行中など)が毎フレーム呼ぶ。呼ぶのをやめれば、その後は普通にスリープできる
int LuaEngineExt::l_keep_awake(lua_State*) {
    PowerFunctions::KeepAwake();
    return 0;
}

// ===================================================================
// 描画の補助
// ===================================================================

int LuaEngineExt::l_get_pixel(lua_State* L) {
    const int x = (int)luaL_checkinteger(L, 1);
    const int y = (int)luaL_checkinteger(L, 2);
    if (x < 0 || y < 0 || x >= OSData::frame->width() || y >= OSData::frame->height()) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, (lua_Integer)OSData::frame->readPixelValue(x, y));
    return 1;
}

// Luaが読み込んだ画像(images_)のパレットを今のパレットへ合わせる(インデックスは変わらず色だけ変わる)
void LuaEngineExt::SyncImagePalettes(LuaEngine* self) {
    for (auto& slot : self->images_) {
        if (!slot.used || !slot.sprite.usable) continue;
        for (int i = 0; i < 16; i++) slot.sprite.sprite.setPaletteColor(i, PICO_GFX::COLORS[i]);
    }
}

// pico.set_palette(index, r, g, b): 黒(0)と白(15)以外のパレット色を変える(r,g,bは0〜255)。
// アプリを閉じる/別の画面へ移るときは既定のパレットへ戻る(~LuaEngine)
int LuaEngineExt::l_set_palette(lua_State* L) {
    LuaEngine* self = Self(L);
    const lua_Integer index = luaL_checkinteger(L, 1);
    if (index == 0 || index == 15) return luaL_error(L, "pico.set_palette: 黒(0)と白(15)は変更できません");
    if (index < 1 || index > 14) return luaL_error(L, "pico.set_palette: indexは1〜14です(%d)", (int)index);
    int c[3];
    for (int i = 0; i < 3; i++) {
        const lua_Integer v = luaL_checkinteger(L, 2 + i);
        if (v < 0 || v > 255) return luaL_error(L, "pico.set_palette: r,g,bは0〜255です(%d)", (int)v);
        c[i] = (int)v;
    }
    PICO_GFX::SetPaletteColor((int)index, PICO_GFX::Rgb565(c[0], c[1], c[2]));
    self->used_palette_ = true;
    SyncImagePalettes(self);
    return 0;
}

// pico.get_palette(index) -> r, g, b (index 0〜15)
int LuaEngineExt::l_get_palette(lua_State* L) {
    const lua_Integer index = luaL_checkinteger(L, 1);
    if (index < 0 || index > 15) return luaL_error(L, "pico.get_palette: indexは0〜15です(%d)", (int)index);
    // COLORSはRGB565。下位ビットを上位の複製で埋めて8bitへ戻す(0xF800 -> 255,0,0)
    const int c = PICO_GFX::COLORS[index];
    const int r5 = (c >> 11) & 0x1F, g6 = (c >> 5) & 0x3F, b5 = c & 0x1F;
    lua_pushinteger(L, (r5 << 3) | (r5 >> 2));
    lua_pushinteger(L, (g6 << 2) | (g6 >> 4));
    lua_pushinteger(L, (b5 << 3) | (b5 >> 2));
    return 3;
}

// pico.reset_palette(): 既定のパレットへ戻す
int LuaEngineExt::l_reset_palette(lua_State* L) {
    LuaEngine* self = Self(L);
    PICO_GFX::ResetPalette();
    SyncImagePalettes(self);
    return 0;
}

int LuaEngineExt::l_canvas_get_pixel(lua_State* L) {
    Widget* w = WidgetArg(L, 1, "pico.canvas_get_pixel");
    if (w->getWidgetType() != WidgetType::CanvasRaster) return luaL_error(L, "pico.canvas_get_pixel: CanvasRasterのみ対応");
    CanvasRaster* cr = static_cast<CanvasRaster*>(w);
    const int x = (int)luaL_checkinteger(L, 2);
    const int y = (int)luaL_checkinteger(L, 3);
    if (x < 0 || y < 0 || x >= cr->getW() || y >= cr->getH()) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, (lua_Integer)cr->getSprite()->readPixelValue(x, y));
    return 1;
}

namespace {
    // 幅max_wに収まるよう1行ぶんの長さ(バイト)を決める。'\n'で必ず区切り、ASCIIの空白があればそこで折る
    size_t WrapLineLength(FontFn::FontSize size, const char* s, size_t remaining, int max_w, size_t* consumed) {
        // 先頭からmax_wに収まる最大の文字数を探す
        size_t fit = 0;           // 収まった長さ(バイト)
        size_t last_space = 0;    // 直近の空白の位置(その手前で折れる)
        size_t i = 0;
        char buf[160];
        while (i < remaining) {
            if (s[i] == '\n') { *consumed = i + 1; return i; }
            // 次の1文字(UTF-8)の長さ
            size_t cl = 1;
            const unsigned char c = (unsigned char)s[i];
            if (c >= 0xF0) cl = 4; else if (c >= 0xE0) cl = 3; else if (c >= 0xC0) cl = 2;
            if (i + cl > remaining) cl = remaining - i;
            const size_t next = i + cl;
            if (next >= sizeof(buf)) break;
            memcpy(buf, s, next);
            buf[next] = '\0';
            if (Label<PICO_STR_M>::GetTextWidth(size, buf) > max_w && fit > 0) {
                // 溢れた。空白があればそこまでで折る
                if (last_space > 0) { *consumed = last_space + 1; return last_space; }
                *consumed = fit;
                return fit;
            }
            fit = next;
            if (c == ' ') last_space = i;
            i = next;
        }
        *consumed = fit;
        return fit;
    }
}

// pico.draw_text_wrapped(x, y, w, text [, color [, font_size [, align [, line_gap]]]]) -> 行数, 高さ
int LuaEngineExt::l_draw_text_wrapped(lua_State* L) {
    const int x = (int)luaL_checkinteger(L, 1);
    const int y = (int)luaL_checkinteger(L, 2);
    const int w = (int)luaL_checkinteger(L, 3);
    size_t len = 0;
    const char* text = luaL_checklstring(L, 4, &len);
    const int8_t color = (int8_t)luaL_optinteger(L, 5, PICO_FORECOLOR);
    const FontFn::FontSize size = (FontFn::FontSize)luaL_optinteger(L, 6, (lua_Integer)FontFn::Normal);
    const char* align = luaL_optstring(L, 7, "left");
    const int gap = (int)luaL_optinteger(L, 8, 0);
    if (w <= 0) return luaL_error(L, "pico.draw_text_wrapped: 幅は1以上です");
    if (len > 2048) len = 2048;

    const int line_h = Label<PICO_STR_M>::GetLineHeight(size) + gap;
    int cy = y;
    int lines = 0;
    size_t pos = 0;
    while (pos < len && lines < 64) {
        size_t consumed = 0;
        const size_t n = WrapLineLength(size, text + pos, len - pos, w, &consumed);
        if (consumed == 0) break; // 1文字も進めない(幅が極端に狭い等)
        if (n > 0) {
            char line[160];
            const size_t m = std::min(n, sizeof(line) - 1);
            memcpy(line, text + pos, m);
            line[m] = '\0';
            int dx = x;
            if (strcmp(align, "center") == 0) dx = x + (w - Label<PICO_STR_M>::GetTextWidth(size, line)) / 2;
            else if (strcmp(align, "right") == 0) dx = x + w - Label<PICO_STR_M>::GetTextWidth(size, line);
            Label<PICO_STR_M>::DrawPlain(size, color, dx, cy, std::max(1, x + w - dx), line);
            if (!LuaOffscreen::active) PICO_GFX::MarkDirty({(int16_t)dx, (int16_t)cy, (int16_t)w, (int16_t)line_h});
        }
        cy += line_h;
        lines++;
        pos += consumed;
    }
    lua_pushinteger(L, lines);
    lua_pushinteger(L, lines * line_h);
    return 2;
}

// pico.measure_text(text, width [, font_size [, line_gap]]) -> 行数, 高さ(折り返したときの大きさ)
int LuaEngineExt::l_measure_text(lua_State* L) {
    size_t len = 0;
    const char* text = luaL_checklstring(L, 1, &len);
    const int w = (int)luaL_checkinteger(L, 2);
    const FontFn::FontSize size = (FontFn::FontSize)luaL_optinteger(L, 3, (lua_Integer)FontFn::Normal);
    const int gap = (int)luaL_optinteger(L, 4, 0);
    if (w <= 0) return luaL_error(L, "pico.measure_text: 幅は1以上です");
    if (len > 2048) len = 2048;
    int lines = 0;
    size_t pos = 0;
    while (pos < len && lines < 64) {
        size_t consumed = 0;
        WrapLineLength(size, text + pos, len - pos, w, &consumed);
        if (consumed == 0) break;
        lines++;
        pos += consumed;
    }
    lua_pushinteger(L, lines);
    lua_pushinteger(L, lines * (Label<PICO_STR_M>::GetLineHeight(size) + gap));
    return 2;
}

// ---- オフスクリーン画像 ----
// pico.image_create(w, h [, transparent]) -> ハンドル | nil
//   中身は白(transparent=trueなら0番色=透過)。pico.draw_imageなどで画面へ描けるし、
//   pico.image_target(handle)の間はpico.draw_*の描き先がこの画像になる
// 空いているスロットにw x hの4bppの画像を作り、添字を返す(作れなければ警告を出して-1)
int LuaEngineExt::AllocImageSlot(LuaEngine* self, int w, int h, bool transparent, const char* fn) {
    size_t index = LuaEngine::kMaxLuaImages;
    for (size_t i = 0; i < LuaEngine::kMaxLuaImages; ++i) {
        if (!self->images_[i].used) { index = i; break; }
    }
    if (index == LuaEngine::kMaxLuaImages) {
        LOG_APP_WARN("%s: 同時に保持できる画像数の上限(%zu枚)に達しています", fn, LuaEngine::kMaxLuaImages);
        return -1;
    }
    const size_t need = ((size_t)w * h + 1) / 2;
    if (self->image_bytes_used_ + need > LuaEngine::kMaxLuaImageBytes) {
        LOG_APP_WARN("%s: 画像用メモリの上限(%uB)を超えます(%uB必要)", fn,
                     (unsigned)LuaEngine::kMaxLuaImageBytes, (unsigned)need);
        return -1;
    }

    LuaEngine::ImageSlot& slot = self->images_[index];
    slot.sprite.sprite.setColorDepth(4);
    if (!slot.sprite.sprite.createSprite(w, h)) return -1;
    for (int i = 0; i < 16; i++) slot.sprite.sprite.setPaletteColor(i, PICO_GFX::COLORS[i]);
    slot.sprite.sprite.fillScreen(transparent ? 0 : PICO_BACKGROUND);
    slot.sprite.width = (uint16_t)w;
    slot.sprite.height = (uint16_t)h;
    slot.sprite.transparent = transparent;
    slot.sprite.usable = true;
    slot.used = true;
    slot.bytes = need;
    slot.rot_frames = slot.rot_cols = slot.rot_cell = 0;
    self->image_bytes_used_ += need;
    if (slot.generation == 0) slot.generation = 1;
    return (int)index;
}

int LuaEngineExt::l_image_create(lua_State* L) {
    LuaEngine* self = Self(L);
    const int w = (int)luaL_checkinteger(L, 1);
    const int h = (int)luaL_checkinteger(L, 2);
    const bool transparent = lua_toboolean(L, 3);
    if (w <= 0 || h <= 0 || w > SCREEN_WIDTH * 2 || h > SCREEN_HEIGHT * 2) {
        return luaL_error(L, "pico.image_create: 大きさが不正です(1〜%dx%d)", SCREEN_WIDTH * 2, SCREEN_HEIGHT * 2);
    }
    const int index = AllocImageSlot(self, w, h, transparent, "pico.image_create");
    if (index < 0) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, (lua_Integer)LuaEngine::MakeImageHandle((size_t)index, self->images_[index].generation));
    return 1;
}

namespace {
    constexpr double kTwoPi = 6.283185307179586;
}

// ---- 回転済みのコマ(pico.image_rotate / pico.draw_rotated) ----
// pico.draw_image_exは描くたびに1画素ずつ逆変換するので、回転のない描画より何倍も重い。
// そこで「frames通りの角度に回した絵」を先に1枚の画像(コマを格子に並べたもの)へ作っておき、
// 描くときは一番近い角度のコマを切り出してpushSprite()するだけにする(pico.draw_image_partと同じ重さ)。
// 角度はframes段階に丸まる(既定16=22.5度刻み)。品質より速さを取る版。
//
// pico.image_rotate(handle, frames [, opts]) -> sheet, cell | nil
//   opts = { sx=, sy= (拡大率。既定1、負で反転), ox=, oy= (回転の中心。既定は画像の中央), start= (最初のコマの角度、ラジアン) }
//   sheetは新しい画像のハンドル(画像のスロットとメモリを1つ使う。pico.image_freeで解放)。
//   cellはコマの一辺(px)。回転の中心はコマの真ん中に来る。
//   出来た画像は0番の色が透過になる(元が透過でない画像の0番=黒も抜ける)。
int LuaEngineExt::l_image_rotate(lua_State* L) {
    LuaEngine* self = Self(L);
    const uint32_t handle = (uint32_t)luaL_checkinteger(L, 1);
    const int frames = (int)luaL_optinteger(L, 2, 16);
    size_t src_index;
    if (!self->ResolveImageHandle(handle, src_index)) {
        return luaL_error(L, "pico.image_rotate: 無効なイメージハンドル");
    }
    if (frames < 1 || frames > 64) return luaL_error(L, "pico.image_rotate: コマの数は1〜64です");

    const int iw = self->images_[src_index].sprite.width, ih = self->images_[src_index].sprite.height;
    double sx = 1.0, sy = 1.0, ox = iw / 2.0, oy = ih / 2.0, start = 0.0;
    if (lua_istable(L, 3)) {
        auto opt = [&](const char* key, double& v) {
            lua_getfield(L, 3, key);
            if (!lua_isnil(L, -1)) v = luaL_checknumber(L, -1);
            lua_pop(L, 1);
        };
        opt("sx", sx);
        sy = sx;
        opt("sy", sy);
        opt("ox", ox);
        opt("oy", oy);
        opt("start", start);
    } else if (!lua_isnoneornil(L, 3)) {
        return luaL_error(L, "pico.image_rotate: 3番目の引数はテーブルです");
    }
    if (std::fabs(sx) < 1e-3 || std::fabs(sy) < 1e-3 || std::fabs(sx) > 8 || std::fabs(sy) > 8) {
        return luaL_error(L, "pico.image_rotate: 倍率は0より大きく8倍までです");
    }

    // コマの一辺: 回転の中心から一番遠い角までの距離(拡大後)の2倍。中心がコマの真ん中に来る
    double r = 0;
    const double cx4[4] = {0, (double)iw, 0, (double)iw};
    const double cy4[4] = {0, 0, (double)ih, (double)ih};
    for (int i = 0; i < 4; i++) {
        r = std::max(r, std::hypot((cx4[i] - ox) * sx, (cy4[i] - oy) * sy));
    }
    const int cell = 2 * (int)std::ceil(r) + 2;
    const int max_w = SCREEN_WIDTH * 2, max_h = SCREEN_HEIGHT * 2;
    if (cell > max_w || cell > max_h) {
        LOG_APP_WARN("pico.image_rotate: 回した画像が大きすぎます(一辺%dpx)", cell);
        lua_pushnil(L);
        return 1;
    }
    const int cols = std::min(frames, max_w / cell);
    const int rows = (frames + cols - 1) / cols;
    if (rows * cell > max_h) {
        LOG_APP_WARN("pico.image_rotate: コマが多すぎて1枚に並びません(%dコマ x 一辺%dpx)", frames, cell);
        lua_pushnil(L);
        return 1;
    }

    const int index = AllocImageSlot(self, cols * cell, rows * cell, true, "pico.image_rotate");
    if (index < 0) { lua_pushnil(L); return 1; }
    LuaEngine::ImageSlot& dst = self->images_[index];
    LuaEngine::ImageSlot& src = self->images_[src_index]; // AllocImageSlotの後に取る(同じ配列の別の要素)
    const bool src_transparent = src.sprite.transparent;

    // 1回きりの処理なので、描くときの速さより分かりやすさを取る(1画素ずつ逆変換して最近傍)
    const double half = cell / 2.0;
    for (int k = 0; k < frames; k++) {
        const double a = start + k * (kTwoPi / frames);
        const double c = std::cos(a), s = std::sin(a);
        const int bx = (k % cols) * cell, by = (k / cols) * cell;
        for (int py = 0; py < cell; py++) {
            const double ry = py + 0.5 - half;
            for (int px = 0; px < cell; px++) {
                const double rx = px + 0.5 - half;
                const double u = (c * rx + s * ry) / sx + ox;
                const double v = (-s * rx + c * ry) / sy + oy;
                if (u < 0 || v < 0 || u >= iw || v >= ih) continue;
                const uint32_t col = src.sprite.sprite.readPixelValue((int)u, (int)v);
                if (src_transparent && col == 0) continue;
                dst.sprite.sprite.writePixel(bx + px, by + py, (int)col);
            }
        }
    }
    dst.rot_frames = (uint16_t)frames;
    dst.rot_cols = (uint16_t)cols;
    dst.rot_cell = (uint16_t)cell;

    lua_pushinteger(L, (lua_Integer)LuaEngine::MakeImageHandle((size_t)index, dst.generation));
    lua_pushinteger(L, cell);
    return 2;
}

// pico.draw_rotated(sheet, x, y, r)
//   pico.image_rotateで作った画像から、角度r(ラジアン、時計回り)に一番近いコマを選び、
//   回転の中心が(x, y)に来るように描く。中身はpico.draw_image_partと同じ(1回のpushSprite)
int LuaEngineExt::l_draw_rotated(lua_State* L) {
    LuaEngine* self = Self(L);
    const uint32_t handle = (uint32_t)luaL_checkinteger(L, 1);
    const double x = luaL_checknumber(L, 2);
    const double y = luaL_checknumber(L, 3);
    const double r = luaL_optnumber(L, 4, 0.0);
    size_t index;
    if (!self->ResolveImageHandle(handle, index)) {
        return luaL_error(L, "pico.draw_rotated: 無効なイメージハンドル");
    }
    LuaEngine::ImageSlot& slot = self->images_[index];
    if (slot.rot_frames == 0) {
        return luaL_error(L, "pico.draw_rotated: pico.image_rotateで作った画像を渡してください");
    }
    const int frames = slot.rot_frames, cell = slot.rot_cell;
    int k = (int)std::lround(r * frames / kTwoPi) % frames;
    if (k < 0) k += frames;
    const int sx = (k % slot.rot_cols) * cell, sy = (k / slot.rot_cols) * cell;
    const int32_t dx = (int32_t)std::lround(x) - cell / 2;
    const int32_t dy = (int32_t)std::lround(y) - cell / 2;

    int32_t kx = 0, ky = 0, kw = 0, kh = 0;
    OSData::frame->getClipRect(&kx, &ky, &kw, &kh);
    const Rect clip = Rect{ (int16_t)kx, (int16_t)ky, (int16_t)kw, (int16_t)kh }
        .intersection({ (int16_t)dx, (int16_t)dy, (int16_t)cell, (int16_t)cell });
    if (clip.w > 0 && clip.h > 0) {
        // 透過つきのpushSprite()は重いので、4bppのバッファどうしで直接写す(IconRender::Blit4bpp)
        if (!IconRender::Blit4bpp(slot.sprite.sprite, sx, sy, cell, cell, dx, dy, true)) {
            OSData::frame->setClipRect(clip.x, clip.y, clip.w, clip.h);
            IconRender::DrawPimgSprite(slot.sprite, dx - sx, dy - sy);
            OSData::frame->setClipRect(kx, ky, kw, kh);
        }
    }
    if (!LuaOffscreen::active) PICO_GFX::MarkDirty({ (int16_t)dx, (int16_t)dy, (int16_t)cell, (int16_t)cell });
    return 0;
}

// pico.draw_tilemap(handle, tile_w, tile_h, data, cols, x, y)
// タイルマップを描く(pico.gameの土台。Luaでタイルを1枚ずつdraw_image_partすると、
// 全面で数百回の呼び出しになって重いため、ここでまとめて回す)。
//   handle: タイルを並べた画像(左上から右へ、行が終わったら次の行へ番号が進む)
//   data:   1バイト=1マスの文字列(行優先。cols列で折り返す)。0は空(何も描かない)、
//           1〜255は画像の(値-1)番目のタイル
//   x, y:   マップの左上を置く画面座標(カメラでずらした結果。画面の外でもよい)
// 描くのは今のクリップ(renderの中ではdirty矩形)にかかるマスだけ。
int LuaEngineExt::l_draw_tilemap(lua_State* L) {
    LuaEngine* self = Self(L);
    const uint32_t handle = (uint32_t)luaL_checkinteger(L, 1);
    const int32_t tw = (int32_t)luaL_checkinteger(L, 2);
    const int32_t th = (int32_t)luaL_checkinteger(L, 3);
    size_t len = 0;
    const char* data = luaL_checklstring(L, 4, &len);
    const int32_t cols = (int32_t)luaL_checkinteger(L, 5);
    const int32_t x = (int32_t)luaL_checkinteger(L, 6);
    const int32_t y = (int32_t)luaL_checkinteger(L, 7);

    size_t index;
    if (!self->ResolveImageHandle(handle, index)) {
        return luaL_error(L, "pico.draw_tilemap: 無効なイメージハンドル");
    }
    if (tw < 1 || th < 1 || tw > 128 || th > 128) return luaL_error(L, "pico.draw_tilemap: タイルの大きさは1〜128です");
    if (cols < 1 || cols > 4096) return luaL_error(L, "pico.draw_tilemap: 列数は1〜4096です");
    if (len == 0) return 0;

    LuaEngine::ImageSlot& slot = self->images_[index];
    const int32_t tcols = slot.sprite.width / tw;
    const int32_t trows = slot.sprite.height / th;
    if (tcols < 1 || trows < 1) return luaL_error(L, "pico.draw_tilemap: 画像がタイル1枚より小さいです");
    const int32_t tcount = tcols * trows;
    const int32_t rows = (int32_t)((len + (size_t)cols - 1) / (size_t)cols);

    // マップの範囲 ∩ 画面 ∩ 今のクリップ
    int32_t kx = 0, ky = 0, kw = 0, kh = 0;
    OSData::frame->getClipRect(&kx, &ky, &kw, &kh);
    const int32_t ax0 = std::max({x, kx, (int32_t)0});
    const int32_t ay0 = std::max({y, ky, (int32_t)0});
    const int32_t ax1 = std::min({x + cols * tw, kx + kw, (int32_t)SCREEN_WIDTH});
    const int32_t ay1 = std::min({y + rows * th, ky + kh, (int32_t)SCREEN_HEIGHT});
    if (ax1 <= ax0 || ay1 <= ay0) return 0;

    const int32_t c0 = (ax0 - x) / tw, c1 = (ax1 - 1 - x) / tw;
    const int32_t r0 = (ay0 - y) / th, r1 = (ay1 - 1 - y) / th;
    for (int32_t r = r0; r <= r1; r++) {
        for (int32_t c = c0; c <= c1; c++) {
            const size_t i = (size_t)r * (size_t)cols + (size_t)c;
            if (i >= len) break;
            const int32_t v = (uint8_t)data[i];
            if (v == 0 || v > tcount) continue;
            const int32_t ti = v - 1;
            const int32_t dx = x + c * tw, dy = y + r * th;
            const int32_t cx0 = std::max(dx, ax0), cy0 = std::max(dy, ay0);
            const int32_t cx1 = std::min(dx + tw, ax1), cy1 = std::min(dy + th, ay1);
            if (cx1 <= cx0 || cy1 <= cy0) continue;
            // 速い道: タイル1枚ぶんを4bppのバッファどうしで直接写す(クリップは触らない)
            if (IconRender::Blit4bpp(slot.sprite.sprite, (ti % tcols) * tw, (ti / tcols) * th, tw, th,
                                     dx, dy, slot.sprite.transparent)) continue;
            OSData::frame->setClipRect(cx0, cy0, cx1 - cx0, cy1 - cy0);
            IconRender::DrawPimgSprite(slot.sprite, dx - (ti % tcols) * tw, dy - (ti / tcols) * th);
        }
    }
    OSData::frame->setClipRect(kx, ky, kw, kh);
    if (!LuaOffscreen::active) {
        PICO_GFX::MarkDirty({ (int16_t)ax0, (int16_t)ay0, (int16_t)(ax1 - ax0), (int16_t)(ay1 - ay0) });
    }
    return 0;
}

int LuaEngineExt::l_image_clear(lua_State* L) {
    LuaEngine* self = Self(L);
    const uint32_t handle = (uint32_t)luaL_checkinteger(L, 1);
    size_t index;
    if (!self->ResolveImageHandle(handle, index)) return luaL_error(L, "pico.image_clear: 無効なイメージハンドル");
    LuaEngine::ImageSlot& slot = self->images_[index];
    const int color = (int)luaL_optinteger(L, 2, slot.sprite.transparent ? 0 : PICO_BACKGROUND);
    slot.sprite.sprite.fillScreen(color);
    return 0;
}

// pico.image_target(handle) で以降のpico.draw_*をその画像へ描く。pico.image_target(nil)で画面へ戻す。
// 呼び出し(コールバック)を抜けたら自動で画面へ戻る
int LuaEngineExt::l_image_target(lua_State* L) {
    LuaEngine* self = Self(L);
    if (lua_isnoneornil(L, 1)) {
        self->EndImageTarget();
        return 0;
    }
    const uint32_t handle = (uint32_t)luaL_checkinteger(L, 1);
    size_t index;
    if (!self->ResolveImageHandle(handle, index)) return luaL_error(L, "pico.image_target: 無効なイメージハンドル");
    self->EndImageTarget();
    self->saved_frame_ = OSData::frame;
    self->image_target_index_ = (int)index;
    OSData::frame = &self->images_[index].sprite.sprite;
    LuaOffscreen::active = true;
    return 0;
}

void LuaEngine::EndImageTarget() {
    if (image_target_index_ < 0) return;
    if (saved_frame_) OSData::frame = saved_frame_;
    saved_frame_ = nullptr;
    image_target_index_ = -1;
    LuaOffscreen::active = false;
}

// ===================================================================
// ユーティリティ
// ===================================================================

int LuaEngineExt::l_app_dir(lua_State* L) {
    lua_pushstring(L, Self(L)->app_dir_.c_str());
    return 1;
}

// pico.path_join(a, b, ...): "/"始まりの要素が来たらそこからやり直す。".."と"."は畳む
int LuaEngineExt::l_path_join(lua_State* L) {
    const int n = lua_gettop(L);
    FixedString<PICO_PATH_LEN> acc;
    for (int i = 1; i <= n; i++) {
        const char* part = luaL_checkstring(L, i);
        if (!part[0]) continue;
        if (part[0] == '/') acc.assign(part);
        else {
            if (!acc.empty() && acc.c_str()[acc.length() - 1] != '/') acc.append("/");
            acc.append(part);
        }
    }
    FixedString<PICO_PATH_LEN> norm;
    // 相対パスのまま渡された場合は、先頭に"/"を補ってから畳む(コアのスタックは小さいので、大きなバッファは2本までにする)
    if (acc.c_str()[0] != '/') {
        norm.assign("/");
        norm.append(acc.c_str());
        acc = norm;
    }
    if (!PICO_IO::normalize(norm, acc.c_str())) return luaL_error(L, "pico.path_join: パスが長すぎます");
    lua_pushstring(L, norm.c_str());
    return 1;
}

int LuaEngineExt::l_time(lua_State* L) {
    const time_t t = time(nullptr);
    if (t < 1577836800) { lua_pushnil(L); return 1; } // 2020年より前 = 時計が合っていない
    lua_pushinteger(L, (lua_Integer)t);
    return 1;
}

int LuaEngineExt::l_wifi_status(lua_State* L) {
    using NetworkFunctions::NetStatus;
    const NetStatus st = NetworkFunctions::currentStatus;
    lua_createtable(L, 0, 4);
    lua_pushboolean(L, st == NetStatus::SUCCESS);
    lua_setfield(L, -2, "connected");
    const char* name = "failed";
    switch (st) {
        case NetStatus::SUCCESS: name = "connected"; break;
        case NetStatus::TRYING_CONNECT: name = "connecting"; break;
        case NetStatus::OFF: name = "off"; break;
        case NetStatus::TIMEOUT: name = "timeout"; break;
        case NetStatus::SSID_NOT_FOUND: name = "not_found"; break;
        case NetStatus::FAILED: name = "failed"; break;
    }
    lua_pushstring(L, name);
    lua_setfield(L, -2, "status");
    lua_pushboolean(L, st != NetStatus::OFF);
    lua_setfield(L, -2, "enabled");
    if (st == NetStatus::SUCCESS) {
        lua_pushstring(L, NetworkFunctions::currentSSID.c_str());
        lua_setfield(L, -2, "ssid");
    }
    return 1;
}

int LuaEngineExt::l_url_encode(lua_State* L) {
    size_t n = 0;
    const char* s = luaL_checklstring(L, 1, &n);
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    static const char* hex = "0123456789ABCDEF";
    for (size_t i = 0; i < n; i++) {
        const unsigned char c = (unsigned char)s[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
            || c == '-' || c == '_' || c == '.' || c == '~') {
            luaL_addchar(&b, (char)c);
        } else {
            luaL_addchar(&b, '%');
            luaL_addchar(&b, hex[c >> 4]);
            luaL_addchar(&b, hex[c & 15]);
        }
    }
    luaL_pushresult(&b);
    return 1;
}

int LuaEngineExt::l_url_decode(lua_State* L) {
    size_t n = 0;
    const char* s = luaL_checklstring(L, 1, &n);
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    auto hexv = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '%' && i + 2 < n && hexv(s[i + 1]) >= 0 && hexv(s[i + 2]) >= 0) {
            luaL_addchar(&b, (char)(hexv(s[i + 1]) * 16 + hexv(s[i + 2])));
            i += 2;
        } else if (s[i] == '+') {
            luaL_addchar(&b, ' ');
        } else {
            luaL_addchar(&b, s[i]);
        }
    }
    luaL_pushresult(&b);
    return 1;
}

int LuaEngineExt::l_base64_encode(lua_State* L) {
    size_t n = 0;
    const char* s = luaL_checklstring(L, 1, &n);
    const bool url_safe = lua_toboolean(L, 2);
    if (n > 12 * 1024) return luaL_error(L, "pico.base64_encode: 12KiBまでです");
    // 出力をLuaの文字列バッファへ直接書く
    luaL_Buffer b;
    char* out = luaL_buffinitsize(L, &b, Base64::EncodedSize(n, url_safe) + 4);
    const size_t m = Base64::Encode((const uint8_t*)s, n, out, url_safe);
    luaL_pushresultsize(&b, m);
    return 1;
}

int LuaEngineExt::l_base64_decode(lua_State* L) {
    size_t n = 0;
    const char* s = luaL_checklstring(L, 1, &n);
    if (n > 16 * 1024) return luaL_error(L, "pico.base64_decode: 16KiBまでです");
    luaL_Buffer b;
    char* out = luaL_buffinitsize(L, &b, n * 3 / 4 + 4);
    size_t m = 0;
    if (!Base64::Decode(s, n, (uint8_t*)out, &m)) {
        luaL_pushresultsize(&b, 0);
        lua_pop(L, 1);
        lua_pushnil(L);
        return 1;
    }
    luaL_pushresultsize(&b, m);
    return 1;
}

namespace {
    bool SettingsPath(const LuaEngine* e, const char* app_dir, FixedString<PICO_PATH_LEN>& out) {
        (void)e;
        return PICO_IO::join(out, app_dir, "settings.cfg");
    }
}

// pico.settings_get(key [, default]) / settings_set(key, value) / settings_all()
// アプリのディレクトリの settings.cfg(key=value形式)を読み書きする。値は文字列
int LuaEngineExt::l_settings_get(lua_State* L) {
    LuaEngine* self = Self(L);
    const char* key = luaL_checkstring(L, 1);
    FixedString<PICO_PATH_LEN> path;
    SettingsPath(self, self->app_dir_.c_str(), path);

    bool has = false;
    FixedString<PICO_Config::kConfigMaxValueLen> found;
    if (OSData::SD_usable && OSData::SD.exists(path.c_str())) {
        PICO_Config::ParseFile(path.c_str(), [&](const char* k, const char* v) {
            if (strcmp(k, key) == 0) { found.assign(v); has = true; }
        });
    }
    if (has) lua_pushstring(L, found.c_str());
    else if (lua_gettop(L) >= 2) lua_pushvalue(L, 2);
    else lua_pushnil(L);
    return 1;
}

int LuaEngineExt::l_settings_all(lua_State* L) {
    LuaEngine* self = Self(L);
    FixedString<PICO_PATH_LEN> path;
    SettingsPath(self, self->app_dir_.c_str(), path);
    lua_newtable(L);
    const int t = lua_gettop(L);
    if (OSData::SD_usable && OSData::SD.exists(path.c_str())) {
        PICO_Config::ParseFile(path.c_str(), [&](const char* k, const char* v) {
            lua_pushstring(L, v);
            lua_setfield(L, t, k);
        });
    }
    return 1;
}

int LuaEngineExt::l_settings_set(lua_State* L) {
    LuaEngine* self = Self(L);
    const char* key = luaL_checkstring(L, 1);
    if (!key[0] || strchr(key, '=') || strchr(key, '\n') || key[0] == '#' || strlen(key) >= PICO_Config::kConfigMaxKeyLen) {
        return luaL_error(L, "pico.settings_set: キーが不正です");
    }
    char num[48];
    const char* value = nullptr;
    switch (lua_type(L, 2)) {
        case LUA_TBOOLEAN: value = lua_toboolean(L, 2) ? "true" : "false"; break;
        case LUA_TNUMBER:
            if (lua_isinteger(L, 2)) snprintf(num, sizeof(num), "%lld", (long long)lua_tointeger(L, 2));
            else {
                snprintf(num, sizeof(num), "%.6f", (double)lua_tonumber(L, 2));
                char* end = num + strlen(num);
                while (end > num + 1 && end[-1] == '0' && end[-2] != '.') *--end = '\0';
            }
            value = num;
            break;
        case LUA_TSTRING: value = lua_tostring(L, 2); break;
        default: return luaL_error(L, "pico.settings_set: 値は文字列/数値/真偽値です");
    }
    if (strchr(value, '\n') || strchr(value, '\r') || strlen(value) >= PICO_Config::kConfigMaxValueLen) {
        lua_pushboolean(L, false);
        return 1;
    }
    if (!OSData::SD_usable) { lua_pushboolean(L, false); return 1; }
    FixedString<PICO_PATH_LEN> path;
    SettingsPath(self, self->app_dir_.c_str(), path);
    lua_pushboolean(L, PICO_Config::SetValue(path.c_str(), key, value));
    return 1;
}

int LuaEngineExt::l_memory_info(lua_State* L) {
    LuaEngine* self = Self(L);
    const MemFunctions::Snapshot s = MemFunctions::Take(false);
    lua_createtable(L, 0, 7);
    lua_pushinteger(L, (lua_Integer)s.stack_headroom);
    lua_setfield(L, -2, "heap_headroom");
    lua_pushinteger(L, (lua_Integer)self->used_);
    lua_setfield(L, -2, "lua_used");
    lua_pushinteger(L, (lua_Integer)self->budget_);
    lua_setfield(L, -2, "lua_budget");
    lua_pushinteger(L, (lua_Integer)(self->budget_ > self->used_ ? self->budget_ - self->used_ : 0));
    lua_setfield(L, -2, "lua_free");
    lua_pushinteger(L, (lua_Integer)s.free_total);
    lua_setfield(L, -2, "heap_free");
    lua_pushinteger(L, (lua_Integer)s.used);
    lua_setfield(L, -2, "heap_used");
    lua_pushinteger(L, (lua_Integer)self->image_bytes_used_);
    lua_setfield(L, -2, "image_bytes");
    return 1;
}

// ===================================================================
// 登録
// ===================================================================

void LuaEngine::RegisterExtApi() {
    // スタックのトップは pico テーブル(registerApi参照)
    registerFn("off", LuaEngineExt::l_off);
    registerFn("text_set", LuaEngineExt::l_text_set);
    registerFn("set_dots", LuaEngineExt::l_set_dots);
    registerFn("set_name", LuaEngineExt::l_set_name);
    registerFn("find", LuaEngineExt::l_find);
    registerFn("parent", LuaEngineExt::l_parent);
    registerFn("children", LuaEngineExt::l_children);
    registerFn("get_rect", LuaEngineExt::l_get_rect);
    registerFn("bring_to_front", LuaEngineExt::l_bring_to_front);
    registerFn("send_to_back", LuaEngineExt::l_send_to_back);
    registerFn("show_keyboard", LuaEngineExt::l_show_keyboard);
    registerFn("hide_keyboard", LuaEngineExt::l_hide_keyboard);
    registerFn("scroll_to", LuaEngineExt::l_scroll_to);
    registerFn("show_choice", LuaEngineExt::l_show_choice);
    registerFn("show_date", LuaEngineExt::l_show_date);
    registerFn("show_time", LuaEngineExt::l_show_time);
    registerFn("show_number", LuaEngineExt::l_show_number);
    registerFn("show_progress", LuaEngineExt::l_show_progress);
    // list_add は既存を差し替える(アイコン・色のオプション付き)
    registerFn("list_add", LuaEngineExt::l_list_add);
    registerFn("list_insert", LuaEngineExt::l_list_insert);
    registerFn("list_remove", LuaEngineExt::l_list_remove);
    registerFn("list_get", LuaEngineExt::l_list_get);
    registerFn("list_select", LuaEngineExt::l_list_select);
    registerFn("list_scroll_to", LuaEngineExt::l_list_scroll_to);
    registerFn("tab_set_label", LuaEngineExt::l_tab_set_label);
    registerFn("tab_label", LuaEngineExt::l_tab_label);
    registerFn("tab_remove", LuaEngineExt::l_tab_remove);
    registerFn("tab_clear", LuaEngineExt::l_tab_clear);
    registerFn("tab_link", LuaEngineExt::l_tab_link);
    registerFn("tab_unlink", LuaEngineExt::l_tab_unlink);
    registerFn("toast", LuaEngineExt::l_toast);
    registerFn("keep_awake", LuaEngineExt::l_keep_awake);
    registerFn("on_back", LuaEngineExt::l_on_back);
    registerFn("go_back", LuaEngineExt::l_go_back);
    registerFn("get_pixel", LuaEngineExt::l_get_pixel);
    registerFn("set_palette", LuaEngineExt::l_set_palette);
    registerFn("get_palette", LuaEngineExt::l_get_palette);
    registerFn("reset_palette", LuaEngineExt::l_reset_palette);
    registerFn("canvas_get_pixel", LuaEngineExt::l_canvas_get_pixel);
    registerFn("draw_text_wrapped", LuaEngineExt::l_draw_text_wrapped);
    registerFn("measure_text", LuaEngineExt::l_measure_text);
    registerFn("image_create", LuaEngineExt::l_image_create);
    registerFn("image_clear", LuaEngineExt::l_image_clear);
    registerFn("draw_tilemap", LuaEngineExt::l_draw_tilemap);
    registerFn("image_rotate", LuaEngineExt::l_image_rotate);
    registerFn("draw_rotated", LuaEngineExt::l_draw_rotated);
    registerFn("image_target", LuaEngineExt::l_image_target);
    registerFn("app_dir", LuaEngineExt::l_app_dir);
    registerFn("path_join", LuaEngineExt::l_path_join);
    registerFn("time", LuaEngineExt::l_time);
    registerFn("wifi_status", LuaEngineExt::l_wifi_status);
    registerFn("url_encode", LuaEngineExt::l_url_encode);
    registerFn("url_decode", LuaEngineExt::l_url_decode);
    registerFn("base64_encode", LuaEngineExt::l_base64_encode);
    registerFn("base64_decode", LuaEngineExt::l_base64_decode);
    registerFn("settings_get", LuaEngineExt::l_settings_get);
    registerFn("settings_set", LuaEngineExt::l_settings_set);
    registerFn("settings_all", LuaEngineExt::l_settings_all);
    registerFn("memory_info", LuaEngineExt::l_memory_info);
    RegisterCryptoApi();
    RegisterIsoApi();
}
