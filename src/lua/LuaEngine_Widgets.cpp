// pico.* のうちウィジェット(作る・壊す・プロパティ・イベント・コンテナ・リスト・タブ・描き直しの依頼)とダイアログ
#include "lua/LuaEngine.hpp"
#include "lua/LuaEngine_Internal.hpp"
#include "services/Power_Functions.hpp"
#include "system/Mem_Functions.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>

#include "gui/widgets/Widget.hpp"
#include "gui/widgets/WidgetRegistry.hpp"
#include "gui/widgets/WidgetFactory.hpp"
#include "gui/widgets/WidgetProperty.hpp"
#include "gui/widgets/LayoutContainer.hpp"
#include "gui/widgets/GridContainer.hpp"
#include "gui/widgets/ScrollContainer.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/Textbox.hpp"
#include "gui/widgets/LuaCanvas.hpp"
#include "gui/widgets/CanvasRaster.hpp"
#include "gui/widgets/Checkbox.hpp"
#include "gui/widgets/NumberSlider.hpp"
#include "gui/widgets/ScrollList.hpp"
#include "gui/widgets/TabBar.hpp"
#include "gui/widgets/DropdownMenu.hpp"
#include "gui/widgets/dialogs/MsgDialog.hpp"
#include "gui/widgets/dialogs/InputDialog.hpp"
#include "gui/widgets/dialogs/FileSaveDialog.hpp"
#include "gui/widgets/dialogs/FileSelectDialog.hpp"
#include "gui/widgets/dialogs/ColorDialog.hpp"
#include "gui/widgets/dialogs/PickerDialog.hpp"
#include "gui/icons/icon_render.h"
#include "gui/icons/Bmp_Codec.hpp"
#include "gui/icons/Image_Codec.hpp"
#include "gui/Fill4bpp.hpp"
#include "gui/core/Widget_Functions.hpp"
#include "gui/core/Error_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "gui/core/Scene_Functions.hpp"
#include "services/App_Functions.hpp"
#include "gui/core/GFX_Functions.hpp"
#include "gui/core/Scene.hpp"
#include "gui/scenes/LuaScene.hpp"
#include "storage/SD_IO.hpp"
#include "storage/SD_Path.hpp"
#include "system/Config_Functions.hpp"
#include "net/Http_Request.hpp"
#include "util/Url.hpp"
#include "system/Time_Functions.hpp"
#include "hal/Sound_Functions.hpp"
#include "system/Pad_Functions.hpp"
#include "hal/Battery_Functions.hpp"
#include "system/Notification_Functions.hpp"
#include "sound/Note_Name.hpp"
#include "sound/Mml_Compiler.hpp"
#include "lua/LuaDebugger.hpp"
#include "lua/LuaJson.hpp"
#include "lua/LuaBuiltinModules.hpp"
#include "crypto/Secret_Aead.hpp"
#include "devtools/Profiler_Functions.hpp"
#include "OS_Data.hpp"
#include "consts.hpp"
#include <Arduino.h>
#include "system/I18n_Functions.hpp"


