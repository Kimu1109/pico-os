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
#include "gui/icons/icon_render.h"
#include "functions/Widget_Functions.hpp"
#include "functions/Error_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/App_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "gui/scenes/Scene.hpp"
#include "gui/scenes/LuaScene.hpp"
#include "storage/SD_IO.hpp"
#include "task/Http_Request.hpp"
#include "util/Url.hpp"
#include "functions/Time_Functions.hpp"
#include "functions/Sound_Functions.hpp"
#include "sound/Note_Name.hpp"
#include "sound/Mml_Compiler.hpp"
#include "OS_Data.hpp"
#include "consts.hpp"

namespace {
    // WidgetFactory::Create()が実際に生成する特殊化と揃える(WidgetProperty.cppの
    // 同名エイリアスと同じ理由。食い違うと不正なstatic_castになる)
    using TextboxT = Textbox<WidgetFactory::kTextboxCapacity>;

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

    // pico.http_request()の送信ボディ/受信本文の上限。pico.sd_read等の
    // kMaxSdReadBytesと同じ考え方(Lua state全体の予算を1回のリクエストで
    // 食い潰さないための頭打ち)。HttpEngine::HttpState越しにしか使わないため
    // LuaEngineのメンバにはせずここへ置く
    constexpr size_t kMaxHttpBodyBytes = PICO_STR_16KiB;
    constexpr size_t kMaxHttpResponseBytes = PICO_STR_16KiB;

    bool HttpMethodFromName(const char* name, HttpRequest::Method& out) {
        if (!name) return false;
        if (strcmp(name, "GET") == 0)    { out = HttpRequest::Method::GET;    return true; }
        if (strcmp(name, "POST") == 0)   { out = HttpRequest::Method::POST;   return true; }
        if (strcmp(name, "PUT") == 0)    { out = HttpRequest::Method::PUT;    return true; }
        if (strcmp(name, "PATCH") == 0)  { out = HttpRequest::Method::PATCH;  return true; }
        if (strcmp(name, "DELETE") == 0) { out = HttpRequest::Method::Delete; return true; }
        return false;
    }

    // pico.http_request()の受信本文の行き先。上限を超える分は書き込みを拒否して
    // 応答全体を失敗させる(pico.sd_readと同じく黙って切り詰めない方針)
    struct LuaHttpSink : IHttpSink {
        FixedString<kMaxHttpResponseBytes> body;
        bool write(const void* data, size_t len) override {
            if (body.length() + len > kMaxHttpResponseBytes) return false;
            return body.append((const char*)data, len);
        }
    };
}

// pico.http_request()用の状態一式。ヘッダでは前方宣言のみにしてポインタで持ち、
// 使わないLuaアプリのメモリコストをゼロに保つ(クラスコメント「ネットワーク」参照)
struct LuaEngine::HttpState {
    HttpRequest request;
    LuaHttpSink sink;
    // HttpRequestは送信ボディ/Content-Typeを非所有ポインタで受け取る(IHttpSinkと
    // 同じ約束)ため、Luaスタック上の一時的な文字列をそのまま渡すのではなく、
    // リクエストが終わるまで生きているこのバッファへ一度コピーしてから渡す
    FixedString<kMaxHttpBodyBytes> body_buf;
    FixedString<PICO_STR_M> content_type_buf;
    // 進行中のリクエストが無ければLUA_NOREF。「同時に1本まで」の判定にも使う
    int callback_ref = LUA_NOREF;
};

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

// ---------------- 実行時間の安全網(暴走防止) ----------------
// クラスコメント「実行時間の安全網」参照。

void LuaEngine::InstructionHook(lua_State* L, lua_Debug*) {
    // Alloc()へlua_newstate(Alloc, this)で渡したudをlua_getallocf()経由で取り戻す。
    // フック専用の状態をLuaEngine以外に持たずに済む
    void* ud = nullptr;
    lua_getallocf(L, &ud);
    LuaEngine* self = static_cast<LuaEngine*>(ud);

    if (self->instructions_remaining_ <= (uint32_t)kHookInstructionInterval) {
        // luaL_error()は内部でlongjmpするため、この関数はここで戻らない。
        // lua_pcall()から見れば通常の実行時エラーと区別が付かないので、
        // 呼び出し元(ProtectedCall()の呼び出し元)の既存エラー処理がそのまま効く
        luaL_error(L, "スクリプトの実行が命令数の上限(%u)を超えたため打ち切りました"
                      "(無限ループの可能性があります)", (unsigned)kMaxInstructionsPerCall);
        return; // 到達しないが、"呼んだら戻らない"ことを読み手へ明示するため書いておく
    }
    self->instructions_remaining_ -= kHookInstructionInterval;
}

int LuaEngine::ProtectedCall(int nargs) {
    instructions_remaining_ = kMaxInstructionsPerCall;
    return lua_pcall(L, nargs, 0, 0);
}

