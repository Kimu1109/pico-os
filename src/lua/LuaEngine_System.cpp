// pico.* のうち画面の遷移・通知・時刻・タッチ・コントローラー・電池・打鍵
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


using namespace LuaInternal;

namespace {
    // push_scene/change_scene/pop が画面の間で渡す値(引数・結果)をJSONにする。
    // 渡さない(nil)なら空文字列。1KiBに収まらなければ luaL_error
    void EncodeSceneValue(lua_State* L, int idx, const char* api, FixedString<PICO_STR_1KiB>& out) {
        out.clear();
        if (lua_isnoneornil(L, idx)) return;
        const char* err = nullptr; // 静的文字列
        bool too_big = false;
        {   // luaL_error(longjmp)はデストラクタを飛ばすので、std::stringはここで確実に破棄してから投げる
            std::string json;
            const int base = lua_gettop(L);
            const bool ok = LuaJson::EncodeValue(L, idx, json, 0, err);
            lua_settop(L, base);
            if (ok) {
                if (json.size() >= PICO_STR_1KiB) too_big = true;
                else out.assign(json.c_str());
            } else if (!err) {
                err = "?";
            }
        }
        if (err) luaL_error(L, "%s: 値をJSONにできません(%s)", api, err);
        if (too_big) {
            luaL_error(L, "%s: 渡す値が大きすぎます(%dバイトまで。大きなデータはファイルに置いてください)",
                       api, (int)PICO_STR_1KiB - 1);
        }
    }
}

int LuaEngine::l_pop(lua_State* L) {
    LuaEngine* self = Self(L);
    // pico.pop(result): 呼び出し元(push_sceneした画面)の on_result(result) に渡す値(任意)。
    // 親はこのあとPopで戻ってonEnter()からやり直すので、受け渡しはLuaSceneの待ち箱を介す
    if (!lua_isnoneornil(L, 1)) {
        FixedString<PICO_STR_1KiB> json;
        EncodeSceneValue(L, 1, "pico.pop", json);
        if (!self->parent_path_.empty()) {
            LuaScene::PostResult(self->parent_path_.c_str(), json.c_str());
        }
    }
    // LuaSceneがアプリを起動する際はSceneFunctions::Pushなので、Popでランチャへ戻れる
    // (ClocksScene/CalculatorScene等、他のアプリの「戻る」ボタンと同じ仕組み)。
    // 要求を登録するだけで実際の遷移はフレーム境界(SceneFunctions::Update())まで保留される
    SceneFunctions::Pop();
    return 0;
}

int LuaEngine::l_push_scene(lua_State* L) {
    LuaEngine* self = Self(L);
    const char* path = luaL_checkstring(L, 1);

    // pico.push_scene(path [, args]): argsは遷移先で pico.args() として受け取る(JSONにできる値)
    FixedString<PICO_STR_1KiB> args;
    EncodeSceneValue(L, 2, "pico.push_scene", args);

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
    LuaScene* scene = new LuaScene(path, self->permissions_);
    scene->setInheritedSampleRate(self->requested_rate_);
    // 子から見た「親」は今のスクリプト(pico.pop(result)の宛先)
    scene->setLaunchArgs(args.c_str(), self->script_path_.c_str());
    SceneFunctions::Push(scene);
    return 0;
}