using namespace LuaInternal;

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

    // 画像・文書をSDのパスから読み込むプロパティは、pico.image_loadと同じ権限(app_dirの外は
    // sd_outside_app_dirが要る)で縛る。縛らないと、Image/ImageView/MarkdownViewのpathから
    // 権限なしにSDの任意のファイルの中身を(画像や文書として)画面に出せてしまう
    if (pid == WidgetProperty::Id::Path && lua_type(L, 3) == LUA_TSTRING) {
        switch (w->getWidgetType()) {
            case WidgetType::Image:
            case WidgetType::ImageView:
            case WidgetType::MarkdownView: {
                const char* path = lua_tostring(L, 3);
                if (path[0] && !Self(L)->SdPathAllowed(path)) {
                    LOG_APP_WARN("pico.set(path): アプリディレクトリ外へのアクセスは許可されていません: %s", path);
                    return luaL_error(L, "pico.set: アプリのフォルダの外のファイルは読めません(sd_outside_app_dir権限が必要です)");
                }
                break;
            }
            default: break;
        }
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

    if (kind == EventKind::Closed) {
        switch (w->getWidgetType()) {
            case WidgetType::MsgDialog:
            case WidgetType::InputDialog:
            case WidgetType::FileSaveDialog:
            case WidgetType::FileSelectDialog:
            case WidgetType::ColorDialog:
            case WidgetType::PickerDialog:
                break;
            default:
                return luaL_error(L, "pico.on: 'closed'イベントはダイアログ(pico.show_*が返すID)のみ対応");
        }
    }

    // ウィジェット固有イベントは対応する種別以外へ登録できない("render"/"closed"と同じ考え方)
    if (kind == EventKind::CheckedChanged && w->getWidgetType() != WidgetType::Checkbox) {
        return luaL_error(L, "pico.on: 'checked_changed'イベントはCheckboxのみ対応");
    }
    if (kind == EventKind::ValueChanged && w->getWidgetType() != WidgetType::NumberSlider) {
        return luaL_error(L, "pico.on: 'value_changed'イベントはNumberSliderのみ対応");
    }
    if (kind == EventKind::SelectItem && w->getWidgetType() != WidgetType::ScrollList) {
        return luaL_error(L, "pico.on: 'select_item'イベントはScrollListのみ対応");
    }
    if (kind == EventKind::TabChanged && w->getWidgetType() != WidgetType::TabBar) {
        return luaL_error(L, "pico.on: 'tab_changed'イベントはTabBarのみ対応");
    }
    if (kind == EventKind::DropdownChanged && w->getWidgetType() != WidgetType::DropdownMenu) {
        return luaL_error(L, "pico.on: 'dropdown_changed'イベントはDropdownMenuのみ対応");
    }
    if (kind == EventKind::TextChanged && w->getWidgetType() != WidgetType::Textbox) {
        return luaL_error(L, "pico.on: 'text_changed'イベントはTextboxのみ対応");
    }

    {
        const char* why = nullptr;
        if (!self->CheckExtEventTarget(kind, w, &why)) {
            return luaL_error(L, "pico.on: '%s'イベントは%sのみ対応", ev, why ? why : "対応するウィジェット");
        }
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

// ---------------- コンテナからの取り外し / リストへの項目追加 ----------------
// クラスコメント参照。細部の穴埋め(2026-09-21追加)。

int LuaEngine::l_remove_child(lua_State* L) {
    const WidgetId container_id = (WidgetId)luaL_checkinteger(L, 1);
    const WidgetId child_id = (WidgetId)luaL_checkinteger(L, 2);

    Widget* container = WidgetRegistry::Resolve(container_id);
    Widget* child = WidgetRegistry::Resolve(child_id);
    if (!container || !child) return luaL_error(L, "pico.remove_child: 無効なID");

    switch (container->getWidgetType()) {
        case WidgetType::LayoutContainer:
        case WidgetType::GridContainer:
        case WidgetType::ScrollContainer:
            break;
        default:
            return luaL_error(L, "pico.remove_child: このウィジェット種別から子を取り外せません");
    }

    if (child->getParent() != container) {
        return luaL_error(L, "pico.remove_child: 指定したコンテナの子ではありません");
    }

    // removeChild()を境に親がnullへ変わり、getScreenRect()の基準(コンテナ座標→
    // 画面座標)が変わってしまうので、コンテナに属していた間の画面矩形は
    // 今のうちにdirty化しておく(後からでは同じ場所を指せない)
    PICO_GFX::MarkDirty(child->getScreenRect());

    // Widget::removeChild()は仮想関数なので、この時点で型ごとのoverride
    // (children_からの除去+setParent(nullptr))がそのまま呼ばれる
    container->removeChild(child);

    // pico.add_child()がフラットリスト(WidgetFunctions::widgets)から外した分を
    // ここで元に戻す。取り外した子は次フレームから独立したルートウィジェットとして
    // 描画・当たり判定の対象になる(座標はコンテナ内での相対値のまま残るので、
    // 必要なら呼び出し側がpico.set(id,"x"/"y",...)で置き直すこと)
    WidgetFunctions::Add(child);
    child->needsRender();
    return 0;
}

int LuaEngine::l_list_add(lua_State* L) {
    const WidgetId id = (WidgetId)luaL_checkinteger(L, 1);
    const char* text = luaL_checkstring(L, 2);

    Widget* w = WidgetRegistry::Resolve(id);
    if (!w) return luaL_error(L, "pico.list_add: 無効なID");

    switch (w->getWidgetType()) {
        case WidgetType::ScrollList: {
            ScrollListTools::Item item;
            item.text.assign(text);
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

int LuaEngine::l_list_clear(lua_State* L) {
    const WidgetId id = (WidgetId)luaL_checkinteger(L, 1);

    Widget* w = WidgetRegistry::Resolve(id);
    if (!w) return luaL_error(L, "pico.list_clear: 無効なID");

    switch (w->getWidgetType()) {
        case WidgetType::ScrollList:
            static_cast<ScrollList*>(w)->clear();
            return 0;
        case WidgetType::DropdownMenu:
            static_cast<DropdownMenu*>(w)->clear();
            return 0;
        default:
            return luaL_error(L, "pico.list_clear: ScrollList/DropdownMenuのみ対応");
    }
}

int LuaEngine::l_tab_add(lua_State* L) {
    const WidgetId id = (WidgetId)luaL_checkinteger(L, 1);
    const char* label = luaL_checkstring(L, 2);

    Widget* w = WidgetRegistry::Resolve(id);
    if (!w) return luaL_error(L, "pico.tab_add: 無効なID");
    if (w->getWidgetType() != WidgetType::TabBar) {
        return luaL_error(L, "pico.tab_add: TabBarのみ対応");
    }

    // TabBar::addTab()はkMaxTabs(4)に達しているとfalseを返す。呼び出し側が
    // タブ数の上限を検知できるよう、そのままLuaへ返す
    const bool ok = static_cast<TabBar*>(w)->addTab(label);
    lua_pushboolean(L, ok);
    return 1;
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

// ---------------- ダイアログ ----------------
// ヘッダのクラスコメント「ダイアログ」参照。いずれも
// new Xxx(...) → WidgetFunctions::AddDialog() → setVisible(true) → WireDialogClosed()
// という同じ手順を踏み、生成したWidgetIdを返す。

int LuaEngine::l_show_message(lua_State* L) {
    const char* text = luaL_checkstring(L, 1);
    const char* cancel_text = luaL_checkstring(L, 2);
    const char* ok_text = luaL_checkstring(L, 3);

    MsgDialog* dialog = new MsgDialog(text, cancel_text, ok_text);
    if (!dialog) return luaL_error(L, "pico.show_message: 生成に失敗しました(メモリ不足の可能性)");

    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    const WidgetId id = dialog->getId();
    Self(L)->WireDialogClosed(dialog, id);

    lua_pushinteger(L, (lua_Integer)id);
    return 1;
}

int LuaEngine::l_show_input(lua_State* L) {
    const char* label = luaL_checkstring(L, 1);
    const char* initial_text = luaL_optstring(L, 2, "");
    // 省略時はtrue(単一行)。lua_toboolean()は未指定/nilをfalseとして返すため、
    // 「複数行を明示的に指定しない限り単一行」にするには先にnoneornilを見る必要がある
    const bool is_single_line = lua_isnoneornil(L, 3) ? true : (bool)lua_toboolean(L, 3);

    // ボタンの文字。省略時は従来どおり「決定」「キャンセル」、空文字列ならそのボタンを出さない
    const char* submit_text = luaL_optstring(L, 4, TR("決定"));
    const char* cancel_text = luaL_optstring(L, 5, TR("キャンセル"));

    InputDialog* dialog = new InputDialog(label, is_single_line, submit_text, cancel_text);
    if (!dialog) return luaL_error(L, "pico.show_input: 生成に失敗しました(メモリ不足の可能性)");
    if (initial_text && *initial_text) dialog->setInput(initial_text);

    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    const WidgetId id = dialog->getId();
    Self(L)->WireDialogClosed(dialog, id);

    lua_pushinteger(L, (lua_Integer)id);
    return 1;
}

int LuaEngine::l_show_file_save(lua_State* L) {
    const char* start_dir = luaL_optstring(L, 1, "/");
    const char* default_name = luaL_optstring(L, 2, nullptr);

    FileSaveDialog* dialog = new FileSaveDialog(start_dir);
    if (!dialog) return luaL_error(L, "pico.show_file_save: 生成に失敗しました(メモリ不足の可能性)");
    if (default_name) dialog->setFileName(default_name);

    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    const WidgetId id = dialog->getId();
    Self(L)->WireDialogClosed(dialog, id);

    lua_pushinteger(L, (lua_Integer)id);
    return 1;
}

int LuaEngine::l_show_file_select(lua_State* L) {
    const char* start_dir = luaL_optstring(L, 1, "/");

    FileSelectDialog* dialog = new FileSelectDialog(start_dir);
    if (!dialog) return luaL_error(L, "pico.show_file_select: 生成に失敗しました(メモリ不足の可能性)");

    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    const WidgetId id = dialog->getId();
    Self(L)->WireDialogClosed(dialog, id);

    lua_pushinteger(L, (lua_Integer)id);
    return 1;
}

int LuaEngine::l_show_color(lua_State* L) {
    ColorDialog* dialog = new ColorDialog();
    if (!dialog) return luaL_error(L, "pico.show_color: 生成に失敗しました(メモリ不足の可能性)");

    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    const WidgetId id = dialog->getId();
    Self(L)->WireDialogClosed(dialog, id);

    lua_pushinteger(L, (lua_Integer)id);
    return 1;
}