LuaEngine::LuaEngine(size_t budget_bytes, const LuaPermissions& permissions, const char* app_dir)
    : budget_(budget_bytes), permissions_(permissions) {
    if (!PICO_IO::normalize(app_dir_, app_dir)) app_dir_.assign("/");

    L = lua_newstate(Alloc, this);
    if (!L) {
        LOG_APP_FAIL("LuaEngine: lua_newstateに失敗しました(予算%zuB)", budget_bytes);
        return;
    }

    // 以降の全てのLua実行(luaL_openlibs()含む)に効かせるため、pcallより前に設定する
    lua_sethook(L, InstructionHook, LUA_MASKCOUNT, kHookInstructionInterval);

    // luaL_openlibs()やregisterApi()の途中でOOMになった場合、pcallで保護せずに
    // 直接呼ぶとLuaは(保護フレームが無いため)abort()してしまう
    // (script/host_test/lua_alloc_budget_test.cppで確認済み)。
    // 必ずpcall越しに呼ぶことでLUA_ERRMEMとして安全に失敗させる。
    lua_pushcfunction(L, InitTrampoline);
    lua_pushlightuserdata(L, this);
    if (ProtectedCall(1) != LUA_OK) {
        LOG_APP_FAIL("LuaEngine: 初期化に失敗しました(予算%zuB): %s",
                     budget_bytes, lua_tostring(L, -1));
        lua_close(L);
        L = nullptr;
        return;
    }
}