int LuaEngine::l_change_scene(lua_State* L) {
    LuaEngine* self = Self(L);
    const char* path = luaL_checkstring(L, 1);
    FixedString<PICO_STR_1KiB> args;
    EncodeSceneValue(L, 2, "pico.change_scene", args);
    // push_sceneと違いスタックを消費しない(戻れなくなる)版。l_push_sceneのコメント参照
    // (権限の引き継ぎ方も同じ)。置き換えた先から pop(result) したときの宛先は、
    // 今の画面の「親」をそのまま引き継ぐ(置き換えた画面は消えるため)
    LuaScene* scene = new LuaScene(path, self->permissions_);
    scene->setInheritedSampleRate(self->requested_rate_);
    scene->setLaunchArgs(args.c_str(), self->parent_path_.c_str());
    SceneFunctions::Change(scene);
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

// ---- 通知 ----

void LuaEngine::SetLaunchReason(const char* tag, const char* data) {
    has_launch_reason_ = true;
    launch_tag_.assign(tag ? tag : "");
    launch_data_.assign(data ? data : "");
}

namespace {
    // テーブルの文字列フィールドを読む(無ければfalse。文字列以外はエラー)
    template<size_t N>
    bool OptStringField(lua_State* L, int t, const char* name, FixedString<N>& out) {
        lua_getfield(L, t, name);
        if (lua_isnil(L, -1)) { lua_pop(L, 1); return false; }
        if (!lua_isstring(L, -1)) {
            lua_pop(L, 1);
            luaL_error(L, "pico.notify: %s は文字列で指定してください", name);
            return false;
        }
        NotificationFunctions::Sanitize(out, lua_tostring(L, -1));
        lua_pop(L, 1);
        return true;
    }

    // 数値フィールド(整数)。無ければfalse
    bool OptIntField(lua_State* L, int t, const char* name, lua_Integer& out) {
        lua_getfield(L, t, name);
        if (lua_isnil(L, -1)) { lua_pop(L, 1); return false; }
        int isnum = 0;
        const lua_Integer v = lua_tointegerx(L, -1, &isnum);
        lua_pop(L, 1);
        if (!isnum) luaL_error(L, "pico.notify: %s は整数で指定してください", name);
        out = v;
        return true;
    }

    // at = エポック秒 または {year=, month=, day=, hour=, min=, sec=}(現地時刻)
    int64_t AtFromTable(lua_State* L, int t) {
        struct tm tm_ = {};
        lua_Integer v = 0;
        auto need = [&](const char* name) -> lua_Integer {
            if (!OptIntField(L, t, name, v)) luaL_error(L, "pico.notify: at に %s がありません", name);
            return v;
        };
        tm_.tm_year = (int)need("year") - 1900;
        tm_.tm_mon  = (int)need("month") - 1;
        tm_.tm_mday = (int)need("day");
        tm_.tm_hour = OptIntField(L, t, "hour", v) ? (int)v : 0;
        tm_.tm_min  = OptIntField(L, t, "min", v) ? (int)v : 0;
        tm_.tm_sec  = OptIntField(L, t, "sec", v) ? (int)v : 0;
        tm_.tm_isdst = -1;
        const time_t e = mktime(&tm_);
        if (e == (time_t)-1) luaL_error(L, "pico.notify: at の日時が正しくありません");
        return (int64_t)e;
    }
}

int LuaEngine::l_notify(lua_State* L) {
    using namespace NotificationFunctions;
    LuaEngine* self = Self(L);
    luaL_checktype(L, 1, LUA_TTABLE);

    Content c;
    if (!OptStringField(L, 1, "title", c.title) || c.title.empty()) {
        return luaL_error(L, "pico.notify: title は必須です");
    }
    OptStringField(L, 1, "body", c.body);
    OptStringField(L, 1, "tag", c.tag);
    OptStringField(L, 1, "data", c.data);
    lua_getfield(L, 1, "sound");
    if (!lua_isnil(L, -1)) c.sound = lua_toboolean(L, -1);
    lua_pop(L, 1);

    // いつ出すか(どれか1つ)
    When w;
    int triggers = 0;
    lua_Integer v = 0;
    if (OptIntField(L, 1, "delay_ms", v)) {
        if (v <= 0) return luaL_error(L, "pico.notify: delay_ms は1以上です");
        w.trigger = Trigger::Delay; w.delay_ms = (unsigned long)v; triggers++;
    }
    if (OptIntField(L, 1, "every_ms", v)) {
        if (v < (lua_Integer)kMinEveryMs) {
            return luaL_error(L, "pico.notify: every_ms は%lu以上です", kMinEveryMs);
        }
        w.trigger = Trigger::Every; w.every_ms = (unsigned long)v; triggers++;
    }
    lua_getfield(L, 1, "at");
    if (!lua_isnil(L, -1)) {
        if (lua_istable(L, -1)) {
            w.at_epoch = AtFromTable(L, lua_gettop(L));
        } else if (lua_isinteger(L, -1)) {
            w.at_epoch = (int64_t)lua_tointeger(L, -1);
        } else {
            return luaL_error(L, "pico.notify: at はエポック秒か {year=,month=,day=,hour=,min=} です");
        }
        if (w.at_epoch <= 0) return luaL_error(L, "pico.notify: at の日時が正しくありません");
        w.trigger = Trigger::At; triggers++;
    }
    lua_pop(L, 1);
    lua_getfield(L, 1, "daily");
    if (!lua_isnil(L, -1)) {
        int h = -1, m = -1;
        const char* d = lua_tostring(L, -1);
        if (!d || sscanf(d, "%d:%d", &h, &m) != 2 || h < 0 || h > 23 || m < 0 || m > 59) {
            return luaL_error(L, "pico.notify: daily は \"HH:MM\" です");
        }
        w.trigger = Trigger::Daily; w.hour = (uint8_t)h; w.minute = (uint8_t)m; triggers++;
    }
    lua_pop(L, 1);
    lua_getfield(L, 1, "when");
    if (!lua_isnil(L, -1)) {
        const char* cond = lua_tostring(L, -1);
        if (cond && strcmp(cond, "battery_low") == 0) {
            w.trigger = Trigger::BatteryLow;
            if (OptIntField(L, 1, "below", v)) {
                if (v < 1 || v > 99) return luaL_error(L, "pico.notify: below は1〜99です");
                w.below = (uint8_t)v;
            }
        } else if (cond && strcmp(cond, "wifi_connected") == 0) {
            w.trigger = Trigger::WifiConnected;
        } else if (cond && strcmp(cond, "wifi_disconnected") == 0) {
            w.trigger = Trigger::WifiDisconnected;
        } else {
            return luaL_error(L, "pico.notify: when は battery_low / wifi_connected / wifi_disconnected です");
        }
        triggers++;
    }
    lua_pop(L, 1);
    if (triggers > 1) {
        return luaL_error(L, "pico.notify: delay_ms / at / daily / every_ms / when は1つだけ指定してください");
    }

    // 権限はプログラマの誤り(引数の書き間違い)を先に弾いてから見る。
    // SD無し等と同じ「実行時の状態」枠なのでエラーにはせず nil, 理由 を返す
    if (!self->permissions_.notify) {
        LOG_APP_WARN("pico.notify: 通知の権限がありません(app.cfgのpermission_notify)");
        lua_pushnil(L);
        lua_pushstring(L, "通知の権限がありません");
        return 2;
    }

    c.owner.assign(self->app_dir_.c_str());
    AppFunctions::NameForDir(self->app_dir_.c_str(), c.app);

    uint16_t id = 0;
    const Result r = Schedule(c, w, &id);
    if (r != Result::Ok) {
        lua_pushnil(L);
        lua_pushstring(L, ResultToStr(r));
        return 2;
    }
    lua_pushinteger(L, id);
    return 1;
}

int LuaEngine::l_notify_cancel(lua_State* L) {
    using namespace NotificationFunctions;
    LuaEngine* self = Self(L);
    const char* owner = self->app_dir_.c_str();
    int n = 0;
    if (lua_isnoneornil(L, 1)) {
        n = CancelAll(owner);
    } else if (lua_isinteger(L, 1)) {
        const lua_Integer id = lua_tointeger(L, 1);
        n = (id > 0 && id <= 0xFFFF) ? Cancel(owner, (uint16_t)id) : 0;
    } else if (lua_type(L, 1) == LUA_TSTRING) {
        n = CancelTag(owner, lua_tostring(L, 1));
    } else {
        return luaL_error(L, "pico.notify_cancel: 引数は予約のid(整数)かtag(文字列)です");
    }
    lua_pushinteger(L, n);
    return 1;
}

int LuaEngine::l_notify_list(lua_State* L) {
    using namespace NotificationFunctions;
    LuaEngine* self = Self(L);
    lua_newtable(L);
    int out = 0;
    const int n = RuleCount();
    for (int i = 0; i < n; i++) {
        const Rule* r = RuleAt(i);
        if (!r || !(r->content.owner == self->app_dir_.c_str())) continue;
        lua_newtable(L);
        lua_pushinteger(L, r->id);                         lua_setfield(L, -2, "id");
        lua_pushstring(L, r->content.tag.c_str());         lua_setfield(L, -2, "tag");
        lua_pushstring(L, r->content.title.c_str());       lua_setfield(L, -2, "title");
        lua_pushstring(L, TriggerToStr(r->when.trigger));  lua_setfield(L, -2, "kind");
        lua_rawseti(L, -2, ++out);
    }
    return 1;
}

int LuaEngine::l_launch_reason(lua_State* L) {
    LuaEngine* self = Self(L);
    if (!self->has_launch_reason_) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushstring(L, self->launch_tag_.c_str());
    lua_pushstring(L, self->launch_data_.c_str());
    return 2;
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
    // OSData::touchX/touchYはWidgetFunctions::HitTest()(src/gui/core/Widget_Functions.cpp)
    // が当たり判定にそのまま使っている絶対スクリーン座標。pico.draw_*やpico.content_rect()
    // と同じ座標系なので、press_start等のコールバック内でそのまま使える。
    // isTouchEnd(離した瞬間)でも座標はリセットされず最後の値を保持したままなので
    // (Touch_Functions*.hppのUpdate()参照)、press_endの中で読んでも問題ない。
    lua_pushinteger(L, OSData::touchX);
    lua_pushinteger(L, OSData::touchY);
    lua_pushboolean(L, OSData::isTouched);
    return 3;
}

// ---- 外部コントローラー ----
// 状態はloop()の先頭で1回だけ更新される(PadFunctions)ので、1フレームの中では何度読んでも同じ答え。
// 名前はPadFunctions::ButtonFromName()の小文字("up" "a" "start" …)。知らない名前はエラー
// (綴りの間違いで「押しても反応しない」と悩まないように)

static uint16_t CheckPadButton(lua_State* L, int arg) {
    const char* name = luaL_checkstring(L, arg);
    const uint16_t b = PadFunctions::ButtonFromName(name);
    if (b == 0) luaL_error(L, "知らないボタン名です: %s", name);
    return b;
}

// コントローラーを読むアプリ(ゲーム)の間は、コントローラーでフォーカスを動かさない(LuaScene::usesPad())
int LuaEngine::l_pad_connected(lua_State* L) {
    Self(L)->used_pad_ = true;
    lua_pushboolean(L, PadFunctions::IsConnected());
    return 1;
}

int LuaEngine::l_pad_down(lua_State* L) {
    Self(L)->used_pad_ = true;
    lua_pushboolean(L, PadFunctions::IsDown(CheckPadButton(L, 1)));
    return 1;
}

int LuaEngine::l_pad_pressed(lua_State* L) {
    Self(L)->used_pad_ = true;
    lua_pushboolean(L, PadFunctions::Pressed(CheckPadButton(L, 1)));
    return 1;
}

int LuaEngine::l_pad_released(lua_State* L) {
    Self(L)->used_pad_ = true;
    lua_pushboolean(L, PadFunctions::Released(CheckPadButton(L, 1)));
    return 1;
}

// ---- システム ----

int LuaEngine::l_millis(lua_State* L) {
    // 起動からのミリ秒(単調増加。NTPの同期で飛ばない)。経過時間の計測用
    lua_pushinteger(L, (lua_Integer)millis());
    return 1;
}

int LuaEngine::l_micros(lua_State* L) {
    // 起動からのマイクロ秒(32bit で約71分ごとに一巡する)。短い処理の時間を測る用。Luaの整数も32bit(LUA_32BITS)なので差は b - a
    lua_pushinteger(L, (lua_Integer)(uint32_t)micros());
    return 1;
}

int LuaEngine::l_battery(lua_State* L) {
    // pico.battery() -> 残量%(0〜100), 電圧(V), USB給電中か。まだ読めていなければnil
    if (!BatteryFunctions::HasSample()) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, BatteryFunctions::GetPercent());
    lua_pushnumber(L, BatteryFunctions::GetVoltage());
    lua_pushboolean(L, BatteryFunctions::IsExternallyPowered());
    return 3;
}

// pico.on_key(fn)で物理キーボードの打鍵を受け取る(nilで解除)。
// fn(key, mods): keyは文字(UTF-8。Shiftやキー配列は反映済み)か、特殊キーの名前
// ("enter" "backspace" "tab" "escape" "delete" "left" "right" "up" "down" "home" "end"
// "pageup" "pagedown")。modsは{ctrl=,alt=,shift=}。
// 真を返すと「取った」扱い。偽/nilを返すと開いているキーボードの入力へ回る
int LuaEngine::l_on_key(lua_State* L) {
    LuaEngine* self = Self(L);
    if (!lua_isnoneornil(L, 1)) luaL_checktype(L, 1, LUA_TFUNCTION);
    if (self->key_callback_ref_ != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, self->key_callback_ref_);
        self->key_callback_ref_ = LUA_NOREF;
    }
    if (!lua_isnoneornil(L, 1)) {
        lua_pushvalue(L, 1);
        self->key_callback_ref_ = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    return 0;
}