LuaEngine::~LuaEngine() {
    // 鳴らしっぱなし(長さ0)の音を残したままアプリを閉じると鳴り止まないので、
    // 音を使ったアプリは閉じるときに全部止める
    if (used_sound_) SoundFunctions::StopAll();
    if (used_music_) SoundFunctions::MusicStop();
    delete http_; // lua_close()より前でも後でも問題ない(HttpStateはLuaと無関係のC++側の状態)
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

    if (ProtectedCall(0) != LUA_OK) {
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

    if (ProtectedCall(0) != LUA_OK) {
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
    if (ProtectedCall(1) != LUA_OK) {
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
    registerFn("remove_child", l_remove_child);
    registerFn("list_add", l_list_add);
    registerFn("list_clear", l_list_clear);
    registerFn("tab_add", l_tab_add);
    registerFn("log", l_log);
    registerFn("show_error", l_show_error);
    registerFn("pop", l_pop);
    registerFn("push_scene", l_push_scene);
    registerFn("change_scene", l_change_scene);
    registerFn("launch_app", l_launch_app);
    registerFn("content_rect", l_content_rect);
    registerFn("get_time", l_get_time);
    registerFn("get_touch", l_get_touch);
    registerFn("sound_available", l_sound_available);
    registerFn("beep", l_beep);
    registerFn("sound_play", l_sound_play);
    registerFn("sound_stop", l_sound_stop);
    registerFn("sound_playing", l_sound_playing);
    registerFn("note_freq", l_note_freq);
    registerFn("music_play", l_music_play);
    registerFn("music_play_text", l_music_play_text);
    registerFn("music_stop", l_music_stop);
    registerFn("music_playing", l_music_playing);
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
    registerFn("canvas_clear", l_canvas_clear);
    registerFn("canvas_save", l_canvas_save);
    registerFn("canvas_load", l_canvas_load);
    registerFn("canvas_undo", l_canvas_undo);
    registerFn("sd_exists", l_sd_exists);
    registerFn("sd_read", l_sd_read);
    registerFn("sd_write", l_sd_write);
    registerFn("sd_remove", l_sd_remove);
    registerFn("sd_mkdir", l_sd_mkdir);
    registerFn("sd_list", l_sd_list);
    registerFn("show_message", l_show_message);
    registerFn("show_input", l_show_input);
    registerFn("show_file_save", l_show_file_save);
    registerFn("show_file_select", l_show_file_select);
    registerFn("show_color", l_show_color);
    registerFn("http_request", l_http_request);
    registerFn("http_cancel", l_http_cancel);
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
        {"closed", EventKind::Closed},
        {"checked_changed", EventKind::CheckedChanged},
        {"value_changed", EventKind::ValueChanged},
        {"select_item", EventKind::SelectItem},
        {"tab_changed", EventKind::TabChanged},
        {"dropdown_changed", EventKind::DropdownChanged},
        {"text_changed", EventKind::TextChanged},
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
        case EventKind::Closed:
            // ここでは何もしない: ダイアログのsetOnClosed/setOnClose配線自体は
            // 生成時点(pico.show_xxx() → WireDialogClosed())で既に済んでいる。
            // pico.on()はcallbacks_への登録(Dispatch()が引くref)だけを担う
            break;
        // ウィジェット固有イベント(クラスコメント「ウィジェット固有イベント」参照)。
        // l_on()側で対応するWidgetTypeであることを確認済みなので安全にstatic_castできる。
        // CheckedChanged/ValueChanged/TabChangedは変わった後の値そのものを渡さず、
        // 既存の共通Dispatch(id, kind)(idのみ)に乗せる(値はpico.get()で読む)
        case EventKind::CheckedChanged:
            static_cast<Checkbox*>(w)->setOnChangeChecked(
                [this, id]() { this->Dispatch(id, EventKind::CheckedChanged); });
            break;
        case EventKind::ValueChanged:
            static_cast<NumberSlider*>(w)->setOnValueChanged(
                [this, id]() { this->Dispatch(id, EventKind::ValueChanged); });
            break;
        case EventKind::TabChanged:
            static_cast<TabBar*>(w)->setOnChanged(
                [this, id](int) { this->Dispatch(id, EventKind::TabChanged); });
            break;
        case EventKind::DropdownChanged:
            static_cast<DropdownMenu*>(w)->setOnChanged(
                [this, id]() { this->Dispatch(id, EventKind::DropdownChanged); });
            break;
        case EventKind::TextChanged:
            static_cast<TextboxT*>(w)->setOnTextChanged(
                [this, id]() { this->Dispatch(id, EventKind::TextChanged); });
            break;
        case EventKind::SelectItem:
            // already_selectedは永続プロパティとして持てない一時的な値なので、
            // DispatchClosedと同じ形の専用Dispatchで2引数目として渡す
            // (indexは"selected_index"プロパティとして既に読めるので渡さない)
            static_cast<ScrollList*>(w)->setOnSelectItem(
                [this, id](int, bool already_selected) { this->DispatchSelectItem(id, already_selected); });
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
    if (ProtectedCall(1) != LUA_OK) {
        const char* msg = lua_tostring(L, -1);
        ErrorFunctions::ShowFatal(msg ? msg : "Luaコールバックでエラーが発生しました");
        lua_pop(L, 1);
    }
}

void LuaEngine::DispatchClosed(WidgetId id, bool is_ok) {
    int ref = LUA_NOREF;
    for (const auto& e : callbacks_) {
        if (e.id == id && e.kind == EventKind::Closed) { ref = e.ref; break; }
    }
    if (ref != LUA_NOREF) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        lua_pushinteger(L, (lua_Integer)id);
        lua_pushboolean(L, is_ok);
        if (ProtectedCall(2) != LUA_OK) {
            const char* msg = lua_tostring(L, -1);
            ErrorFunctions::ShowFatal(msg ? msg : "Luaコールバックでエラーが発生しました");
            lua_pop(L, 1);
        }
    }
    // pico.on(id,"closed",fn)を呼んでいなくても、ダイアログは必ずここで片付ける
    // (呼び忘れがモーダルの居座りにならないようにするための保証。クラスコメント
    // 「ダイアログ」参照)
    Widget* w = WidgetRegistry::Resolve(id);
    if (w) WidgetFunctions::DestroyLater(w);
}

void LuaEngine::DispatchSelectItem(WidgetId id, bool already_selected) {
    int ref = LUA_NOREF;
    for (const auto& e : callbacks_) {
        if (e.id == id && e.kind == EventKind::SelectItem) { ref = e.ref; break; }
    }
    if (ref == LUA_NOREF) return;

    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    lua_pushinteger(L, (lua_Integer)id);
    lua_pushboolean(L, already_selected);
    if (ProtectedCall(2) != LUA_OK) {
        const char* msg = lua_tostring(L, -1);
        ErrorFunctions::ShowFatal(msg ? msg : "Luaコールバックでエラーが発生しました");
        lua_pop(L, 1);
    }
}

void LuaEngine::WireDialogClosed(Widget* dialog, WidgetId id) {
    // キャプチャはthis(LuaEngine*)+id(WidgetId)だけなので、他のBindCallback同様
    // std::functionの小バッファに収まる
    auto handler = [this, id](bool is_ok) { this->DispatchClosed(id, is_ok); };
    switch (dialog->getWidgetType()) {
        case WidgetType::MsgDialog:
            static_cast<MsgDialog*>(dialog)->setOnClosed(handler);
            break;
        case WidgetType::InputDialog:
            static_cast<InputDialog*>(dialog)->setOnClosed(handler);
            break;
        case WidgetType::FileSaveDialog:
            static_cast<FileSaveDialog*>(dialog)->setOnClose(handler);
            break;
        case WidgetType::FileSelectDialog:
            static_cast<FileSelectDialog*>(dialog)->setOnClose(handler);
            break;
        case WidgetType::ColorDialog:
            static_cast<ColorDialog*>(dialog)->setOnClose(handler);
            break;
        default:
            break; // pico.show_xxx()から渡される型は上の5種のみ
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

    if (kind == EventKind::Closed) {
        switch (w->getWidgetType()) {
            case WidgetType::MsgDialog:
            case WidgetType::InputDialog:
            case WidgetType::FileSaveDialog:
            case WidgetType::FileSelectDialog:
            case WidgetType::ColorDialog:
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

int LuaEngine::l_pop(lua_State*) {
    // LuaSceneがアプリを起動する際はSceneFunctions::Pushなので、Popでランチャへ戻れる
    // (ClocksScene/CalculatorScene等、他のアプリの「戻る」ボタンと同じ仕組み)。
    // 要求を登録するだけで実際の遷移はフレーム境界(SceneFunctions::Update())まで保留される
    SceneFunctions::Pop();
    return 0;
}

int LuaEngine::l_push_scene(lua_State* L) {
    LuaEngine* self = Self(L);
    const char* path = luaL_checkstring(L, 1);

    // LuaScene(path, permissions)のコンストラクタはFixedStringへコピーするだけなので、
    // ここで即座に構築してよい(SDを開くのはSceneFunctions::Update()経由のonEnter()から)。
    // pico.pop()と同じく要求を登録するだけで、実際の遷移・エラー表示(ファイル不在等)は
    // 次のフレーム境界(LuaScene::onEnter())まで保留される。今のスクリプト(=このLuaEngine)は
    // その時点でonExit()経由で破棄されるので、この呼び出し自体は安全に戻ってこられる。
    //
    // 権限は「スクリプトファイル単位」ではなく「アプリ単位」で決まるものとして、
    // 今のLuaEngineが持つLuaPermissionsをそのまま引き継ぐ(push_scene/change_sceneは
    // 同じアプリの内部で別の画面へ移るためのAPIなので、遷移のたびに権限が既定値
    // (最小権限)へ戻ってしまうと、複数画面のLuaアプリで2画面目以降だけ権限が
    // 落ちるという分かりにくい挙動になる)。app_dir自体は遷移先スクリプト自身の
    // 親ディレクトリから改めて計算し直す(LuaScene::onEnter()側)
    SceneFunctions::Push(new LuaScene(path, self->permissions_));
    return 0;
}

int LuaEngine::l_change_scene(lua_State* L) {
    LuaEngine* self = Self(L);
    const char* path = luaL_checkstring(L, 1);
    // push_sceneと違いスタックを消費しない(戻れなくなる)版。l_push_sceneのコメント参照
    // (権限の引き継ぎ方も同じ)
    SceneFunctions::Change(new LuaScene(path, self->permissions_));
    return 0;
}

int LuaEngine::l_launch_app(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);

    // AppFunctions::Launch()と同じPush経路(C++製アプリ含め登録簿の全アプリへ飛べる)。
    // 名前の綴りミス等その場で判定できる失敗だけbool falseで返す
    // (実際のシーン遷移自体はpico.pop()/push_scene同様フレーム境界まで保留される)
    const bool ok = AppFunctions::LaunchByName(name);
    lua_pushboolean(L, ok);
    return 1;
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

int LuaEngine::l_get_time(lua_State* L) {
    // TimeFunctions::timeinfoはmain.cpp起動時のTimeFunctions::Setup()以降、333msごとに
    // 更新される(クラスコメント「時刻取得」参照)。NTP未同期の間の値の妥当性は
    // 呼び出し元(このAPI)では保証しない(ClocksScene等、既存の利用箇所と同じ割り切り)
    const struct tm& t = TimeFunctions::timeinfo;

    lua_newtable(L);
    lua_pushinteger(L, TimeFunctions::year);  lua_setfield(L, -2, "year");
    lua_pushinteger(L, TimeFunctions::month); lua_setfield(L, -2, "month");
    lua_pushinteger(L, t.tm_mday); lua_setfield(L, -2, "day");
    lua_pushinteger(L, t.tm_hour); lua_setfield(L, -2, "hour");
    lua_pushinteger(L, t.tm_min);  lua_setfield(L, -2, "min");
    lua_pushinteger(L, t.tm_sec);  lua_setfield(L, -2, "sec");
    lua_pushinteger(L, t.tm_wday); lua_setfield(L, -2, "wday"); // 0=日曜〜6=土曜(tm_wdayそのまま)
    return 1;
}

int LuaEngine::l_get_touch(lua_State* L) {
    // OSData::touchX/touchYはWidgetFunctions::HitTest()(src/functions/Widget_Functions.cpp)
    // が当たり判定にそのまま使っている絶対スクリーン座標。pico.draw_*やpico.content_rect()
    // と同じ座標系なので、press_start等のコールバック内でそのまま使える。
    // isTouchEnd(離した瞬間)でも座標はリセットされず最後の値を保持したままなので
    // (Touch_Functions*.hppのUpdate()参照)、press_endの中で読んでも問題ない。
    lua_pushinteger(L, OSData::touchX);
    lua_pushinteger(L, OSData::touchY);
    lua_pushboolean(L, OSData::isTouched);
    return 3;
}

int LuaEngine::l_sound_available(lua_State* L) {
    // アンプが刺さっていて、かつsound.cfgでoffにされていないとき(=実際に音が出るとき)だけtrue。
    // falseでもpico.beep()等は呼んでよい(黙って鳴ったことになる)。音で知らせる代わりに
    // 画面でも知らせたいアプリが見分けるためのもの
    lua_pushboolean(L, SoundFunctions::IsAvailable());
    return 1;
}

int LuaEngine::l_beep(lua_State* L) {
    // チャンネル1で矩形波を鳴らすだけの簡易版(pico.sound_playの省略形)。
    // 長さは10秒で頭打ち(うっかり長い値を渡しても困らないように)
    const lua_Integer freq = luaL_checkinteger(L, 1);
    const lua_Integer ms   = luaL_checkinteger(L, 2);
    const uint16_t f = (uint16_t)std::clamp<lua_Integer>(freq, 0, 20000);
    const uint16_t d = (uint16_t)std::clamp<lua_Integer>(ms, 0, 10000);
    Self(L)->used_sound_ = true;
    SoundFunctions::Beep(f, d);
    return 0;
}

namespace {
    // pico.sound_play の wave に書ける名前(ChipSynth::Waveの並びと同じ順)
    const char* const kWaveNames[] = {
        "pulse12", "pulse25", "pulse50", "pulse75", "triangle", "saw", "noise", "noise_short",
    };
    static_assert(sizeof(kWaveNames) / sizeof(kWaveNames[0]) == (size_t)ChipSynth::Wave::kCount,
                  "kWaveNamesをChipSynth::Waveと揃えること");

    // Luaのチャンネル番号(1始まり)→ 0始まり。範囲外はエラー
    uint8_t CheckChannel(lua_State* L, int arg){
        const lua_Integer ch = luaL_checkinteger(L, arg);
        if (ch < 1 || ch > SoundFunctions::kChannels) {
            luaL_error(L, "チャンネルは1〜%dです(%d)", SoundFunctions::kChannels, (int)ch);
        }
        return (uint8_t)(ch - 1);
    }
}

int LuaEngine::l_sound_play(lua_State* L) {
    // pico.sound_play(ch, freq, ms [, {wave=, volume=, envelope=}]) -> bool
    const uint8_t ch = CheckChannel(L, 1);
    const lua_Number freq = luaL_checknumber(L, 2);
    const lua_Integer ms = luaL_checkinteger(L, 3);

    ChipSynth::Note note;
    note.freq_x16 = (freq <= 0) ? 0 : (uint32_t)std::min<lua_Number>(freq * 16.0 + 0.5, 1e9);
    //長さは1分で頭打ち。0は「止めるまで鳴らし続ける」
    note.length_ms = (uint32_t)std::clamp<lua_Integer>(ms, 0, 60000);

    if (!lua_isnoneornil(L, 4)) {
        luaL_checktype(L, 4, LUA_TTABLE);

        lua_getfield(L, 4, "wave");
        if (!lua_isnil(L, -1)) {
            const char* name = luaL_checkstring(L, -1);
            bool found = false;
            for (size_t i = 0; i < (size_t)ChipSynth::Wave::kCount; i++) {
                if (strcmp(name, kWaveNames[i]) == 0) {
                    note.wave = (ChipSynth::Wave)i;
                    found = true;
                    break;
                }
            }
            if (!found) return luaL_error(L, "pico.sound_play: 不明な波形です(%s)", name);
        }
        lua_pop(L, 1);

        lua_getfield(L, 4, "volume");
        if (!lua_isnil(L, -1)) note.volume = (uint8_t)std::clamp<lua_Integer>(luaL_checkinteger(L, -1), 0, 15);
        lua_pop(L, 1);

        lua_getfield(L, 4, "envelope");
        if (!lua_isnil(L, -1)) note.envelope = (int8_t)std::clamp<lua_Integer>(luaL_checkinteger(L, -1), -7, 7);
        lua_pop(L, 1);
    }

    //周波数0は「止める」と同じ扱い(休符を書きやすいように)
    Self(L)->used_sound_ = true;
    if (note.freq_x16 == 0) {
        SoundFunctions::Stop(ch);
        lua_pushboolean(L, 1);
        return 1;
    }
    lua_pushboolean(L, SoundFunctions::Play(ch, note));
    return 1;
}

int LuaEngine::l_sound_stop(lua_State* L) {
    // pico.sound_stop([ch]) chを省略すると全部
    if (lua_isnoneornil(L, 1)) SoundFunctions::StopAll();
    else SoundFunctions::Stop(CheckChannel(L, 1));
    return 0;
}

int LuaEngine::l_sound_playing(lua_State* L) {
    // pico.sound_playing([ch]) -> bool。chを省略するとどれか1つでも
    if (lua_isnoneornil(L, 1)) {
        lua_pushboolean(L, SoundFunctions::IsPlaying());
    } else {
        const uint8_t ch = CheckChannel(L, 1);
        lua_pushboolean(L, (SoundFunctions::ActiveChannels() >> ch) & 1);
    }
    return 1;
}

namespace {
    // 曲の読み込み結果をLuaへ返す: 成功なら true、失敗なら nil, "3行12列: 理由"
    int PushMusicResult(lua_State* L, bool ok, const MmlResult& r) {
        if (ok) {
            lua_pushboolean(L, 1);
            return 1;
        }
        lua_pushnil(L);
        if (r.line > 0) lua_pushfstring(L, "%d行%d列: %s", r.line, r.col, r.message.c_str());
        else lua_pushstring(L, r.message.c_str());
        return 2;
    }
}

int LuaEngine::l_music_play(lua_State* L) {
    // pico.music_play(path) -> true | nil, 理由
    LuaEngine* self = Self(L);
    const char* path = luaL_checkstring(L, 1);
    MmlResult r;
    if (!OSData::SD_usable) {
        r.message.assign("SDカードが使えません");
        return PushMusicResult(L, false, r);
    }
    if (!self->SdPathAllowed(path)) {
        LOG_APP_WARN("pico.music_play: アプリディレクトリ外へのアクセスは許可されていません: %s", path);
        r.message.assign("このアプリからは読めない場所です");
        return PushMusicResult(L, false, r);
    }
    self->used_music_ = true;
    const bool ok = SoundFunctions::MusicPlayFile(path, &r);
    return PushMusicResult(L, ok, r);
}

int LuaEngine::l_music_play_text(lua_State* L) {
    // pico.music_play_text(mml) -> true | nil, 理由
    size_t len = 0;
    const char* text = luaL_checklstring(L, 1, &len);
    Self(L)->used_music_ = true;
    MmlResult r;
    const bool ok = SoundFunctions::MusicPlayText(text, len, &r);
    return PushMusicResult(L, ok, r);
}

int LuaEngine::l_music_stop(lua_State* L) {
    (void)L;
    SoundFunctions::MusicStop();
    return 0;
}

int LuaEngine::l_music_playing(lua_State* L) {
    lua_pushboolean(L, SoundFunctions::MusicPlaying());
    return 1;
}

int LuaEngine::l_note_freq(lua_State* L) {
    // pico.note_freq("C4" | 60) -> number | nil
    int note = -1;
    if (lua_type(L, 1) == LUA_TNUMBER) {
        if (!lua_isinteger(L, 1)) return luaL_error(L, "pico.note_freq: ノート番号は整数です");
        note = (int)std::clamp<lua_Integer>(lua_tointeger(L, 1), -1, 128);
    } else {
        note = NoteName::Parse(luaL_checkstring(L, 1));
    }
    const float f = NoteName::MidiToFreq(note);
    if (f <= 0.0f) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushnumber(L, f);
    return 1;
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
    if (!self->SdPathAllowed(path)) {
        LOG_APP_WARN("pico.image_load: アプリディレクトリ外へのアクセスは許可されていません: %s", path);
        lua_pushnil(L);
        return 1;
    }

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

// ---------------- ラスタキャンバス(CanvasRaster) ----------------
// ヘッダのクラスコメント「ラスタキャンバスの保存/読み込み」参照。

namespace {
    // 3関数共通: idを解決し、CanvasRaster以外ならluaL_error。
    // 呼び出し側は戻り値nullptrをチェックする必要は無い(エラーはここで飛ぶ)
    CanvasRaster* ResolveCanvasRasterOrError(lua_State* L, int arg_index, const char* fn_name) {
        const WidgetId id = (WidgetId)luaL_checkinteger(L, arg_index);
        Widget* w = WidgetRegistry::Resolve(id);
        if (!w) {
            luaL_error(L, "%s: 無効なID", fn_name);
            return nullptr; // 到達しない(luaL_errorはlongjmpする)
        }
        if (w->getWidgetType() != WidgetType::CanvasRaster) {
            luaL_error(L, "%s: CanvasRaster以外には使えません", fn_name);
            return nullptr;
        }
        return static_cast<CanvasRaster*>(w);
    }

    // 実機の上限に関わらず「画面に収まらないサイズを.pimgから復元して確保する」
    // 事故を防ぐための上限(SCREEN_WIDTH/HEIGHT基準)。不正/悪意あるファイルが
    // 巨大なwidth/heightを名乗っていても、ここで弾けばcreateSprite()の
    // 大量確保まで進まない
    bool CanvasSizeSane(uint16_t w, uint16_t h) {
        return w > 0 && h > 0 && w <= SCREEN_WIDTH && h <= SCREEN_HEIGHT;
    }
}

int LuaEngine::l_canvas_clear(lua_State* L) {
    CanvasRaster* cr = ResolveCanvasRasterOrError(L, 1, "pico.canvas_clear");
    cr->canvasClear();
    return 0;
}

int LuaEngine::l_canvas_save(lua_State* L) {
    LuaEngine* self = Self(L);
    CanvasRaster* cr = ResolveCanvasRasterOrError(L, 1, "pico.canvas_save");
    const char* path = luaL_checkstring(L, 2);

    if (!OSData::SD_usable) { lua_pushboolean(L, false); return 1; }
    if (!self->SdPathAllowed(path)) {
        LOG_APP_WARN("pico.canvas_save: アプリディレクトリ外へのアクセスは許可されていません: %s", path);
        lua_pushboolean(L, false);
        return 1;
    }

    FsFile f = OSData::SD.open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (!f) { lua_pushboolean(L, false); return 1; }

    const bool ok = IconRender::EncodePimg(*cr->getSprite(), (uint16_t)cr->getW(), (uint16_t)cr->getH(), f);
    f.close();
    lua_pushboolean(L, ok);
    return 1;
}

int LuaEngine::l_canvas_load(lua_State* L) {
    LuaEngine* self = Self(L);
    CanvasRaster* cr = ResolveCanvasRasterOrError(L, 1, "pico.canvas_load");
    const char* path = luaL_checkstring(L, 2);
    const bool keep_size = lua_toboolean(L, 3);

    if (!OSData::SD_usable) { lua_pushboolean(L, false); return 1; }
    if (!self->SdPathAllowed(path)) {
        LOG_APP_WARN("pico.canvas_load: アプリディレクトリ外へのアクセスは許可されていません: %s", path);
        lua_pushboolean(L, false);
        return 1;
    }

    FsFile f = OSData::SD.open(path, O_RDONLY);
    if (!f) { lua_pushboolean(L, false); return 1; }

    IconRender::PimgHeader header;
    if (!IconRender::ReadPimgHeader(f, header) || !CanvasSizeSane(header.width, header.height)) {
        f.close();
        lua_pushboolean(L, false);
        return 1;
    }

    if (keep_size) {
        // 大きさは変えず、白紙にしてから左上に合わせて読む。DecodePimgBody()は
        // 画像の幅で行を折り返し、スプライトの外へ出た画素はwritePixel()のクリップで
        // 捨てられるので、大きい画像は右/下が切れ、小さい画像は余白が白で残る
        cr->saveUndoPoint();
        cr->getSprite()->clear(PICO_WHITE);
    } else {
        // 保存時と現在のw/hが食い違っていても読み込めるよう、先にキャンバス自体を
        // 画像のサイズへ合わせる(CanvasRaster::resize()。この時点で旧内容は消える)
        cr->resize((int16_t)header.width, (int16_t)header.height);
    }

    const bool ok = IconRender::DecodePimgBody(f, *cr->getSprite(), header.width, header.height);
    f.close();
    if (ok) cr->needsRender();
    lua_pushboolean(L, ok);
    return 1;
}

int LuaEngine::l_canvas_undo(lua_State* L) {
    CanvasRaster* cr = ResolveCanvasRasterOrError(L, 1, "pico.canvas_undo");
    lua_pushboolean(L, cr->undo());
    return 1;
}

// ---------------- SDカードアクセス ----------------
// ヘッダのクラスコメント参照。OSData::SD_usable==falseの間はどれも失敗(false/nil)を
// 返すだけでluaL_errorにはしない。app_dir_の外を指すパスも同じ扱い(SdPathAllowed()参照。
// プログラマの書き間違いだけでなく、悪意あるスクリプトが試す経路でもあるため
// luaL_errorで詳細を返さず、SD無し等と同じ「実行時の状態」枠にまとめてある)。

bool LuaEngine::SdPathAllowed(const char* path) const {
    if (permissions_.sd_outside_app_dir) return true;

    FixedString<PICO_PATH_LEN> normalized;
    if (!PICO_IO::normalize(normalized, path)) return false;

    const size_t dir_len = app_dir_.length();
    // app_dir_=="/"(既定値。LuaScene以外がapp_dirを指定せずLuaEngineを直接使う場合)は
    // 「制限なし」に相当する
    if (dir_len <= 1) return true;

    const char* p = normalized.c_str();
    const char* dir = app_dir_.c_str();
    if (strncmp(p, dir, dir_len) != 0) return false;
    // "/lua/foo"は"/lua"の配下だが、"/luaxxx"のような別ディレクトリを誤って配下と
    // 判定しないよう、続きがパス終端か'/'であることまで確認する
    return p[dir_len] == '\0' || p[dir_len] == '/';
}

int LuaEngine::l_sd_exists(lua_State* L) {
    LuaEngine* self = Self(L);
    const char* path = luaL_checkstring(L, 1);
    lua_pushboolean(L, OSData::SD_usable && self->SdPathAllowed(path) && OSData::SD.exists(path));
    return 1;
}

int LuaEngine::l_sd_read(lua_State* L) {
    LuaEngine* self = Self(L);
    const char* path = luaL_checkstring(L, 1);
    if (!OSData::SD_usable) { lua_pushnil(L); return 1; }
    if (!self->SdPathAllowed(path)) {
        LOG_APP_WARN("pico.sd_read: アプリディレクトリ外へのアクセスは許可されていません: %s", path);
        lua_pushnil(L);
        return 1;
    }

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
    LuaEngine* self = Self(L);
    const char* path = luaL_checkstring(L, 1);
    size_t len = 0;
    const char* data = luaL_checklstring(L, 2, &len);
    const bool append = lua_toboolean(L, 3);

    if (!OSData::SD_usable) { lua_pushboolean(L, false); return 1; }
    if (!self->SdPathAllowed(path)) {
        LOG_APP_WARN("pico.sd_write: アプリディレクトリ外へのアクセスは許可されていません: %s", path);
        lua_pushboolean(L, false);
        return 1;
    }

    FsFile f = OSData::SD.open(path, O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC));
    if (!f) { lua_pushboolean(L, false); return 1; }

    const bool ok = (len == 0) || (f.write(data, len) == len);
    f.close();
    lua_pushboolean(L, ok);
    return 1;
}

int LuaEngine::l_sd_remove(lua_State* L) {
    LuaEngine* self = Self(L);
    const char* path = luaL_checkstring(L, 1);
    if (!OSData::SD_usable) { lua_pushboolean(L, false); return 1; }
    if (!self->SdPathAllowed(path)) {
        LOG_APP_WARN("pico.sd_remove: アプリディレクトリ外へのアクセスは許可されていません: %s", path);
        lua_pushboolean(L, false);
        return 1;
    }

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
    LuaEngine* self = Self(L);
    const char* path = luaL_checkstring(L, 1);
    if (!OSData::SD_usable) { lua_pushboolean(L, false); return 1; }
    if (!self->SdPathAllowed(path)) {
        LOG_APP_WARN("pico.sd_mkdir: アプリディレクトリ外へのアクセスは許可されていません: %s", path);
        lua_pushboolean(L, false);
        return 1;
    }

    lua_pushboolean(L, OSData::SD.mkdir(path));
    return 1;
}

int LuaEngine::l_sd_list(lua_State* L) {
    LuaEngine* self = Self(L);
    const char* path = luaL_checkstring(L, 1);
    if (!OSData::SD_usable) { lua_pushnil(L); return 1; }
    if (!self->SdPathAllowed(path)) {
        LOG_APP_WARN("pico.sd_list: アプリディレクトリ外へのアクセスは許可されていません: %s", path);
        lua_pushnil(L);
        return 1;
    }

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

    InputDialog* dialog = new InputDialog(label, is_single_line);
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

// ---------------- ネットワーク ----------------
// ヘッダのクラスコメント「ネットワーク」参照。

int LuaEngine::l_http_request(lua_State* L) {
    LuaEngine* self = Self(L);
    const char* method_str = luaL_checkstring(L, 1);
    const char* url_str = luaL_checkstring(L, 2);
    size_t body_len = 0;
    const char* body = lua_isnoneornil(L, 3) ? nullptr : luaL_checklstring(L, 3, &body_len);
    const char* content_type = lua_isnoneornil(L, 4) ? nullptr : luaL_checkstring(L, 4);
    luaL_checktype(L, 5, LUA_TFUNCTION);

    HttpRequest::Method method;
    if (!HttpMethodFromName(method_str, method)) {
        return luaL_error(L, "pico.http_request: 未知のメソッド '%s'(GET/POST/PUT/PATCH/DELETEのいずれか)", method_str);
    }

    if (!self->permissions_.network) {
        LOG_APP_WARN("pico.http_request: このアプリにはネットワーク権限がありません");
        lua_pushboolean(L, false);
        return 1;
    }

    // 同時に1本まで。前のリクエストが完了していなければ黙って拒否する
    // (SD無し等と同じ「実行時の状態」枠として扱い、luaL_errorにはしない)
    if (self->http_ && self->http_->callback_ref != LUA_NOREF) {
        lua_pushboolean(L, false);
        return 1;
    }

    Url url;
    if (!UrlTools::Parse(url, url_str)) {
        lua_pushboolean(L, false); // 不正なURL(httpsも通る。接続はHttp_Transportが担う)
        return 1;
    }

    if (body_len > kMaxHttpBodyBytes) {
        LOG_APP_WARN("pico.http_request: リクエストボディが上限(%uB)を超えています",
            (unsigned)kMaxHttpBodyBytes);
        lua_pushboolean(L, false);
        return 1;
    }

    if (!self->http_) self->http_ = new HttpState();
    HttpState* st = self->http_;

    st->sink.body.clear();
    st->body_buf.clear();
    st->content_type_buf.clear();

    // HttpRequestは送信ボディ/Content-Typeを非所有ポインタで受け取るため、
    // Luaスタック上の一時的な文字列をそのまま渡さず、リクエストが終わるまで
    // 生きているst->body_buf/content_type_bufへ一度コピーしてから渡す
    const void* body_ptr = nullptr;
    if (body && body_len > 0) {
        st->body_buf.assign(body, body_len);
        body_ptr = st->body_buf.c_str();
        body_len = st->body_buf.length();
    }
    const char* content_type_ptr = nullptr;
    if (content_type && *content_type) {
        st->content_type_buf.assign(content_type);
        content_type_ptr = st->content_type_buf.c_str();
    }

    if (!st->request.begin(url, method, &st->sink, body_ptr, body_len, content_type_ptr)) {
        lua_pushboolean(L, false);
        return 1;
    }

    lua_pushvalue(L, 5);
    st->callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    lua_pushboolean(L, true);
    return 1;
}

int LuaEngine::l_http_cancel(lua_State* L) {
    LuaEngine* self = Self(L);
    if (self->http_ && self->http_->callback_ref != LUA_NOREF) {
        self->http_->request.cancel();
        luaL_unref(L, LUA_REGISTRYINDEX, self->http_->callback_ref);
        self->http_->callback_ref = LUA_NOREF;
    }
    return 0;
}

void LuaEngine::UpdateHttp() {
    if (!http_ || http_->callback_ref == LUA_NOREF) return;

    http_->request.update();
    if (http_->request.getStatus() == TaskTools::PROCESSING) return;

    const bool ok = (http_->request.getStatus() == TaskTools::SUCCESS);
    const int status_code = ok ? http_->request.response().statusCode() : 0;

    // 先に外しておく: コールバック内からpico.http_request()を再度呼べるようにするため
    // (LuaEngine::l_http_requestの「同時に1本まで」判定はcallback_refを見ている)
    const int ref = http_->callback_ref;
    http_->callback_ref = LUA_NOREF;

    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    lua_pushboolean(L, ok);
    lua_pushinteger(L, status_code);
    if (ok && !http_->sink.body.empty()) {
        // 本文にNULが混じり得るため、strlen前提のlua_pushstring()ではなく
        // 長さ明示のlua_pushlstring()を使う
        lua_pushlstring(L, http_->sink.body.c_str(), http_->sink.body.length());
    } else {
        lua_pushnil(L);
    }
    if (!ok) {
        lua_pushstring(L, http_->request.failureToStr());
    } else {
        lua_pushnil(L);
    }

    if (ProtectedCall(4) != LUA_OK) {
        const char* msg = lua_tostring(L, -1);
        ErrorFunctions::ShowFatal(msg ? msg : "pico.http_requestのコールバックでエラーが発生しました");
        lua_pop(L, 1);
    }

    luaL_unref(L, LUA_REGISTRYINDEX, ref);
}
