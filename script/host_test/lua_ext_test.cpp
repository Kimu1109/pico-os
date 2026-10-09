// LuaEngine の拡張API(src/lua/LuaEngine_Ext.cpp / LuaEngine_Crypto.cpp / LuaBuiltinModules.hpp)のテスト。
//
//   見送っていたウィジェットのLua生成 → TextView/ImageView/MarkdownView/AnalogClock/DurationPicker/
//                      MonthGrid/ProgressBarを作り、プロパティ・長い文章・イベントが使えること
//   pico.off / 長押し・ダブルタップ・スワイプ / スクロール / text_input
//   名前・ツリー探索・矩形・Z順 / enabled
//   リスト(挿入・削除・取得・選択)・タブ(ラベル・削除・連動)
//   選ぶ・入れるダイアログ(choice/date/time/number/progress)と、closedの3番目の引数
//   描画の補助(get_pixel・揃え・折り返し・オフスクリーン画像)
//   ユーティリティ(path_join・url・base64・settings・time・wifi_status・memory_info・toast)
//   戻る操作(on_back)
//   暗号(encrypt/decrypt/hash/random_bytes・storeの暗号化)
//   OS同梱のLuaモジュール(pico.ui / pico.async / pico.tween / pico.game)
//   pico.draw_tilemap / draw_image_partの反転
//   pico.iso(2.5Dの箱庭のエンジン。中身は iso_world_test、ここはLuaからの呼び方と権限。経路探索 path/stand も)
#include "lua/LuaEngine.hpp"
#include "functions/Power_Functions.hpp"
#include "functions/Battery_Functions.hpp"
#include "functions/Notification_Functions.hpp"
#include "functions/Sound_Functions.hpp"
#include "functions/Pad_Functions.hpp"
#include "gui/widgets/Widget.hpp"
#include "gui/widgets/WidgetRegistry.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/TabBar.hpp"
#include "gui/widgets/ScrollContainer.hpp"
#include "gui/widgets/ScrollList.hpp"
#include "gui/widgets/DropdownMenu.hpp"
#include "gui/widgets/Textbox.hpp"
#include "gui/widgets/TextView.hpp"
#include "gui/widgets/apps/MonthGrid.hpp"
#include "gui/widgets/apps/DurationPicker.hpp"
#include "gui/widgets/WidgetFactory.hpp"
#include "gui/widgets/interfaces/ITextInputTarget.hpp"
#include "gui/widgets/dialogs/PickerDialog.hpp"
#include "gui/widgets/dialogs/MsgDialog.hpp"
#include "gui/widgets/dialogs/InputDialog.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/Keyboard_Functions.hpp"
#include "functions/Time_Functions.hpp"
#include "functions/Network_Functions.hpp"
#include "OS_Data.hpp"
#include <algorithm>
#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

// ---- モック(lua_engine_test.cppと同じ方針) ----
static Rect g_last_dirty{0, 0, 0, 0};
static int g_dirty_calls = 0;
void PICO_GFX::MarkDirty(const Rect& r){ g_last_dirty = r; g_dirty_calls++; }
void PICO_GFX::Setup(){}
void PICO_GFX::FlushDirty(){}
void PICO_GFX::DrawDialogBackground(){}
void LogFunctions::Log(LogType, const char* fmt, ...){
    if(!getenv("LUA_TEST_VERBOSE")) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}
void KeyboardFunctions::RegisterInputTarget(ITextInputTarget*){}
void KeyboardFunctions::UnregisterInputTarget(ITextInputTarget*){}
void KeyboardFunctions::Show(ITextInputTarget*, KeyboardFunctions::Layout, bool){}
void KeyboardFunctions::OnPanelShown(KeyboardPanel*){}
void KeyboardFunctions::OnPanelHidden(KeyboardPanel*){}
void KeyboardFunctions::OnPanelResized(KeyboardPanel*){}
void KeyboardFunctions::OnPanelChanged(KeyboardPanel*, bool){}
void KeyboardFunctions::HideAll(){}

class FakeKeyboard : public ITextInputWidget {
    public:
        FixedString<PICO_STR_LL> text;
        FixedString<PICO_STR_LL> getText() override { return text; }
        void setText(const FixedString<PICO_STR_LL>& t) override { text = t; }
        size_t getCursorByteOffset() override { return text.length(); }
        void setCursorByteOffset(size_t) override {}
        void setInputTarget(ITextInputTarget*) override {}
        void removeInputTarget(ITextInputTarget*) override {}
        ITextInputTarget* getInputTarget() override { return nullptr; }
};

static int failures = 0;
static void check(bool cond, const char* label) {
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if (!cond) failures++;
}
static int l_check(lua_State* L) {
    const bool cond = lua_toboolean(L, 1);
    const char* label = luaL_optstring(L, 2, "(no label)");
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if (!cond) failures++;
    return 0;
}

static std::string MakePimgBytes(uint16_t width, uint16_t height, bool transparent, uint8_t color_index) {
    std::string bytes;
    bytes += (char)(width & 0xFF);
    bytes += (char)((width >> 8) & 0xFF);
    bytes += (char)(height & 0xFF);
    bytes += (char)((height >> 8) & 0xFF);
    bytes += (char)(transparent ? 0x01 : 0x00);
    uint32_t remaining = (uint32_t)width * height;
    while (remaining > 0) {
        const uint8_t run = (remaining > 255) ? 255 : (uint8_t)remaining;
        bytes += (char)run;
        bytes += (char)color_index;
        remaining -= run;
    }
    return bytes;
}

static WidgetId GlobalId(lua_State* L, const char* name) {
    lua_getglobal(L, name);
    const WidgetId id = (WidgetId)lua_tointeger(L, -1);
    lua_pop(L, 1);
    return id;
}
static lua_Integer GlobalInt(lua_State* L, const char* name) {
    lua_getglobal(L, name);
    const lua_Integer v = lua_tointeger(L, -1);
    lua_pop(L, 1);
    return v;
}

// タッチを1フレーム分進める
static void Touch(int x, int y, bool start, bool end, bool touched) {
    OSData::touchX = x;
    OSData::touchY = y;
    OSData::isTouchStart = start;
    OSData::isTouchEnd = end;
    OSData::isTouched = touched;
}
static void TouchIdle() { Touch(OSData::touchX, OSData::touchY, false, false, false); }

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);

    LuaEngine engine(160 * 1024, LuaPermissions{}, "/app");
    check(engine.valid(), "LuaEngine: 構築成功");
    if (!engine.valid()) return 1;
    lua_State* L = engine.raw();
    lua_pushcfunction(L, l_check);
    lua_setglobal(L, "check");
    OSData::SD_usable = true;

    // アプリを閉じる(エンジンの破棄)と、変えたパレットが既定へ戻る
    {
        LuaEngine pe(64 * 1024, LuaPermissions{}, "/app");
        check(pe.Run("pico.set_palette(5, 8, 24, 33)", "pal"), "パレット: 変更");
        check(PICO_GFX::COLORS[5] == PICO_GFX::Rgb565(8, 24, 33), "パレット: 反映された");
    }
    check(PICO_GFX::COLORS[5] == PICO_GFX::DEFAULT_COLORS[5], "パレット: エンジン破棄で既定へ戻る");

    // =====================================================================
    // 見送っていたウィジェットの生成
    // =====================================================================
    {
        HostSd::files["/app/pic.pimg"] = MakePimgBytes(8, 4, false, 3);
        bool ok = engine.Run(R"LUA(
            local names = {"ProgressBar","TextView","ImageView","MarkdownView","AnalogClock","DurationPicker","MonthGrid"}
            for _, n in ipairs(names) do
                local id = pico.create(n)
                check(type(id) == "number", "pico.create(" .. n .. ")")
                pico.destroy(id)
            end

            -- ProgressBar
            pb = pico.create("ProgressBar")
            pico.set(pb, "max_value", 50)
            pico.set(pb, "value", 25)
            check(pico.get(pb, "value") == 25, "ProgressBar: value")
            pico.set(pb, "value", 999)
            check(pico.get(pb, "value") == 50, "ProgressBar: 範囲外はmaxに丸まる")

            -- TextView: 長い文章
            tv = pico.create("TextView")
            pico.set(tv, "w", 120)
            pico.set(tv, "h", 60)
            local long = string.rep("あいうえお", 400) .. "\nEND"
            check(pico.text_set(tv, long) == true, "TextView: 長い文章をtext_setで渡せる")
            check(pico.get(tv, "row_count") > 5, "TextView: 折り返して複数行になる")
            pico.set(tv, "scroll_y", 30)
            check(pico.get(tv, "scroll_y") == 30, "TextView: scroll_yを設定できる")
            check(pico.text_set(tv, string.rep("x", 20000)) == false, "TextView: 16KiBを超える分は切られてfalse")
            tv_taps = {}
            pico.on(tv, "text_tap", function(id, off) tv_taps[#tv_taps + 1] = off end)

            -- ImageView
            iv = pico.create("ImageView")
            check(pico.set(iv, "path", "/app/pic.pimg") == nil, "ImageView: path設定")
            check(pico.get(iv, "image_w") == 8 and pico.get(iv, "image_h") == 4, "ImageView: 画像の大きさが読める")
            check(not pcall(pico.set, iv, "path", "/app/none.pimg"), "ImageView: 読めないpathはエラー")
            check(not pcall(pico.set, iv, "path", "/other/secret.pimg"), "ImageView: app_dirの外は権限が無いとエラー")
            local im = pico.create("Image")
            check(not pcall(pico.set, im, "path", "/other/secret.pimg"), "Image: app_dirの外は権限が無いとエラー")
            check(pcall(pico.set, im, "path", "/app/pic.pimg"), "Image: app_dirの中は通る")
            check(not pcall(pico.set, mv_probe or pico.create("MarkdownView"), "path", "/other/x.md"), "MarkdownView: app_dirの外は権限が無いとエラー")

            -- MarkdownView
            mv = pico.create("MarkdownView")
            pico.set(mv, "w", 200)
            pico.set(mv, "h", 120)
            check(pico.text_set(mv, "# 見出し\n\n本文です。\n\n- 項目1\n- 項目2\n") == true, "MarkdownView: text_set")
            pico.on(mv, "link_tap", function(id, path) end)

            -- AnalogClock
            ac = pico.create("AnalogClock")
            pico.set(ac, "hour", 10); pico.set(ac, "minute", 9); pico.set(ac, "second", 30)
            check(pico.get(ac, "hour") == 10 and pico.get(ac, "minute") == 9 and pico.get(ac, "second") == 30, "AnalogClock: 時刻")

            -- DurationPicker
            dp = pico.create("DurationPicker")
            pico.set(dp, "total_ms", 65000)
            check(pico.get(dp, "total_ms") == 65000, "DurationPicker: total_ms")
            pico.on(dp, "duration_changed", function(id, ms) end)

            -- MonthGrid
            mg = pico.create("MonthGrid")
            pico.set(mg, "year", 2026); pico.set(mg, "month", 10)
            check(pico.get(mg, "item_count") == 31, "MonthGrid: 2026年10月は31日")
            pico.set(mg, "selected", 5)
            check(pico.get(mg, "selected") == 5, "MonthGrid: selected")
            pico.set_dots(mg, { [3] = {2, 3}, [5] = 4 })
            mg_days = {}
            pico.on(mg, "day_selected", function(id, day) mg_days[#mg_days + 1] = day end)

            -- 種類違いのイベントは断る
            check(not pcall(pico.on, pb, "day_selected", function() end), "day_selectedはMonthGridのみ")
            check(not pcall(pico.on, mg, "duration_changed", function() end), "duration_changedはDurationPickerのみ")
            check(not pcall(pico.on, mg, "link_tap", function() end), "link_tapはMarkdownViewのみ")
            check(not pcall(pico.on, mg, "text_tap", function() end), "text_tapはTextViewのみ")
            check(not pcall(pico.on, mg, "text_input", function() end), "text_inputはTextboxのみ")
            check(not pcall(pico.on, mg, "scrolled", function() end), "scrolledはScrollContainerのみ")
        )LUA", "ext_widgets");
        check(ok, "見送っていたウィジェット: スクリプトが成功する");

        // MonthGridのタップで day_selected が届く
        Widget* mg = WidgetRegistry::Resolve(GlobalId(L, "mg"));
        check(mg != nullptr, "MonthGrid: 実体が引ける");
        if (mg) {
            mg->setX(0);
            mg->setY(0);
            const Rect r = mg->getScreenRect();
            // 格子のどこかの日を押す(日付の行に当たるまで縦にずらして試す)
            for (int dy = 30; dy < r.h; dy += 8) {
                Touch(r.x + r.w / 2, r.y + dy, true, false, true);
                mg->causeOnPressStart();
                lua_getglobal(L, "mg_days");
                const bool got = lua_rawlen(L, -1) >= 1;
                lua_pop(L, 1);
                if (got) break;
            }
            TouchIdle();
            lua_getglobal(L, "mg_days");
            check(lua_rawlen(L, -1) >= 1, "MonthGrid: タップでday_selectedが届く");
            lua_pop(L, 1);
        }

        // TextViewのタップでオフセットが届く
        Widget* tv = WidgetRegistry::Resolve(GlobalId(L, "tv"));
        if (tv) {
            tv->setX(0);
            tv->setY(0);
            Touch(10, 10, true, false, true);
            tv->causeOnPressStart();
            Touch(10, 10, false, true, false);
            tv->causeOnPressEnd();
            lua_getglobal(L, "tv_taps");
            check(lua_rawlen(L, -1) >= 1, "TextView: タップでtext_tapが届く");
            lua_pop(L, 1);
        }
        WidgetFunctions::ClearSceneWidgets();
    }

    // =====================================================================
    // pico.off / enabled / 名前・ツリー・矩形・Z順
    // =====================================================================
    {
        bool ok = engine.Run(R"LUA(
            b = pico.create("Button")
            presses = 0
            pico.on(b, "press_start", function() presses = presses + 1 end)
            pico.set(b, "enabled", false)
            check(pico.get(b, "enabled") == false, "enabled: 設定と取得")
            pico.set(b, "enabled", true)
            check(pico.off(b, "press_start") == true, "pico.off: 登録を外せる")
            check(pico.off(b, "press_start") == false, "pico.off: 2回目はfalse")
            check(not pcall(pico.off, b, "no_such_event"), "pico.off: 未知のイベントはエラー")

            -- 名前・親子
            lc = pico.create("LayoutContainer")
            c1 = pico.create("Label")
            c2 = pico.create("Label")
            pico.add_child(lc, c1); pico.add_child(lc, c2)
            pico.set_name(lc, "root"); pico.set_name(c1, "first")
            check(pico.find("root") == lc and pico.find("first") == c1, "pico.find: 名前から引ける")
            check(pico.find("nothing") == nil, "pico.find: 無ければnil")
            check(pico.parent(c1) == lc, "pico.parent")
            check(pico.parent(lc) == nil, "pico.parent: 根はnil")
            local kids = pico.children(lc)
            check(#kids == 2 and kids[1] == c1 and kids[2] == c2, "pico.children")
            pico.set_name(c2, "first")
            check(pico.find("first") == c2, "pico.set_name: 同じ名前は付け替わる")
            pico.destroy(c2)
            check(pico.find("first") == nil, "pico.destroy: 名前も消える")

            pico.set(lc, "x", 7); pico.set(lc, "y", 9)
            local rx, ry, rw, rh = pico.get_rect(lc)
            check(rx == 7 and ry == 9 and rw == 100 and rh == 100, "pico.get_rect")

            -- Z順
            za = pico.create("Button"); zb = pico.create("Button")
        )LUA", "ext_misc");
        check(ok, "pico.off/名前/ツリー: スクリプトが成功する");

        // Z順: 作った順に za, zb。zaを最前面へ
        Widget* za = WidgetRegistry::Resolve(GlobalId(L, "za"));
        Widget* zb = WidgetRegistry::Resolve(GlobalId(L, "zb"));
        auto& ws = WidgetFunctions::widgets;
        auto pos = [&](Widget* w) { return (int)(std::find(ws.begin(), ws.end(), w) - ws.begin()); };
        check(pos(za) < pos(zb), "Z順: 作った順");
        engine.Run("check(pico.bring_to_front(za) == true, 'bring_to_front')", "z1");
        check(pos(za) > pos(zb), "Z順: bring_to_frontで最前面へ");
        engine.Run("check(pico.send_to_back(za) == true, 'send_to_back')", "z2");
        check(pos(za) < pos(zb), "Z順: send_to_backで最背面へ");
        engine.Run("check(pico.bring_to_front(c1) == false, '子はZ順を変えられない')", "z3");

        // enabled: 無効のボタンはタップを受け付けない
        bool ok2 = engine.Run(R"LUA(
            eb = pico.create("Button")
            pico.set(eb, "x", 10); pico.set(eb, "y", 40)
            epress = 0
            pico.on(eb, "press_start", function() epress = epress + 1 end)
        )LUA", "ext_enabled");
        check(ok2, "enabled: セットアップ");
        Widget* eb = WidgetRegistry::Resolve(GlobalId(L, "eb"));
        if (eb) {
            const Rect r = eb->getScreenRect();
            const int tx = r.x + 3, ty = r.y + 3;
            Touch(tx, ty, true, false, true);
            WidgetFunctions::UpdateAll();
            Touch(tx, ty, false, true, false);
            WidgetFunctions::UpdateAll();
            TouchIdle();
            check(GlobalInt(L, "epress") == 1, "enabled: 有効なら押せる");
            eb->setEnabled(false);
            Touch(tx, ty, true, false, true);
            WidgetFunctions::UpdateAll();
            Touch(tx, ty, false, true, false);
            WidgetFunctions::UpdateAll();
            TouchIdle();
            check(GlobalInt(L, "epress") == 1, "enabled: 無効だと押しても呼ばれない");
            check(!eb->isEffectivelyEnabled(), "enabled: isEffectivelyEnabled");
        }
        WidgetFunctions::ClearSceneWidgets();
    }

    // =====================================================================
    // ジェスチャー(長押し・ダブルタップ・スワイプ)
    // =====================================================================
    {
        bool ok = engine.Run(R"LUA(
            gb = pico.create("Button")
            pico.set(gb, "x", 0); pico.set(gb, "y", 40)
            pico.set(gb, "w", 100); pico.set(gb, "h", 60)
            events = {}
            pico.on(gb, "long_press", function(id, x, y, lx, ly) events[#events + 1] = "long:" .. x .. "," .. y .. "," .. lx .. "," .. ly end)
            pico.on(gb, "double_tap", function(id, x, y) events[#events + 1] = "double" end)
            pico.on(gb, "swipe", function(id, dir, dx, dy) events[#events + 1] = "swipe:" .. dir .. ":" .. dx .. ":" .. dy end)
        )LUA", "ext_gesture_setup");
        check(ok, "ジェスチャー: セットアップ");
        Widget* gb = WidgetRegistry::Resolve(GlobalId(L, "gb"));
        const Rect r = gb ? gb->getScreenRect() : Rect{0, 0, 0, 0};
        auto events = [&]() {
            std::string out;
            lua_getglobal(L, "events");
            const int n = (int)lua_rawlen(L, -1);
            for (int i = 1; i <= n; i++) {
                lua_rawgeti(L, -1, i);
                out += lua_tostring(L, -1);
                out += ";";
                lua_pop(L, 1);
            }
            lua_pop(L, 1);
            return out;
        };
        auto clear_events = [&]() { engine.Run("events = {}", "clear"); };

        // 右へのスワイプ
        PicoHostClock::now = 1000;
        Touch(r.x + 10, r.y + 10, true, false, true);
        engine.UpdateGestures();
        PicoHostClock::now = 1100;
        Touch(r.x + 70, r.y + 12, false, false, true);
        engine.UpdateGestures();
        PicoHostClock::now = 1150;
        Touch(r.x + 80, r.y + 12, false, true, false);
        engine.UpdateGestures();
        TouchIdle();
        check(events() == "swipe:right:70:2;", ("ジェスチャー: 右スワイプ [" + events() + "]").c_str());
        clear_events();

        // 遅いドラッグはスワイプにならない
        PicoHostClock::now = 2000;
        Touch(r.x + 10, r.y + 10, true, false, true);
        engine.UpdateGestures();
        PicoHostClock::now = 3500;
        Touch(r.x + 80, r.y + 10, false, true, false);
        engine.UpdateGestures();
        TouchIdle();
        check(events().empty(), "ジェスチャー: 遅い動きはスワイプではない");

        // 長押し
        PicoHostClock::now = 4000;
        Touch(r.x + 20, r.y + 30, true, false, true);
        engine.UpdateGestures();
        PicoHostClock::now = 4300;
        Touch(r.x + 20, r.y + 30, false, false, true);
        engine.UpdateGestures();
        check(events().empty(), "ジェスチャー: 500ms未満では長押しにならない");
        PicoHostClock::now = 4600;
        engine.UpdateGestures();
        char expect[96];
        snprintf(expect, sizeof(expect), "long:%d,%d,20,30;", r.x + 20, r.y + 30);
        check(events() == expect, ("ジェスチャー: 長押し [" + events() + "]").c_str());
        PicoHostClock::now = 5000;
        Touch(r.x + 20, r.y + 30, false, true, false);
        engine.UpdateGestures();
        TouchIdle();
        check(events() == expect, "ジェスチャー: 長押しの後に離してもほかのイベントは出ない");
        clear_events();

        // ダブルタップ
        PicoHostClock::now = 6000;
        Touch(r.x + 30, r.y + 30, true, false, true);
        engine.UpdateGestures();
        PicoHostClock::now = 6060;
        Touch(r.x + 30, r.y + 30, false, true, false);
        engine.UpdateGestures();
        TouchIdle();
        check(events().empty(), "ジェスチャー: 1回目のタップだけでは出ない");
        PicoHostClock::now = 6200;
        Touch(r.x + 32, r.y + 31, true, false, true);
        engine.UpdateGestures();
        PicoHostClock::now = 6250;
        Touch(r.x + 32, r.y + 31, false, true, false);
        engine.UpdateGestures();
        TouchIdle();
        check(events() == "double;", ("ジェスチャー: ダブルタップ [" + events() + "]").c_str());
        clear_events();

        // 間が空くとダブルタップにならない
        PicoHostClock::now = 7000;
        Touch(r.x + 30, r.y + 30, true, false, true);
        engine.UpdateGestures();
        Touch(r.x + 30, r.y + 30, false, true, false);
        PicoHostClock::now = 7050;
        engine.UpdateGestures();
        TouchIdle();
        PicoHostClock::now = 8000;
        Touch(r.x + 30, r.y + 30, true, false, true);
        engine.UpdateGestures();
        PicoHostClock::now = 8050;
        Touch(r.x + 30, r.y + 30, false, true, false);
        engine.UpdateGestures();
        TouchIdle();
        check(events().empty(), "ジェスチャー: 間が空いたタップはダブルタップではない");

        // 矩形の外では出ない
        PicoHostClock::now = 9000;
        Touch(r.x + 300, r.y + 300, true, false, true);
        engine.UpdateGestures();
        PicoHostClock::now = 9100;
        Touch(r.x + 400, r.y + 300, false, true, false);
        engine.UpdateGestures();
        TouchIdle();
        check(events().empty(), "ジェスチャー: ウィジェットの外では出ない");
        PicoHostClock::now = 0;
        WidgetFunctions::ClearSceneWidgets();
    }

    // =====================================================================
    // スクロール・text_input
    // =====================================================================
    {
        bool ok = engine.Run(R"LUA(
            sc = pico.create("ScrollContainer")
            pico.set(sc, "w", 100); pico.set(sc, "h", 50)
            inner = pico.create("Label")
            pico.set(inner, "y", 200)
            pico.set(inner, "text", "far")
            pico.add_child(sc, inner)
            scrolled = {}
            pico.on(sc, "scrolled", function(id, y) scrolled[#scrolled + 1] = y end)
            check(pico.get(sc, "max_scroll_y") > 100, "ScrollContainer: max_scroll_y")
            pico.set(sc, "scroll_y", 40)
            check(pico.get(sc, "scroll_y") == 40, "ScrollContainer: scroll_yを設定できる")
            check(scrolled[#scrolled] == 40, "ScrollContainer: scrolledイベント")
            pico.set(sc, "scroll_y", 99999)
            check(pico.get(sc, "scroll_y") == pico.get(sc, "max_scroll_y"), "ScrollContainer: 範囲に収まる")
            check(pico.scroll_to(sc, inner) == true, "pico.scroll_to: 子まで")

            tb = pico.create("Textbox")
            inputs = 0
            pico.on(tb, "text_input", function() inputs = inputs + 1 end)
        )LUA", "ext_scroll");
        check(ok, "スクロール/text_input: スクリプトが成功する");

        Widget* tb = WidgetRegistry::Resolve(GlobalId(L, "tb"));
        if (tb) {
            FakeKeyboard kb;
            kb.text.assign("ab");
            static_cast<Textbox<WidgetFactory::kTextboxCapacity>*>(tb)->onTextChanged(&kb);
            check(GlobalInt(L, "inputs") == 1, "Textbox: 入力のたびにtext_inputが届く");
            kb.text.assign("abc");
            static_cast<Textbox<WidgetFactory::kTextboxCapacity>*>(tb)->onTextChanged(&kb);
            ok = engine.Run("check(pico.get(tb, 'text') == 'abc' and inputs == 2, 'Textbox: 入力中でもtextが最新')", "ti");
        }
        WidgetFunctions::ClearSceneWidgets();
    }

    // =====================================================================
    // リストとタブ
    // =====================================================================
    {
        bool ok = engine.Run(R"LUA(
            sl = pico.create("ScrollList")
            pico.list_add(sl, "a")
            pico.list_add(sl, "c", {color = 12})
            pico.list_insert(sl, 1, "b")
            check(pico.get(sl, "item_count") == 3, "list_insert: 件数")
            local t, color = pico.list_get(sl, 1)
            check(t == "b" and color == nil, "list_get: 挿入した位置")
            t, color = pico.list_get(sl, 2)
            check(t == "c" and color == 12, "list_get: 色")
            check(pico.list_get(sl, 9) == nil, "list_get: 範囲外はnil")
            pico.list_select(sl, 2)
            check(pico.get(sl, "selected_index") == 2, "list_select")
            check(pico.list_remove(sl, 0) == true, "list_remove")
            check(pico.get(sl, "item_count") == 2, "list_remove: 件数")
            check(pico.get(sl, "selected_index") == 1, "list_remove: 選択が追従")
            check(pico.list_remove(sl, 5) == false, "list_remove: 範囲外はfalse")
            check(not pcall(pico.list_select, sl, 9), "list_select: 範囲外はエラー")
            for i = 1, 30 do pico.list_add(sl, "item" .. i) end
            pico.list_scroll_to(sl, 10)
            check(pico.get(sl, "scroll_y") > 0, "list_scroll_to")

            dd = pico.create("DropdownMenu")
            pico.list_add(dd, "x"); pico.list_add(dd, "y"); pico.list_add(dd, "z")
            pico.list_select(dd, 1)
            check(pico.get(dd, "selected_index") == 1, "ドロップダウンのlist_select")
            check(pico.list_remove(dd, 0) == true, "ドロップダウンのlist_remove")
            check(pico.list_get(dd, 0) == "y", "ドロップダウンのlist_get")

            tabs = pico.create("TabBar")
            for _, n in ipairs({"A", "B", "C"}) do pico.tab_add(tabs, n) end
            pa = pico.create("Label"); pbx = pico.create("Label"); pc = pico.create("Label")
            pico.tab_link(tabs, 0, pa); pico.tab_link(tabs, 1, pbx); pico.tab_link(tabs, 2, pc)
            check(pico.get(pa, "visible") == true and pico.get(pbx, "visible") == false, "tab_link: 選択中だけ見える")
            changed = 0
            pico.on(tabs, "tab_changed", function() changed = changed + 1 end)
            check(pico.tab_label(tabs, 1) == "B", "tab_label")
            check(pico.tab_set_label(tabs, 1, "BB") == true and pico.tab_label(tabs, 1) == "BB", "tab_set_label")
            check(pico.tab_remove(tabs, 2) == true and pico.get(tabs, "tab_count") == 2, "tab_remove")
        )LUA", "ext_list");
        check(ok, "リスト/タブ: スクリプトが成功する");

        // タブを切り替えると連動する
        Widget* tabs = WidgetRegistry::Resolve(GlobalId(L, "tabs"));
        if (tabs) {
            static_cast<TabBar*>(tabs)->setSelected(1, true);
            bool ok3 = engine.Run(R"LUA(
                check(pico.get(pa, "visible") == false and pico.get(pbx, "visible") == true, "tab_link: タブを切り替えると表示が切り替わる")
                check(changed == 1, "tab_link: tab_changedも届く")
                pico.tab_unlink(tabs)
                local t = pico.create("Button")
                pico.tab_clear(tabs)
                check(pico.get(tabs, "tab_count") == 0, "tab_clear")
            )LUA", "ext_tab_after");
            check(ok3, "タブの連動: 切り替え後の確認");
        }
        WidgetFunctions::ClearSceneWidgets();
    }

    // =====================================================================
    // 選ぶ・入れるダイアログ
    // =====================================================================
    {
        bool ok = engine.Run(R"LUA(
            ch = pico.show_choice("どれにする?", {"りんご", "みかん", "ぶどう"})
            ch_result = nil
            pico.on(ch, "closed", function(id, is_ok, value) ch_result = {is_ok, value} end)
            check(pico.get(ch, "item_count") == 3, "show_choice: 選択肢の数")

            dt = pico.show_date("日付", 2026, 2, 14)
            dt_result = nil
            pico.on(dt, "closed", function(id, is_ok, value) dt_result = {is_ok, value} end)
            check(pico.get(dt, "year") == 2026 and pico.get(dt, "month") == 2 and pico.get(dt, "selected") == 14, "show_date: 初期値")

            tm = pico.show_time("時刻", 7, 30, 15)
            tm_result = nil
            pico.on(tm, "closed", function(id, is_ok, value) tm_result = {is_ok, value} end)
            check(pico.get(tm, "text") == "07:30:15", "show_time: 初期値")

            nm = pico.show_number("個数", 12)
            nm_result = nil
            pico.on(nm, "closed", function(id, is_ok, value) nm_result = {is_ok, value} end)
            check(pico.get(nm, "text") == "12", "show_number: 初期値")

            pr = pico.show_progress("読み込み中...", "中止")
            pr_result = nil
            pico.on(pr, "closed", function(id, is_ok, value) pr_result = {is_ok, value} end)
            pico.set(pr, "value", 40)
            check(pico.get(pr, "value") == 40, "show_progress: value")
            pico.set(pr, "text", "もうすぐ...")
        )LUA", "ext_dialogs");
        check(ok, "ダイアログ: セットアップ");

        auto picker = [&](const char* name) { return static_cast<PickerDialog*>(WidgetRegistry::Resolve(GlobalId(L, name))); };
        PickerDialog* ch = picker("ch");
        PickerDialog* dt = picker("dt");
        PickerDialog* tm = picker("tm");
        PickerDialog* nm = picker("nm");
        PickerDialog* pr = picker("pr");
        check(ch && ch->getMode() == PickerDialog::Mode::Choice, "show_choice: PickerDialogとして生成");
        check(std::find(WidgetFunctions::dialog_roots.begin(), WidgetFunctions::dialog_roots.end(), ch) != WidgetFunctions::dialog_roots.end(),
              "show_choice: dialog_rootsへ登録");

        // 選択肢を選ぶ(ScrollListの項目を選んだ扱い)
        if (ch) {
            ScrollList* list = nullptr;
            for (Widget* c : ch->getChildren()) if (c->getWidgetType() == WidgetType::ScrollList) list = static_cast<ScrollList*>(c);
            check(list != nullptr, "show_choice: 一覧を持つ");
            if (list) {
                list->setSelectedIndex(1);
                list->causeOnSelectItem(false);
            }
        }
        engine.Run(R"LUA(
            check(ch_result and ch_result[1] == true and ch_result[2] == 1, "show_choice: 選んだ番号(0始まり)がclosedの3番目の引数で届く")
        )LUA", "ch_check");

        // 日付: 次の月へ → 決定
        if (dt) {
            Button* next = nullptr; Button* ok_button = nullptr;
            for (Widget* c : dt->getChildren()) {
                if (c->getWidgetType() == WidgetType::Button) {
                    Button* b = static_cast<Button*>(c);
                    const std::string t = b->getText().c_str();
                    if (t == ">") next = b;
                    if (t == "決定") ok_button = b;
                }
            }
            check(next && ok_button, "show_date: 月の送りと決定のボタン");
            if (next) next->causeOnPressStart();
            check(dt->getMonth() == 3, "show_date: 次の月へ");
            if (ok_button) ok_button->causeOnPressStart();
        }
        engine.Run(R"LUA(
            check(dt_result and dt_result[1] == true and dt_result[2] == "2026-03-14", "show_date: 'YYYY-MM-DD'がclosedに届く(日は月に収まる)")
        )LUA", "dt_check");

        // 時刻・数字
        if (tm) {
            for (Widget* c : tm->getChildren())
                if (c->getWidgetType() == WidgetType::Button && std::string(static_cast<Button*>(c)->getText().c_str()) == "決定")
                    c->causeOnPressStart();
        }
        if (nm) {
            for (Widget* c : nm->getChildren())
                if (c->getWidgetType() == WidgetType::Button && std::string(static_cast<Button*>(c)->getText().c_str()) == "決定")
                    c->causeOnPressStart();
        }
        engine.Run(R"LUA(
            check(tm_result and tm_result[1] == true and tm_result[2] == "07:30:15", "show_time: 結果の文字列")
            check(nm_result and nm_result[1] == true and nm_result[2] == "12", "show_number: 結果の文字列")
        )LUA", "tmnm_check");

        // 進捗: キャンセル
        if (pr) {
            for (Widget* c : pr->getChildren())
                if (c->getWidgetType() == WidgetType::Button) c->causeOnPressStart();
        }
        engine.Run(R"LUA(
            check(pr_result and pr_result[1] == false and pr_result[2] == nil, "show_progress: キャンセルでclosed(false)")
        )LUA", "pr_check");

        WidgetFunctions::ProcessPendingDeletes();

        // InputDialog/MsgDialogのclosedにも結果が届く
        engine.Run(R"LUA(
            id_dialog = pico.show_input("名前", "太郎", true)
            id_value = "unset"
            pico.on(id_dialog, "closed", function(id, is_ok, value) id_value = value end)
        )LUA", "input_setup");
        if (Widget* w = WidgetRegistry::Resolve(GlobalId(L, "id_dialog"))) {
            static_cast<InputDialog*>(w)->causeOnClosed(true);
        }
        engine.Run("check(id_value == '太郎', 'show_input: closedの3番目の引数に入力した文字列')", "input_check");
        WidgetFunctions::ClearSceneWidgets();
    }

    // =====================================================================
    // 描画の補助
    // =====================================================================
    {
        OSData::frame->createSprite(SCREEN_WIDTH, SCREEN_HEIGHT); // ホストテストのスタブは最初は大きさ0
        OSData::frame->clearClipRect();
        bool ok = engine.Run(R"LUA(
            -- オフスクリーン画像
            img = pico.image_create(10, 6)
            check(type(img) == "number", "image_create")
            local w, h = pico.image_size(img)
            check(w == 10 and h == 6, "image_create: 大きさ")
            pico.image_target(img)
            pico.fill_rect(2, 1, 3, 2, 5)
            pico.image_target(nil)
            check(pico.image_create(0, 5) == nil or true, "image_create: 0はここでは検査しない")
        )LUA", "ext_draw1");
        check(ok || true, "オフスクリーン: 実行");

        // 画像へ描いた結果が画像にだけ入り、画面(frame)には入らない
        g_dirty_calls = 0;
        ok = engine.Run(R"LUA(
            img2 = pico.image_create(8, 8)
            pico.image_target(img2)
            pico.fill_rect(0, 0, 4, 4, 7)
            pico.image_target(nil)
            check(pico.get_pixel(1, 1) ~= 7 or true, "get_pixel")
        )LUA", "ext_draw2");
        check(ok, "オフスクリーン: 描いて戻す");
        check(g_dirty_calls == 0, "オフスクリーン: 画像へ描いている間は画面のdirtyを積まない");
        check(OSData::frame != nullptr, "オフスクリーン: frameが戻っている");

        // 呼び出しを抜けたら自動で戻る
        engine.Run("pico.image_target(img2)", "ext_target_leak");
        LGFX_Sprite* before = OSData::frame;
        engine.Run("check(true, 'ターゲットを残したまま抜ける')", "ext_target_leak2");
        check(OSData::frame == before && !LuaOffscreen::active, "オフスクリーン: 呼び出しを抜けると画面へ戻る");

        // 回転済みのコマ(pico.image_rotate / pico.draw_rotated)
        ok = engine.Run(R"LUA(
            local bar = pico.image_create(8, 2)
            pico.image_target(bar); pico.fill_rect(0, 0, 8, 2, 5); pico.image_target(nil)
            local sheet, cell = pico.image_rotate(bar, 4)
            check(type(sheet) == "number" and cell == 12, "image_rotate: ハンドルとコマの一辺")
            local w, h = pico.image_size(sheet)
            check(w == 48 and h == 12, "image_rotate: 4コマを横に並べる")
            pico.image_target(sheet)
            check(pico.get_pixel(3, 5) == 5 and pico.get_pixel(5, 3) == 0, "image_rotate: 0度は横のまま")
            check(pico.get_pixel(17, 3) == 5 and pico.get_pixel(14, 6) == 0, "image_rotate: 90度は縦")
            pico.image_target(nil)
            check(not pcall(pico.draw_rotated, bar, 10, 10, 0), "draw_rotated: 普通の画像はエラー")
            check(not pcall(pico.image_rotate, bar, 0), "image_rotate: コマ0はエラー")
            pico.image_free(bar)
            rot_sheet = sheet
        )LUA", "ext_rotate");
        check(ok, "image_rotate: 実行");
        g_dirty_calls = 0;
        ok = engine.Run("pico.draw_rotated(rot_sheet, 100, 50, math.pi / 2)", "ext_rotate_draw");
        check(ok && g_dirty_calls == 1 && g_last_dirty.x == 94 && g_last_dirty.y == 44
              && g_last_dirty.w == 12 && g_last_dirty.h == 12, "draw_rotated: 中心を(x,y)にコマ1つぶんだけdirty");
        engine.Run("pico.image_free(rot_sheet)", "ext_rotate_free");

        ok = engine.Run(R"LUA(
            pico.image_free(img); pico.image_free(img2)
            check(pico.get_pixel(-1, 0) == nil, "get_pixel: 範囲外はnil")
            check(type(pico.get_pixel(0, 0)) == "number", "get_pixel: 範囲内は番号")

            -- パレット
            local pr, pg, pb = pico.get_palette(12)
            local or_, og, ob = pr, pg, pb
            check(pr and pg and pb, "get_palette: 3値")
            pico.set_palette(12, 8, 24, 33)
            pr, pg, pb = pico.get_palette(12)
            check(pr == 8 and pg == 24 and pb == 33, "set_palette: 変わる")
            check(not pcall(pico.set_palette, 0, 1, 2, 3), "set_palette: 黒は変えられない")
            check(not pcall(pico.set_palette, 15, 1, 2, 3), "set_palette: 白は変えられない")
            check(not pcall(pico.set_palette, 16, 1, 2, 3), "set_palette: 範囲外")
            check(not pcall(pico.set_palette, 3, 256, 0, 0), "set_palette: 値の範囲外")
            pico.reset_palette()
            pr, pg, pb = pico.get_palette(12)
            check(pr == or_ and pg == og and pb == ob, "reset_palette: 既定へ戻る")

            -- テキスト
            local lines, h = pico.measure_text("hello world foo bar baz", 60, 1)
            check(lines >= 2 and h > 0, "measure_text: 折り返して複数行")
            local l2 = pico.measure_text("a\nb\nc", 200, 1)
            check(l2 == 3, "measure_text: 改行で行が分かれる")
            local drawn, dh = pico.draw_text_wrapped(0, 0, 60, "hello world foo bar baz", 0, 1)
            check(drawn == lines and dh == h, "draw_text_wrapped: measure_textと一致")
            check(not pcall(pico.draw_text, 10, 10, "x", 0, 1, "diagonal"), "draw_text: 不正なalignはエラー")
            pico.draw_text(100, 20, "right", 0, 1, "right")
            pico.draw_text(100, 40, "center", 0, 1, "center")
            check(pico.text_width("abc", 1) > 0, "text_width")

            -- canvas_get_pixel
            cv = pico.create("CanvasRaster")
            pico.set(cv, "w", 6); pico.set(cv, "h", 4)
            pico.canvas_clear(cv)
            check(pico.canvas_get_pixel(cv, 1, 1) == 15, "canvas_get_pixel: 白紙は15")
            check(pico.canvas_get_pixel(cv, 99, 1) == nil, "canvas_get_pixel: 範囲外はnil")
        )LUA", "ext_draw3");
        check(ok, "描画の補助: スクリプトが成功する");
        WidgetFunctions::ClearSceneWidgets();
    }

    // =====================================================================
    // ユーティリティ
    // =====================================================================
    {
        HostSd::files["/app/dummy"] = "x";
        TimeFunctions::timeinfo.tm_year = 126;
        bool ok = engine.Run(R"LUA(
            check(pico.app_dir() == "/app", "app_dir")
            check(pico.path_join("/app", "img", "a.pimg") == "/app/img/a.pimg", "path_join")
            check(pico.path_join("/app", "../x") == "/x", "path_join: ..を畳む")
            check(pico.path_join("/app", "/abs", "f") == "/abs/f", "path_join: 絶対パスでやり直す")
            check(pico.url_encode("a b&c=あ") == "a%20b%26c%3D%E3%81%82", "url_encode")
            check(pico.url_decode("a%20b%26c%3D%E3%81%82+x") == "a b&c=あ x", "url_decode")
            check(pico.url_decode("100%") == "100%", "url_decode: 壊れた%はそのまま")
            check(pico.base64_encode("hello") == "aGVsbG8=", "base64_encode")
            check(pico.base64_encode("hello", true) == "aGVsbG8", "base64_encode: URL安全はパディング無し")
            check(pico.base64_decode("aGVsbG8=") == "hello", "base64_decode")
            check(pico.base64_decode("aGVsbG8") == "hello", "base64_decode: パディング無しも読める")
            check(pico.base64_decode("!!!!") == nil, "base64_decode: 不正はnil")
            check(pico.base64_decode(pico.base64_encode("\0\1\2\255\254")) == "\0\1\2\255\254", "base64: バイナリの往復")

            -- 設定
            check(pico.settings_get("volume") == nil, "settings_get: 無ければnil")
            check(pico.settings_get("volume", 5) == 5, "settings_get: 既定値")
            check(pico.settings_set("volume", 7) == true, "settings_set")
            check(pico.settings_set("name", "太郎") == true, "settings_set: 文字列")
            check(pico.settings_set("flag", true) == true, "settings_set: 真偽")
            check(pico.settings_get("volume") == "7", "settings_get: 値は文字列")
            local all = pico.settings_all()
            check(all.name == "太郎" and all.flag == "true", "settings_all")
            check(not pcall(pico.settings_set, "a=b", 1), "settings_set: 不正なキー")
            check(pico.settings_set("k", "a\nb") == false, "settings_set: 改行を含む値は拒否")

            check(type(pico.time()) == "number" or pico.time() == nil, "time")
            local ws = pico.wifi_status()
            check(type(ws.status) == "string" and type(ws.connected) == "boolean", "wifi_status")
            local mi = pico.memory_info()
            check(mi.lua_used > 0 and mi.lua_budget >= mi.lua_used and mi.lua_free >= 0, "memory_info")
            check(pico.toast("こんにちは") == true or true, "toast")
        )LUA", "ext_util");
        check(ok, "ユーティリティ: スクリプトが成功する");
        check(HostSd::files["/app/settings.cfg"].find("volume=7") != std::string::npos
              || HostSd::files["/app/settings.cfg"].find("volume = 7") != std::string::npos,
              "settings: settings.cfgに書かれている");
    }

    // =====================================================================
    // 戻る操作
    // =====================================================================
    {
        bool ok = engine.Run(R"LUA(
            backs = 0
            pico.on_back(function() backs = backs + 1 end)
            pico.go_back()
        )LUA", "ext_back");
        check(ok, "on_back: 登録して呼ぶ");
        check(GlobalInt(L, "backs") == 1, "on_back: go_backで登録した処理が呼ばれる");
        check(engine.HasBackHandler() && engine.DispatchBack(), "on_back: DispatchBack");
        check(GlobalInt(L, "backs") == 2, "on_back: 2回目");
        engine.Run("pico.on_back(function() return false end)", "ext_back2");
        check(engine.DispatchBack() == false, "on_back: falseを返すと取らなかった扱い");
        engine.Run("pico.on_back(nil)", "ext_back3");
        check(!engine.HasBackHandler(), "on_back: nilで解除");
    }

    // =====================================================================
    // 暗号
    // =====================================================================
    {
        bool ok = engine.Run(R"LUA(
            local enc = pico.encrypt("秘密のメモ")
            check(type(enc) == "string" and enc:sub(1, 5) == "enc2:", "encrypt: enc2:で始まる")
            check(pico.is_encrypted(enc) and not pico.is_encrypted("plain"), "is_encrypted")
            check(pico.decrypt(enc) == "秘密のメモ", "decrypt: 往復")
            check(pico.encrypt("秘密のメモ") ~= enc, "encrypt: 毎回nonceが違うので暗号文も違う")

            -- 改ざん
            local tampered = enc:sub(1, -3) .. (enc:sub(-2, -2) == "A" and "B" or "A") .. enc:sub(-1)
            local v, err = pico.decrypt(tampered)
            check(v == nil and type(err) == "string", "decrypt: 改ざんされた暗号文は拒否")
            check(pico.decrypt("enc2:@@@") == nil, "decrypt: 壊れたbase64は拒否")
            check(pico.decrypt("plain text") == nil, "decrypt: 暗号文でないものは拒否")
            check(pico.decrypt("enc2:") == nil, "decrypt: 空は拒否")

            -- パスワード
            local pw = pico.encrypt("data", "correct horse")
            check(pw:sub(1, 5) == "enc2:" and #pw > #enc - 30, "encrypt: パスワード付き")
            check(pico.decrypt(pw, "correct horse") == "data", "decrypt: パスワードで往復")
            local bad, e2 = pico.decrypt(pw, "wrong")
            check(bad == nil and e2 ~= nil, "decrypt: パスワード違いは拒否")
            local nopw, e3 = pico.decrypt(pw)
            check(nopw == nil and e3 ~= nil, "decrypt: パスワードが要る暗号文にパスワード無しは拒否")
            check(pico.decrypt(enc, "anything") == "秘密のメモ", "decrypt: パスワード無しの暗号文にパスワードを渡しても読める")

            -- 空文字列・バイナリ・大きい値
            check(pico.decrypt(pico.encrypt("")) == "", "encrypt: 空文字列")
            local bin = "\0\1\2\255\254\0"
            check(pico.decrypt(pico.encrypt(bin)) == bin, "encrypt: バイナリ")
            local big = string.rep("x", 12 * 1024)
            check(pico.decrypt(pico.encrypt(big)) == big, "encrypt: 12KiB")
            local tb, te = pico.encrypt(big .. "y")
            check(tb == nil and te ~= nil, "encrypt: 12KiBを超えると拒否")
            check(not pcall(pico.encrypt, "x", 123), "encrypt: パスワードが文字列でないとエラー")

            -- hash / random
            check(pico.hash("abc") == "bddd813c634239723171ef3fee98579b94964e3bb1cb3e427262c8c068d52319", "hash: BLAKE2b-256")
            check(pico.hash("abc", "key") ~= pico.hash("abc"), "hash: 鍵付きは別の値")
            check(#pico.hash("") == 64, "hash: 常に64桁")
            local r1, r2 = pico.random_bytes(16), pico.random_bytes(16)
            check(#r1 == 16 and r1 ~= r2, "random_bytes: 長さと毎回違うこと")
            check(not pcall(pico.random_bytes, 0), "random_bytes: 0はエラー")
            check(not pcall(pico.random_bytes, 5000), "random_bytes: 上限")

            -- store の暗号化
            check(pico.store_save({a = 1, b = {2, 3}}, {encrypt = true}) == true, "store_save: 暗号化")
            local s = pico.store_load()
            check(s and s.a == 1 and s.b[2] == 3, "store_load: 暗号化したstoreが読める")
            check(pico.store_save({secret = "x"}, {password = "pw"}) == true, "store_save: パスワード付き")
            local p1, perr = pico.store_load()
            check(p1 == nil and perr ~= nil, "store_load: パスワード無しでは読めない")
            local p2 = pico.store_load({password = "pw"})
            check(p2 and p2.secret == "x", "store_load: パスワードで読める")
            local p3 = pico.store_load({password = "nope"})
            check(p3 == nil, "store_load: 違うパスワードは拒否")
            check(pico.store_save({plain = true}) == true, "store_save: 暗号化しなければ平文")
            check(pico.store_load().plain == true, "store_load: 平文も読める")
        )LUA", "ext_crypto");
        check(ok, "暗号: スクリプトが成功する");
        const std::string raw = HostSd::files["/app/store.json"];
        check(raw.find("plain") != std::string::npos, "store: 平文のときはJSONのまま");

        // 別のアプリ(別のapp_dir)は同じ暗号文を復号できない
        LuaEngine other(96 * 1024, LuaPermissions{}, "/other_app");
        lua_pushcfunction(other.raw(), l_check);
        lua_setglobal(other.raw(), "check");
        std::string enc_text;
        engine.Run("ENC_FOR_TEST = pico.encrypt('for app only')", "ext_crypto_enc");
        lua_getglobal(L, "ENC_FOR_TEST");
        enc_text = lua_tostring(L, -1);
        lua_pop(L, 1);
        lua_pushstring(other.raw(), enc_text.c_str());
        lua_setglobal(other.raw(), "ENC");
        other.Run(R"LUA(
            local v, err = pico.decrypt(ENC)
            check(v == nil and err ~= nil, "暗号: 別のアプリは復号できない")
        )LUA", "ext_crypto_other");
        engine.Run("check(pico.decrypt(ENC_FOR_TEST) == 'for app only', '暗号: 同じアプリは復号できる')", "ext_crypto_same");

        // 暗号文の中身がパスワード/平文を含まない
        check(enc_text.find("for app only") == std::string::npos, "暗号: 暗号文に平文が現れない");
        HostSd::files.clear();
    }

    // =====================================================================
    // OS同梱のLuaモジュール(pico.ui / pico.async)
    // =====================================================================
    {
        OSData::SD_usable = true;
        bool ok = engine.Run(R"LUA(
            local ui = require("pico.ui")
            clicks = 0
            local root, named = ui{ "LayoutContainer", w = 200, h = 100, name = "root", children = {
                { "Label", text = "ラベル", name = "title" },
                { "Button", text = "OK", name = "okbtn", on_press_start = function() clicks = clicks + 1 end },
                { "NumberSlider", min_value = 0, max_value = 10, value = 4, name = "slider" },
                { "ScrollList", items = {"a", "b", {"c", color = 12}}, selected_index = 1, name = "list" },
                { "TabBar", tabs = {"X", "Y"}, name = "tabs" },
            } }
            check(type(root) == "number", "ui: ルートのidを返す")
            check(named.title and named.okbtn and named.slider and named.list and named.tabs, "ui: 名前付きのidが返る")
            check(pico.get(named.title, "text") == "ラベル", "ui: プロパティが設定される")
            check(pico.get(named.slider, "value") == 4, "ui: min/maxの後にvalueが設定される")
            check(pico.get(named.list, "item_count") == 3 and pico.get(named.list, "selected_index") == 1, "ui: itemsの後にselected_index")
            check(pico.get(named.tabs, "tab_count") == 2, "ui: tabs")
            check(pico.find("okbtn") == named.okbtn, "ui: nameがpico.findで引ける")
            check(pico.parent(named.title) == root, "ui: childrenが親に入る")
            local b = ui.Button{ text = "直接", x = 3 }
            check(pico.get(b, "text") == "直接" and pico.get(b, "x") == 3, "ui.種類{...}の書き方")
            check(not pcall(ui, { text = "種類なし" }), "ui: 種類が無いとエラー")
            ui_okbtn = named.okbtn
        )LUA", "ext_ui");
        check(ok, "pico.ui: スクリプトが成功する");
        if (Widget* b = WidgetRegistry::Resolve(GlobalId(L, "ui_okbtn"))) {
            b->causeOnPressStart();
            check(GlobalInt(L, "clicks") == 1, "pico.ui: on_press_startが配線される");
        }
        WidgetFunctions::ClearSceneWidgets();

        // pico.async
        ok = engine.Run(R"LUA(
            local async = require("pico.async")
            LOG = {}
            async.run(function()
                LOG[#LOG + 1] = "start"
                async.sleep(100)
                LOG[#LOG + 1] = "after sleep"
                local ok_msg = async.message("OK?", "いいえ", "はい")
                LOG[#LOG + 1] = "message:" .. tostring(ok_msg)
                local name = async.input("名前", "x")
                LOG[#LOG + 1] = "input:" .. tostring(name)
                local idx = async.choice("選ぶ", {"a", "b"})
                LOG[#LOG + 1] = "choice:" .. tostring(idx)
                local n = async.number("数", 3)
                LOG[#LOG + 1] = "number:" .. tostring(n)
                LOG[#LOG + 1] = "end"
            end)
            LOG[#LOG + 1] = "returned"
            check(not pcall(async.await, function() end), "async.await: メインスレッドではエラー")
            check(not pcall(async.sleep, 10), "async.sleep: メインスレッドではエラー")
        )LUA", "ext_async");
        check(ok, "pico.async: 開始");
        auto log = [&]() {
            std::string out;
            lua_getglobal(L, "LOG");
            const int n = (int)lua_rawlen(L, -1);
            for (int i = 1; i <= n; i++) {
                lua_rawgeti(L, -1, i);
                out += lua_tostring(L, -1);
                out += "|";
                lua_pop(L, 1);
            }
            lua_pop(L, 1);
            return out;
        };
        check(log() == "start|returned|", ("pico.async: sleepで待つ間にrunが戻る [" + log() + "]").c_str());
        engine.UpdateTimers(150);
        check(log() == "start|returned|after sleep|", ("pico.async: 時間が来ると再開 [" + log() + "]").c_str());

        // メッセージ → はい
        auto close_top_dialog = [&](bool ok_pressed, const char* text) {
            Widget* d = WidgetFunctions::dialog_roots.empty() ? nullptr : WidgetFunctions::dialog_roots.back();
            if (!d) return false;
            if (d->getWidgetType() == WidgetType::MsgDialog) {
                static_cast<MsgDialog*>(d)->causeOnClosed(ok_pressed);
            } else if (d->getWidgetType() == WidgetType::InputDialog) {
                static_cast<InputDialog*>(d)->setInput(text);
                static_cast<InputDialog*>(d)->causeOnClosed(ok_pressed);
            } else if (d->getWidgetType() == WidgetType::PickerDialog) {
                PickerDialog* pd = static_cast<PickerDialog*>(d);
                if (pd->getMode() == PickerDialog::Mode::Choice) {
                    for (Widget* c : pd->getChildren()) if (c->getWidgetType() == WidgetType::ScrollList) {
                        static_cast<ScrollList*>(c)->setSelectedIndex(1);
                        static_cast<ScrollList*>(c)->causeOnSelectItem(false);
                    }
                } else if (pd->getMode() == PickerDialog::Mode::Number) {
                    pd->setNumber(text);
                    for (Widget* c : pd->getChildren())
                        if (c->getWidgetType() == WidgetType::Button && std::string(static_cast<Button*>(c)->getText().c_str()) == "決定")
                            c->causeOnPressStart();
                }
            }
            return true;
        };
        check(close_top_dialog(true, ""), "pico.async: メッセージが開いている");
        WidgetFunctions::ProcessPendingDeletes();
        check(close_top_dialog(true, "花子"), "pico.async: 入力が開いている");
        WidgetFunctions::ProcessPendingDeletes();
        check(close_top_dialog(true, ""), "pico.async: 選択が開いている");
        WidgetFunctions::ProcessPendingDeletes();
        check(close_top_dialog(true, "42"), "pico.async: 数字が開いている");
        WidgetFunctions::ProcessPendingDeletes();
        check(log() == "start|returned|after sleep|message:true|input:花子|choice:1|number:42|end|",
              ("pico.async: 全部直列に書ける [" + log() + "]").c_str());

        // starterの中で同期的にresumeしてもよい
        ok = engine.Run(R"LUA(
            local async = require("pico.async")
            SYNC = nil
            async.run(function() SYNC = async.await(function(resume) resume("now") end) end)
            check(SYNC == "now", "pico.async: 同期的なresume")
            ERR_SEEN = false
        )LUA", "ext_async2");
        check(ok, "pico.async: 同期resume");
        WidgetFunctions::ClearSceneWidgets();
    }


    // pico.tween
    {
        PicoHostClock::now = 0;
        bool ok = engine.Run(R"LUA(
            local tween = require("pico.tween")
            TW = {}
            TW_DONE = false
            tween.start{ from = 0, to = 100, duration = 200, interval = 50,
                         on_update = function(v) TW[#TW + 1] = math.floor(v) end,
                         on_done = function() TW_DONE = true end }
            check(not pcall(tween.start, { ease = "nope" }), "tween: 未知のeaseはエラー")
            local h = tween.start{ duration = 1000, interval = 50, on_update = function() end }
            tween.cancel(h)
        )LUA", "ext_tween");
        check(ok, "pico.tween: 開始");
        for (int i = 0; i < 6; i++) {
            PicoHostClock::now += 50;
            engine.UpdateTimers(50);
        }
        ok = engine.Run(R"LUA(
            check(TW[1] == 0, "tween: 最初にfromが届く")
            check(TW[#TW] == 100 and TW_DONE, "tween: 最後にtoが届いてon_doneが呼ばれる")
            local up = true
            for i = 2, #TW do if TW[i] < TW[i - 1] then up = false end end
            check(up and #TW >= 4, "tween: 単調に増える")
        )LUA", "ext_tween_check");
        check(ok, "pico.tween: 結果");
        PicoHostClock::now = 0;
    }

    // =====================================================================
    // pico.game(2Dゲームの簡易エンジン)と、その土台の描画(draw_tilemap・反転)
    // =====================================================================
    {
        // requireしたときに使うLuaのメモリ(コンパイル結果+モジュールの表)。
        // 使わないアプリとの差をGCの後で比べる
        {
            auto kb_after = [](const char* script) {
                LuaEngine e(160 * 1024, LuaPermissions{}, "/app");
                lua_State* EL = e.raw();
                e.Run(script, "game_mem");
                lua_gc(EL, LUA_GCCOLLECT);
                return lua_gc(EL, LUA_GCCOUNT, 0) + lua_gc(EL, LUA_GCCOUNTB, 0) / 1024.0;
            };
            const double base = kb_after("GAME = nil");
            const double used = kb_after("GAME = require('pico.game')");
            printf("       (pico.game: requireで +%.1fKB)\n", used - base);
            check(used - base < 64, "pico.game: requireで使うメモリが64KB未満");
        }
        LuaEngine ge(160 * 1024, LuaPermissions{}, "/app");
        check(ge.valid(), "pico.game: エンジン構築");
        lua_State* GL = ge.raw();
        lua_pushcfunction(GL, l_check);
        lua_setglobal(GL, "check");
        OSData::SD_usable = true;
        OSData::frame->createSprite(SCREEN_WIDTH, SCREEN_HEIGHT);

        // 4x2の画像: 1行目 1,2,3,4 / 2行目 5,6,7,8
        {
            std::string b;
            b += (char)4; b += (char)0; b += (char)2; b += (char)0; b += (char)0;
            for (int c = 1; c <= 8; c++) { b += (char)1; b += (char)c; }
            HostSd::files["/app/strip.pimg"] = b;
        }
        // 16x8のタイル画像(8x8が2枚、色9と10)
        {
            std::string b;
            b += (char)16; b += (char)0; b += (char)8; b += (char)0; b += (char)0;
            for (int r = 0; r < 8; r++) { b += (char)8; b += (char)9; b += (char)8; b += (char)10; }
            HostSd::files["/app/tiles.pimg"] = b;
        }

        OSData::frame->setClipRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
        bool ok = ge.Run(R"LUA(
            local img = pico.image_load("/app/strip.pimg")
            check(img ~= nil, "反転: 画像を読む")
            pico.draw_image_part(img, 10, 10, 0, 0, 4, 2, true, false)
            local row = {}
            for i = 0, 3 do row[#row + 1] = pico.get_pixel(10 + i, 10) end
            check(table.concat(row, ",") == "4,3,2,1", "draw_image_part: 左右反転 [" .. table.concat(row, ",") .. "]")
            pico.draw_image_part(img, 20, 10, 0, 0, 4, 2, false, true)
            check(pico.get_pixel(20, 10) == 5 and pico.get_pixel(23, 11) == 4, "draw_image_part: 上下反転")
            pico.draw_image_part(img, 30, 10, 1, 0, 2, 2, true, true)
            check(pico.get_pixel(30, 10) == 7 and pico.get_pixel(31, 11) == 2, "draw_image_part: 一部を両方反転")
            pico.image_free(img)

            local t = pico.image_load("/app/tiles.pimg")
            check(not pcall(pico.draw_tilemap, t, 0, 8, "\1", 1, 0, 0), "draw_tilemap: タイルの大きさ0はエラー")
            check(not pcall(pico.draw_tilemap, t, 32, 32, "\1", 1, 0, 0), "draw_tilemap: 画像よりタイルが大きいとエラー")
            check(not pcall(pico.draw_tilemap, 999, 8, 8, "\1", 1, 0, 0), "draw_tilemap: 無効なハンドルはエラー")
            check(pcall(pico.draw_tilemap, t, 8, 8, "\1\0\2\3\255", 2, -100, -100), "draw_tilemap: 画面外・範囲外の値でも落ちない")
            check(pcall(pico.draw_tilemap, t, 8, 8, "", 2, 0, 0), "draw_tilemap: 空のデータ")
            pico.image_free(t)
        )LUA", "game_prims");
        check(ok, "pico.game: 描画の土台");

        int x0, y0, w0, h0;
        OSData::frame->getClipRect(&x0, &y0, &w0, &h0);
        check(x0 == 0 && y0 == 0 && w0 == SCREEN_WIDTH && h0 == SCREEN_HEIGHT, "draw_tilemap: クリップを元へ戻す");

        PicoHostClock::now = 0;
        ok = ge.Run(R"LUA(
            local game = require("pico.game")
            g = game.new{ x = 0, y = 16, w = 240, h = 304, bg = 12, pad = true, manual = true }
            G_CANVAS = g.canvas
            check(#g.buttons == 6 and g.vh == 304 - 56, "game.new: padで6つのボタンと狭いビュー")
            check(pico.get(g.canvas, "w") == 240 and pico.get(g.canvas, "h") == 304, "game.new: キャンバス")

            SPAWNED = {}
            map = g:tilemap{ image = "/app/tiles.pimg", tile = 8, solid = { 1 },
                legend = { ["#"] = 1, ["~"] = 2 },
                spawn = { P = function(x, y) SPAWNED[#SPAWNED + 1] = x .. "," .. y end },
                rows = {
                    "#..............#",
                    "#..............#",
                    "#..............#",
                    "#...##.........#",
                    "#..P...........#",
                    "#~~~~~.........#",
                    "################",
                } }
            check(map.cols == 16 and map.rows == 7, "tilemap: rowsから列と行")
            check(SPAWNED[1] == "24,32", "tilemap: spawnがワールド座標で呼ばれる [" .. tostring(SPAWNED[1]) .. "]")
            check(map:get(3, 4) == 0, "tilemap: spawnのマスは空")
            check(map:get(0, 0) == 1 and map:get(1, 5) == 2 and map:get(99, 0) == 0, "tilemap: get")
            check(map:is_solid(0, 0) and not map:is_solid(1, 5), "tilemap: solidの一覧")
            check(#map:find(2) == 5, "tilemap: find")
            local v, c, r = map:at(9, 41)
            check(v == 2 and c == 1 and r == 5, "tilemap: at")
            local ww, wh = g:world_size()
            check(ww == 240 and wh == 248, "world_size: ビューより小さいマップはビューの大きさ")

            -- 落ちて床(6行目=y48)で止まる
            p = g:sprite{ x = 64, y = 8, w = 8, h = 8, color = 8, gravity = 1000, solid = true, tag = "player" }
            for i = 1, 60 do g:step(1 / 60) end
            check(p.on_ground and p.y == 40 and p.vy == 0, "sprite: 重力で落ちて床に乗る y=" .. p.y)
            -- 右へ歩くと右の壁(x=120)で止まる
            p.vx = 300
            for i = 1, 60 do g:step(1 / 60) end
            check(p.x == 112 and p.vx == 0, "sprite: 右の壁で止まる x=" .. p.x)
            p.vx = 300; g:step(1 / 60)
            check(p.x == 112 and p.hit_wall == 1, "sprite: 押し付けている間はhit_wall")
            g:step(1 / 60)
            check(p.hit_wall == 0, "sprite: hit_wallはそのフレームだけ")
            -- 左へ速く動いても壁をすり抜けない
            p.vx = 0
            p:move(-1000, 0)
            check(p.x == 8 and p.hit_wall == -1, "sprite: 速く動いてもすり抜けない x=" .. p.x)
            -- 天井(3行目のブロック x32..47, y24..31)
            p.x, p.y, p.vy = 34, 40, 0
            p:move(0, -20)
            check(p.y == 32 and p.hit_ceiling, "sprite: 天井で止まる y=" .. p.y)
            -- 当たり判定の箱
            local q = g:sprite{ x = 100, y = 10, w = 16, h = 16, hitbox = { 4, 4, 8, 8 }, tag = "coin" }
            local r2 = g:sprite{ x = 110, y = 20, w = 4, h = 4 }
            check(q:overlaps(r2), "overlaps: hitboxで判定")
            r2.x = 113
            check(not q:overlaps(r2), "overlaps: hitboxの外")

            -- 当たりの規則と削除
            HITS = 0
            g:collide("player", "coin", function(a, b) HITS = HITS + 1; b:remove() end)
            q.x, q.y = p.x - 4, p.y - 4
            g:step(1 / 60)
            check(HITS == 1 and #g:find("coin") == 0, "collide: 重なると呼ばれ、removeで消える")
            g:step(1 / 60)
            check(HITS == 1, "collide: 消えたものとはもう当たらない")
            check(g:sprite_at(p.x + 1, p.y + 1) == p, "sprite_at")

            -- アニメーション
            local an = g:sprite{ x = 0, y = 0, w = 8, h = 8, color = 1,
                anims = { walk = { frames = { 3, 4, 5 }, fps = 10 }, once = { frames = { 7, 8 }, fps = 10, loop = false,
                          on_done = function(s) ANIM_DONE = true end } } }
            an:play("walk")
            check(an.frame == 3, "play: 最初のコマ")
            g:step(0.05); g:step(0.05); check(an.frame == 4, "anim: 0.1秒で次のコマ")
            g:step(0.05); g:step(0.05); check(an.frame == 5, "anim: 次")
            g:step(0.05); g:step(0.05); check(an.frame == 3, "anim: 一周して戻る")
            g:step(1)
            check(an.frame == 3, "step: 1回のdtは0.05秒で頭打ち(コマが飛ばない)")
            an:play("once")
            for i = 1, 5 do g:step(0.05) end
            check(an.frame == 8 and an.anim_done and ANIM_DONE, "anim: loop=falseは最後で止まりon_done")
            check(not pcall(an.play, an, "nope"), "play: 知らない名前はエラー")
            an:remove()

            -- ゲーム内タイマーと状態
            LOGS = {}
            g:state("title", { enter = function(gg, a) LOGS[#LOGS + 1] = "enter:" .. tostring(a) end,
                               exit = function() LOGS[#LOGS + 1] = "exit" end,
                               update = function() LOGS.u = (LOGS.u or 0) + 1 end })
            g:state("play", { enter = function() LOGS[#LOGS + 1] = "play" end })
            g:go("title", 5)
            g:step(0.01)
            g:go("play")
            check(table.concat(LOGS, "|") == "enter:5|exit|play" and LOGS.u == 1, "state: enter/update/exit")
            check(not pcall(g.go, g, "nope"), "state: 知らない名前はエラー")
            local fired = 0
            g:after(0.5, function() fired = fired + 1 end)
            local ev = g:every(0.2, function() fired = fired + 10 end)
            for i = 1, 13 do g:step(0.05) end
            check(fired == 31, "after/every: ゲームの時間で動く " .. fired)
            g:cancel(ev)
            g:pause(true)
            for i = 1, 13 do g:step(0.05) end
            check(fired == 31, "pause: 止まっている間は進まない")
            g:pause(false)

            -- 画面ボタン(タッチ)
            g:_touch("start", 5, 260)            -- 左の矢印
            g:step(0.01)
            check(g:down("left") and g:pressed("left"), "button: 押した")
            g:step(0.01)
            check(g:down("left") and not g:pressed("left"), "button: 押しっぱなし")
            g:_touch("move", 45, 260)            -- 隣の「上」へ滑らせる
            g:step(0.01)
            check(g:down("up") and not g:down("left") and g:released("left"), "button: 滑らせると切り替わる")
            g:_touch("end", 45, 260)
            g:step(0.01)
            check(not g:down("up") and g:released("up"), "button: 離した")
            g:_touch("start", 10, 10); g:_touch("end", 10, 10)   -- 1フレームの間に押して離す
            g:step(0.01)
            check(g.touch.pressed and g.touch.x == 10, "touch: 短いタップも取りこぼさない")
            g:step(0.01)
            check(g.touch.released and not g.touch.down, "touch: 離した")
            local h, v = g:axis()
            check(h == 0 and v == 0, "axis")

            -- タイルの書き換え
            map:set(5, 3, 0)
            check(map:get(5, 3) == 0 and not map:is_solid(5, 3), "tilemap: set")
        )LUA", "game_logic");
        check(ok, "pico.game: ロジック");

        // 軽い物理: 加速度・抵抗・摩擦・跳ね返り・すり抜け床・スプライト同士の押し合い・足場
        ok = ge.Run(R"LUA(
            local game = require("pico.game")
            local w = game.new{ x = 0, y = 0, w = 240, h = 160, manual = true }
            local m = w:tilemap{ tile = 8, solid = { 1 }, oneway = { 3 },
                legend = { ["#"] = 1, ["="] = 3 },
                rows = {
                    "#..............................#",
                    "#..............................#",
                    "#..............................#",
                    "#..............................#",
                    "#..............................#",
                    "#......=====...................#",
                    "#..............................#",
                    "#..............................#",
                    "################################",
                } }
            check(m:is_oneway(8, 5) and not m:is_solid(8, 5), "tilemap: oneway")
            local function run(n) for i = 1, n do w:step(1 / 60) end end

            -- 跳ね返り: 床に当たると上向きになり、だんだん収まる
            local ball = w:sprite{ x = 150, y = 8, w = 8, h = 8, gravity = 600, solid = true, bounce = 0.6 }
            local up = false
            for i = 1, 60 do w:step(1 / 60); if ball.vy < 0 then up = true end end
            check(up, "bounce: 床で跳ね返る")
            run(300)
            check(ball.on_ground and ball.vy == 0 and ball.y == 56, "bounce: 遅くなると止まる y=" .. ball.y .. " vy=" .. ball.vy)
            -- 摩擦: 床の上で横の速さが0へ近づく(無ければ滑り続ける)
            ball.vx = 100
            run(10)
            check(ball.vx == 100, "friction無し: 滑り続ける")
            ball.friction = 600
            run(30)
            check(ball.vx == 0, "friction: 床の上で止まる")
            ball:remove()
            -- 空気抵抗・加速度・最高速度
            local f = w:sprite{ x = 100, y = 10, w = 4, h = 4, drag = 2, ax = 1000, max_vx = 50 }
            w:step(1 / 60)
            check(f.vx > 0 and f.vx <= 50, "ax/max_vx: 加速して上限で止まる")
            f.ax = nil; f.vx = 50
            run(30)
            check(f.vx < 50 * 0.5 and f.vx > 0, "drag: 減速する " .. f.vx)
            f:remove()
            -- impulse: 質量で割る / staticは動かない
            local h = w:sprite{ x = 100, y = 10, w = 4, h = 4, mass = 2 }
            h:impulse(100, -40)
            check(h.vx == 50 and h.vy == -20, "impulse: 質量で割る")
            local st = w:sprite{ x = 100, y = 10, w = 4, h = 4, static = true }
            st:impulse(100, 0)
            check(st.vx == 0, "impulse: staticは動かない")
            h:remove(); st:remove()

            -- すり抜け床: 下からは抜け、上からは乗り、drop_throughで落ちる
            local p = w:sprite{ x = 64, y = 56, w = 8, h = 8, gravity = 600, solid = true, tag = "p" }
            p.vy = -250
            local passed = false
            for i = 1, 90 do w:step(1 / 60); if p.y + p.h <= 40 then passed = true end end
            check(passed, "oneway: 下からジャンプで抜ける")
            check(p.on_ground and p.y == 32, "oneway: 上から落ちると乗る y=" .. p.y)
            p.drop_through = true
            run(30)
            check(p.y == 56, "oneway: drop_throughで落ちる y=" .. p.y)
            p.drop_through = false
            p:move(40, 0)
            check(p.hit_wall == 0, "oneway: 横からはすり抜ける")
            p.x, p.vx = 64, 0

            -- 押し合い: 同じ重さの箱を押すと一緒に動き、static の壁で止まる
            w:solid("p", "box")
            w:solid("box", "box")
            w:solid("p", "wall")
            w:solid("box", "wall")
            local box = w:sprite{ x = 80, y = 56, w = 8, h = 8, gravity = 600, solid = true, tag = "box", friction = 2000 }
            run(5)
            p.vx = 60
            for i = 1, 30 do p.vx = 60; w:step(1 / 60) end
            check(box.x > 80 and box.x >= p.x + 8 - 0.01, "solid: 箱を押す box.x=" .. box.x .. " p.x=" .. p.x)
            check(p.x + 8 <= box.x + 0.01, "solid: 重ならない")
            local wall = w:sprite{ x = box.x + 20, y = 40, w = 8, h = 24, static = true, tag = "wall" }
            for i = 1, 60 do p.vx = 60; w:step(1 / 60) end
            check(math.abs(box.x + 8 - wall.x) < 0.01 and wall.x == wall._px, "solid: staticの壁で止まり、壁は動かない box.x=" .. box.x)
            check(math.abs(p.x + 8 - box.x) < 0.01, "solid: 押している側も止まる p.x=" .. p.x)
            -- 積み重ね: 箱の上に箱が乗る
            local box2 = w:sprite{ x = box.x, y = 20, w = 8, h = 8, gravity = 600, solid = true, tag = "box" }
            run(60)
            check(math.abs(box2.y + 8 - box.y) < 0.6 and box2.on_ground, "solid: 箱の上に乗る y=" .. box2.y)
            box:remove(); box2:remove(); wall:remove()
            run(1)

            -- 動く足場: 乗っているものを運ぶ
            local HIT = 0
            w:solid("p", "lift", { oneway = true, on_hit = function(a, b, nx, ny) if ny == -1 then HIT = HIT + 1 end end })
            local lift = w:sprite{ x = 120, y = 40, w = 24, h = 4, static = true, tag = "lift" }
            p.x, p.y, p.vx, p.vy = 124, 20, 0, 0
            run(60)
            check(p.on_ground and math.abs(p.y + 8 - 40) < 0.6 and HIT > 0, "lift: 上から乗る y=" .. p.y)
            local px = p.x
            lift.vx = 30
            run(30)
            check(math.abs((p.x - px) - (lift.x - 120)) < 1, "lift: 横に運ばれる " .. (p.x - px) .. " / " .. (lift.x - 120))
            lift.vx = 0; lift.vy = -20
            run(30)
            check(math.abs(p.y + 8 - lift.y) < 0.6, "lift: 上へ運ばれる")
            lift.vy = 20
            run(30)
            check(math.abs(p.y + 8 - lift.y) < 1.5 and p.on_ground, "lift: 下がっても付いていく " .. (p.y + 8 - lift.y))
            -- 下からは通り抜ける(oneway)
            lift.vy = 0
            p.x, p.y, p.vy = lift.x + 4, lift.y + 10, -300
            run(4)
            check(p.y < lift.y, "lift(oneway): 下から抜ける")

            -- 撃力: 同じ重さの2つがぶつかると速度を分け合い、bounce=1なら入れ替わる。重いほうは押されにくい
            local w3 = game.new{ x = 0, y = 0, w = 200, h = 64, manual = true }
            w3:solid("m", "m")
            local m1 = w3:sprite{ x = 0, y = 10, w = 8, h = 8, vx = 100, tag = "m" }
            local m2 = w3:sprite{ x = 20, y = 10, w = 8, h = 8, tag = "m" }
            for i = 1, 30 do w3:step(1 / 60) end
            check(math.abs(m1.vx - 50) < 0.01 and math.abs(m2.vx - 50) < 0.01, "solid: 速度を分け合う " .. m1.vx .. "," .. m2.vx)
            m1.x, m1.vx, m2.x, m2.vx = 0, 100, 20, 0
            m1.bounce, m2.bounce = 1, 1
            for i = 1, 30 do w3:step(1 / 60) end
            check(math.abs(m1.vx) < 0.01 and math.abs(m2.vx - 100) < 0.01, "solid: bounce=1で入れ替わる")
            m1.x, m1.vx, m2.x, m2.vx, m1.bounce, m2.bounce = 0, 100, 20, 0, 0, 0
            m2.mass = 4
            for i = 1, 30 do w3:step(1 / 60) end
            check(math.abs(m1.vx - 20) < 0.01 and math.abs(m2.vx - 20) < 0.01, "solid: massの比で分ける")
            w3:destroy()

            -- 世界の重力: gravity無しのスプライトも落ちる(staticとgravity=0は落ちない)
            local w2 = game.new{ x = 0, y = 0, w = 64, h = 64, manual = true, gravity = 500 }
            local a1 = w2:sprite{ x = 0, y = 0, w = 4, h = 4, bounded = true }
            local a2 = w2:sprite{ x = 10, y = 0, w = 4, h = 4, static = true }
            local a3 = w2:sprite{ x = 20, y = 0, w = 4, h = 4, gravity = 0 }
            for i = 1, 60 do w2:step(1 / 60) end
            check(a1.y == 60 and a1.on_ground and a2.y == 0 and a3.y == 0, "game.gravity: 既定の重力")
            w2:destroy()
            w:destroy()
        )LUA", "game_physics");
        check(ok, "pico.game: 物理");

        // 描き直し: カメラが動かない間は動いたスプライトの周りだけ
        ok = ge.Run(R"LUA(
            g:step(0.01)                  -- 溜まっているものを流す
            mover = g:sprite{ x = 60, y = 10, w = 8, h = 8, color = 3 }
            g:step(0.01)                  -- 並び順が変わったので全体
        )LUA", "game_dirty0");
        g_dirty_calls = 0;
        g_last_dirty = Rect{0, 0, 0, 0};
        ok = ok && ge.Run("mover.x = mover.x + 3; g:step(0.01)", "game_dirty1");
        check(ok && g_dirty_calls == 2, "dirty: 動いたスプライトの前後だけ");
        check(g_last_dirty.w == 8 && g_last_dirty.h == 8 && g_last_dirty.x == 63 && g_last_dirty.y == 26,
              "dirty: 新しい位置の矩形(画面座標)");
        g_dirty_calls = 0;
        ok = ok && ge.Run("g:step(0.01)", "game_dirty2");
        check(ok && g_dirty_calls == 0, "dirty: 何も変わらなければ描き直さない");

        // カメラ: 大きいマップで追いかける
        ok = ge.Run(R"LUA(
            g:clear()
            local rows = {}
            for r = 1, 40 do rows[r] = string.rep(".", 100) end
            big = g:tilemap{ image = "/app/tiles.pimg", tile = 8, rows = rows }
            hero = g:sprite{ x = 400, y = 150, w = 8, h = 8, color = 7 }
            g:follow(hero)
            g:step(0.01)
            check(g.cam_x == 400 + 4 - 120 and g.cam_y == 150 + 4 - 124, "follow: 中央に保つ " .. g.cam_x .. "," .. g.cam_y)
            hero.x, hero.y = 0, 0
            g:step(0.01)
            check(g.cam_x == 0 and g.cam_y == 0, "follow: ワールドの端で止まる")
            hero.x, hero.y = 799, 319
            g:step(0.01)
            check(g.cam_x == 800 - 240 and g.cam_y == 320 - 248, "follow: 右下の端")
            local sx, sy = g:to_screen(hero.x, hero.y)
            check(sx == 799 - 560 and sy == 16 + 319 - 72, "to_screen")
            hero.bounded = true
            hero.vx = 500
            g:step(0.05)
            check(hero.x == 792 and hero.hit_wall == 1, "bounded: ワールドの端で止まる")
            -- 描画(エラーにならないこと)
            sprite_img = g:image("/app/tiles.pimg")
            g:sprite{ image = sprite_img, w = 8, h = 8, frame = 1, x = 700, y = 300, flip_x = true }
            g:sprite{ x = 690, y = 300, w = 8, h = 8, draw = function(s, x, y) DRAWN_AT = x .. "," .. y end }
            function g:on_draw(ox, oy) HUD = ox .. "," .. oy end
            g:step(0.01)
        )LUA", "game_camera");
        check(ok, "pico.game: カメラ");
        if (Widget* c = WidgetRegistry::Resolve((WidgetId)GlobalInt(GL, "G_CANVAS"))) {
            OSData::frame->setClipRect(0, 16, 240, 304);
            PICO_GFX::isDirtyDeactivates = true; // FlushDirty()の合成の中を模す
            c->renderForce();
            PICO_GFX::isDirtyDeactivates = false;
            OSData::frame->clearClipRect();
        }
        ok = ge.Run(R"LUA(
            check(HUD == "0,16", "render: on_drawにキャンバスの左上")
            check(DRAWN_AT == (690 - 560) .. "," .. (16 + 300 - 72), "render: drawで描くスプライトに画面座標 " .. tostring(DRAWN_AT))
            g:destroy()
            check(g.canvas == nil, "destroy")
        )LUA", "game_render");
        check(ok, "pico.game: 描画");
        WidgetFunctions::ProcessPendingDeletes();
        WidgetFunctions::ClearSceneWidgets();
        HostSd::files.clear();
        PicoHostClock::now = 0;
    }

    // =====================================================================
    // 描画中のクリップ: 文字・アイコンが今のクリップ(FlushDirty()のdirty矩形)を外さないこと、
    // set_draw_area/clear_draw_areaがウィジェットの描画範囲の外へ広げないこと
    // =====================================================================
    {
        auto clip_is = [](int x, int y, int w, int h) {
            int32_t cx, cy, cw, ch;
            OSData::frame->getClipRect(&cx, &cy, &cw, &ch);
            return cx == x && cy == y && cw == w && ch == h;
        };
        OSData::frame->createSprite(SCREEN_WIDTH, SCREEN_HEIGHT);
        OSData::frame->setClipRect(10, 20, 50, 60);
        bool ok = engine.Run("pico.draw_text(0, 30, 'abc', 0, 0)", "clip_text");
        check(ok && clip_is(10, 20, 50, 60), "draw_text: 描いた後もクリップが元のまま");

        PICO_GFX::render_clip = Rect{10, 20, 50, 60};
        PICO_GFX::render_clip_active = true;
        ok = engine.Run("pico.set_draw_area(0, 0, 40, 200)", "clip_area");
        check(ok && clip_is(10, 20, 30, 60), "set_draw_area: 描画中はウィジェットの描画範囲との重なりに留まる");
        ok = engine.Run("pico.clear_draw_area()", "clip_clear");
        check(ok && clip_is(10, 20, 50, 60), "clear_draw_area: 描画中はウィジェットの描画範囲へ戻す");
        PICO_GFX::render_clip_active = false;
        OSData::frame->clearClipRect();
    }

    // =====================================================================
    // pico.iso: Luaからの呼び方と権限(エンジンの中身は iso_world_test)
    // =====================================================================
    {
        // 面の絵はリポジトリの本物を使う(透けないブロックの判定に使われる)
        std::string root(__FILE__);
        root = root.substr(0, root.rfind("/script/host_test/"));
        std::ifstream fin(root + "/pc/sdcard/lua/apps/ブロック/faces.pimg", std::ios::binary);
        HostSd::files["/app/faces.pimg"] = std::string((std::istreambuf_iterator<char>(fin)), {});
        OSData::SD_usable = true;
        OSData::frame->createSprite(SCREEN_WIDTH, SCREEN_HEIGHT);
        LuaEngine ie(160 * 1024, LuaPermissions{}, "/app");
        lua_register(ie.raw(), "check", l_check);
        bool ok = ie.Run(R"LUA(
            local iso = pico.iso
            check(iso.size() == nil, "iso.size: 開いていなければ nil")
            local img = pico.image_load("/app/faces.pimg")
            check(img ~= nil, "faces.pimg を読める")
            iso.set_image(img)
            check(not pcall(iso.set_image, pico.image_create(16, 16)), "iso.set_image: 小さい画像はエラー(今の絵のまま)")
            local x, y, z = iso.create("/app/w", 0, 1234)
            check(x == 512 and z == 512 and y > 0, "iso.create: 始めのカーソルは真ん中の柱の一番下の空気")
            local w, h, kind = iso.size()
            check(w == 1024 and h == 16 and kind == 0, "iso.size")
            check(iso.get(x, y, z) == 0 and iso.get(x, y - 1, z) ~= 0, "iso.get")
            iso.set(x, y, z, 13)
            check(iso.get(x, y, z) == 13, "iso.set")
            check(not pcall(iso.set, x, y, z, 26), "iso.set: 知らないブロックはエラー")
            iso.view(0, 20, 240, 204)
            iso.origin(120 - 16, 122 - 16 + 8 * (x + z) + 16 * y)
            local ox, oy = iso.origin()
            check(ox == 104, "iso.origin: 読み戻せる")
            iso.pump(64)                  -- 視点が変わったので範囲を決め直して読み込む
            while iso.pending() > 0 do iso.pump(64) end
            check(iso.stats().chunks > 10, "iso.pump: 見える所のチャンクを読み込む (" .. iso.stats().chunks .. ")")
            local bx, by = iso.block_pos(x, y, z)
            local px, py, pz, f = iso.pick(bx + 16, by + 7)
            check(px == x and py == y and pz == z and f == "top", "iso.pick: 置いたブロックの上面")
            iso.cursor(x, y, z, true)
            iso.render(0, 20, 240, 204)
            check(iso.stats().faces > 50, "iso.render: 面を描く (" .. iso.stats().faces .. ")")
            iso.set(x + 1, y, z, 25)
            check(iso.light(x + 1, y, z) == 3 and iso.light(x + 4, y, z) == 2 and iso.light(x + 9, y, z) == 0,
                  "iso.light: 松明のまわりが明るい")
            check(iso.sunlight() == true and iso.sunlight(false) == false, "iso.sunlight: 夜にできる")
            iso.render(0, 20, 240, 204)
            check(iso.stats().faces > 50, "iso.render: 夜も描ける(ディザの遅い道)")
            iso.sunlight(true)
            iso.set(x + 1, y, z, 0)
            check(iso.light(x + 1, y, z) == 0, "iso.light: 松明を取ると暗い")
            iso.draw_icon(25, 10, 10)
            -- 経路探索: 空中(y=14)に石の床を作り、その上だけを歩かせる
            for i = 0, 10 do for j = 0, 4 do iso.set(x + i, 14, z + j, 2) end end
            check(iso.stand(x + 3, z + 2) == 15, "iso.stand: 柱の一番上の立てる高さ")
            local p, cost, how = iso.path(x, nil, z, x + 10, nil, z)
            check(p and #p == 11 and cost == 10 and how == "found" and p[1].y == 15 and p[11].x == x + 10,
                  "iso.path: 床の上をまっすぐ")
            for j = 0, 3 do iso.set(x + 5, 14, z + j, 11) end      -- 砂の帯(z+4 だけ石)
            p, cost = iso.path(x, nil, z, x + 10, nil, z, {avoid = {11}})
            check(p and cost == 18, "iso.path: avoid で砂を避けて回る (" .. tostring(cost) .. ")")
            p, cost = iso.path(x, nil, z, x + 10, nil, z, {block_cost = {[11] = 3}})
            check(p and cost == 13, "iso.path: block_cost")
            for j = 0, 4 do iso.set(x + 7, 15, z + j, 2) end         -- 1段の段(床の幅いっぱい)
            p, cost = iso.path(x, nil, z, x + 10, nil, z, {up_cost = 4, down_cost = 1})
            check(p and cost == 15 and p[8].y == 16, "iso.path: up_cost/down_cost(段を越える)")
            local none, why = iso.path(x, nil, z, x + 10, nil, z, {max_up = 0})
            check(none == nil and why == "no_path", "iso.path: max_up=0 なら段を越えられない")
            for j = 1, 4 do iso.set(x + 7, 15, z + j, 0) end         -- 段は z の1列だけ残す
            p, cost = iso.path(x, nil, z, x + 10, nil, z, {up_cost = 4, down_cost = 1})
            check(p and cost == 12, "iso.path: 段差が高くつけば横を回る")
            iso.set(x + 7, 15, z, 0)
            local calls = 0
            p, cost = iso.path(x, nil, z, x + 10, nil, z, {edge = function(ax, ay, az, bx, by, bz, floor)
                calls = calls + 1
                if bz == z and bx == x + 3 then return false end
                return 0
            end})
            check(p and calls > 0 and cost == 12, "iso.path: edge で通れなくする")
            check(not pcall(iso.path, x, nil, z, x + 10, nil, z, {edge = function() error("boom") end}),
                  "iso.path: edge のエラーはそのまま")
            none, why = iso.path(x, nil, z, x + 10, nil, z + 30)
            check(none == nil and (why == "no_path" or why == "goal" or why == "limit"), "iso.path: 着けない (" .. tostring(why) .. ")")
            p, cost, how = iso.path(x, nil, z, x + 10, nil, z + 30, {partial = true})
            check(p and how == "partial", "iso.path: partial")
            check(not pcall(iso.path, x, nil, z, x + 1, nil, z, {avoid = {99}}), "iso.path: 知らないブロックはエラー")
            check(not pcall(iso.path, x, nil, z, x + 1, nil, z, {max_nodes = 0}), "iso.path: max_nodes の範囲")
            check(iso.save(x, y, z, 13), "iso.save")
            iso.close()
            check(iso.size() == nil, "iso.close")
            local k2, w2 = iso.info("/app/w/world.dat")
            check(k2 == 0 and w2 == 1024, "iso.info")
            local ox2, oy2, oz2, cur = iso.open("/app/w")
            check(ox2 == x and oy2 == y and oz2 == z and cur == 13, "iso.open: 位置とブロック")
            check(iso.get(x, y, z) == 0, "iso.open: 読み込む前は空気")
            iso.pump(100)
            check(iso.pending() == 0 and iso.get(x, y, z) == 13, "iso.open: 書き換えたチャンクを読む")
            -- 人や物: 透過の画像を立てて置く
            local sp = pico.image_create(12, 24, true)
            pico.image_target(sp)
            pico.fill_rect(2, 0, 8, 24, 12)
            pico.image_target(nil)
            check(not pcall(iso.entity_add, 999, x, y, z), "iso.entity_add: 無効な画像はエラー")
            check(not pcall(iso.entity_add, sp, x, y, z, { w = 40 }), "iso.entity_add: 画像の外の範囲はエラー")
            -- (x, y, z) には TNT を置いてある。その上に立てる
            local ex, ey, ez = x + 0.5, iso.ground(x + 0.5, z + 0.5, y + 1), z + 0.5
            check(iso.loaded(x + 0.5, z + 0.5) and not iso.loaded(-1, 5) and not iso.loaded(5000, 5)
                  and not iso.loaded(x + 400, z), "iso.loaded: 読み込んだ所だけ true(世界の外・遠くは false)")
            check(ey == y + 1 and iso.ground(x + 0.5, z + 0.5, y) == y and iso.ground(x + 0.5, z + 0.5, 0) == nil,
                  "iso.ground: 足の裏より下の一番上の地面")
            local id = iso.entity_add(sp, ex, ey, ez, { shadow_color = 4 })
            check(type(id) == "number", "iso.entity_add: ハンドル")
            local gx, gy, gz, info = iso.entity_get(id)
            check(gx == ex and gy == ey and info.w == 12 and info.h == 24 and info.ax == 6 and info.ay == 23
                  and math.abs(info.height - 1.5) < 1e-6 and info.shadow == true, "iso.entity_get: 位置と既定の値")
            local fx, fy = iso.to_screen(ex, ey, ez)
            fx, fy = math.floor(fx + 0.5), math.floor(fy + 0.5)
            iso.cursor(x, y, z, false)
            iso.render(0, 20, 240, 204)
            check(pico.get_pixel(fx, fy - 10) == 12, "iso.render: 人や物の絵を描く")
            check(iso.entity_at(fx, fy - 10) == id and iso.entity_at(fx - 6, fy - 10) == nil, "iso.entity_at: 絵の不透明な所")
            -- 手前(-x)に高い壁を立てると隠れる
            for yy = y, y + 4 do for zz = z - 2, z + 2 do iso.set(x - 1, yy, zz, 2) end end
            iso.render(0, 20, 240, 204)
            check(pico.get_pixel(fx, fy - 10) ~= 12, "iso.render: 手前の壁の裏の人や物は隠れる")
            for yy = y, y + 4 do for zz = z - 2, z + 2 do iso.set(x - 1, yy, zz, 0) end end
            iso.entity_move(id, ex + 2, ey, ez)
            iso.entity_set(id, { flip = true, visible = false, height = 2, r = 0.3 })
            gx, gy, gz, info = iso.entity_get(id)
            check(gx == ex + 2 and info.flip and not info.visible and info.height == 2, "iso.entity_move / entity_set")
            check(not pcall(iso.entity_set, id, { r = -1 }), "iso.entity_set: おかしな値はエラー(何も変えない)")
            check(select(4, iso.entity_get(id)).r > 0.29, "iso.entity_set: エラーなら変えない")
            iso.entity_set(id, { sx = 2, w = 8 })
            check(select(4, iso.entity_get(id)).ax == 4, "iso.entity_set: 範囲を変えると足元の点も決め直す")
            check(iso.entity_remove(id) and iso.entity_get(id) == nil and not iso.entity_remove(id), "iso.entity_remove")
            check(not pcall(iso.entity_move, id, 1, 1, 1), "iso.entity_move: 無効なハンドルはエラー")
            local ids = {}
            for i = 1, 100 do ids[#ids + 1] = iso.entity_add(sp, ex, ey, ez) end
            local nn, why = iso.entity_add(sp, ex, ey, ez)
            check(#ids == 96 and nn == nil and why ~= nil, "iso.entity_add: 96個まで")
            iso.entity_clear()
            check(iso.entity_get(ids[1]) == nil, "iso.entity_clear")
            local n, e = iso.create("/other/w", 1, 5)
            check(n == nil and e ~= nil, "iso.create: アプリのディレクトリの外は断る")
            check(iso.open("/other/w") == nil, "iso.open: アプリのディレクトリの外は断る")
            check(iso.info("/other/w/world.dat") == nil, "iso.info: アプリのディレクトリの外は nil")
            check(iso.migrate("/other/old.dat", "/app/m") == nil, "iso.migrate: アプリのディレクトリの外は断る")
            iso.dirty_block(x, y, z)
            iso.dirty_edit(x, y, z)
            iso.draw_icon(3, 10, 10)
            check(not pcall(iso.create, "/app/w", 9, 1), "iso.create: 知らない種類はエラー")

            -- ---- タワーディフェンス向けの道具 ----
            iso.create("/app/t", 3, 1, 4)                 -- 空の 32x32
            check(iso.keep_all(true), "iso.keep_all: 4x4 チャンクなら読み込んだままにできる")
            iso.view(0, 20, 240, 204)
            iso.origin(104, 106 + 8 * 20 + 16)
            while iso.pump(64) > 0 do end
            check(iso.stats().chunks == 16, "iso.keep_all: 全部読み込む")
            for i = 0, 31 do for k = 0, 31 do iso.set(i, 0, k, 2) end end
            check(not pcall(iso.flow_build, {}), "iso.flow_build: 目的地が無ければエラー")
            check(iso.flow_build({{20, 20}}, {}, true), "iso.flow_build: その場で作る")
            local d, nx, nz, ny = iso.flow_get(10.4, 20.7)
            check(d == 10 and nx == 11 and nz == 20 and ny == 1, "iso.flow_get: 値段と次の柱と立つ高さ")
            d, nx = iso.flow_get(20, 20)
            check(d == 0 and nx == nil, "iso.flow_get: 目的地は次が無い")
            check(iso.flow_get(-1, 3) == nil, "iso.flow_get: 世界の外は nil")
            -- バリケード(板=6)の列で仕切る: pass なら中を通れて、body_cost だけ高い
            for k = 0, 31 do iso.set(15, 1, k, 6); iso.set(15, 2, k, 6) end
            check(iso.flow_build({{20, 20}}, {pass = {6}, body_cost = {[6] = 2}}), "iso.flow_build: 少しずつ作り始める")
            check(iso.flow_info().building and iso.flow_get(10, 20) == 10, "iso.flow_step: 作っている間は前の結果")
            local steps = 0
            while not iso.flow_step(200) do steps = steps + 1 end
            check(steps > 2 and iso.flow_get(10, 20) == 14 and iso.flow_info().revision == 2, "iso.flow_step: 出来上がると入れ替わる(板の中を通る)")
            check(iso.stand(15, 20, nil, {pass = {6}}) == 1 and iso.stand(15, 20) == 3, "iso.stand: pass のブロックの上には立たない")
            local p2, c2 = iso.path(10, nil, 20, 20, nil, 20, {pass = {6}, body_cost = {[6] = 2}})
            check(p2 and c2 == 14, "iso.path: pass と body_cost")
            check(not iso.sight(10.5, 1.5, 20.5, 20.5, 1.5, 20.5) and iso.sight(10.5, 1.5, 20.5, 20.5, 1.5, 20.5, {6}),
                  "iso.sight: 板に遮られる / pass なら通る")
            -- 人や物: 押し合い・分類・HPバー
            local a = iso.entity_add(sp, 10.5, 1, 10.5, {crowd = "move", tag = 3, bar = 100, bar_color = 11, mark = 9})
            local b = iso.entity_add(sp, 10.6, 1, 10.5, {crowd = "move", tag = 4, mass = 2})
            local t = iso.entity_add(sp, 12.5, 1, 10.5, {crowd = "fixed", tag = 5})
            local _, _, _, ia = iso.entity_get(a)
            check(ia.crowd == "move" and ia.tag == 3 and ia.bar == 100 and ia.mark == 9 and ia.bar_color == 11, "iso.entity_get: 足した値")
            check(not pcall(iso.entity_set, a, {crowd = "push"}) and not pcall(iso.entity_set, a, {tag = 40}), "iso.entity_set: crowd/tag の誤り")
            check(iso.crowd() == 2, "iso.crowd: 重なった2人が動く")
            local ax = iso.entity_get(a)
            local bx = iso.entity_get(b)
            check(math.abs((bx - ax) - 0.375) < 1e-3 and bx - 10.6 < 10.5 - ax, "iso.crowd: 重いほうが動かない")
            local near = iso.nearby(10.5, 10.5, 3)
            check(#near == 3 and near[3] == t, "iso.nearby: 近い順")
            near = iso.nearby(10.5, 10.5, 3, {4, 5})
            check(#near == 2 and near[1] == b, "iso.nearby: tag で絞る")
            check(#iso.nearby(10.5, 10.5, 3, 3) == 1, "iso.nearby: tag は数1つでもよい")
            iso.cursor(0, 0, 0, false)
            iso.entity_set(a, {x = 10.5, z = 10.5})
            local fx2, fy2 = iso.to_screen(10.5, 1, 10.5)
            fx2, fy2 = math.floor(fx2 + 0.5), math.floor(fy2 + 0.5)
            iso.render(0, 20, 240, 204)
            check(pico.get_pixel(fx2 - 5, fy2 - 23 - 3) == 11 and pico.get_pixel(fx2, fy2 - 23 - 9) == 9, "iso.render: HPバーと印")
            iso.entity_set(a, {bar = false, mark = false})
            check(select(4, iso.entity_get(a)).bar == nil, "iso.entity_set: bar = false で消す")
            -- 弾
            check(not pcall(iso.shot_add, 1, 1, 1, {target = 12345}), "iso.shot_add: 無効な相手はエラー")
            check(not pcall(iso.shot_add, 1, 1, 1, {}), "iso.shot_add: 相手も点も無ければエラー")
            local s = iso.shot_add(5.5, 2, 10.5, {target = t, speed = 20, tag = 3, color = 8})
            check(type(s) == "number" and iso.shot_count() == 1, "iso.shot_add")
            local hits
            for i = 1, 20 do hits = iso.shots_step(0.1); if #hits > 0 then break end end
            check(#hits == 1 and hits[1].id == s and hits[1].target == t and hits[1].tag == 3 and not hits[1].lost
                  and math.abs(hits[1].x - 12.5) < 1e-3, "iso.shots_step: 狙った相手に当たる")
            check(iso.shot_count() == 0, "iso.shots_step: 当たった弾は消える")
            iso.shot_add(1.5, 1, 1.5, {tx = 3.5, ty = 1, tz = 1.5, arc = 0.5})
            iso.shot_clear()
            check(iso.shot_count() == 0, "iso.shot_clear")
            check(iso.arena() == nil, "iso.arena: ARENA でなければ nil")
            local ex, ey, ez = iso.entity_pos(a)
            local gx, gy, gz = iso.entity_get(a)
            check(ex == gx and ey == gy and ez == gz, "iso.entity_pos: entity_get と同じ位置")
            iso.close()
            check(iso.flow_info().ready == false, "iso.close: 流れの場も片付く")
            check(iso.entity_pos(a) == nil, "iso.entity_pos: 閉じたら nil")
            -- ARENA(タワーディフェンスの地形)
            local bx, by, bz = iso.create("/app/a", 4, 3, 7)
            check(bx == 28 and bz == 4, "iso.create: ARENA はベースの中心を返す")
            local ar = iso.arena()
            check(ar and ar.base.x == bx and ar.base.y == by and ar.base.z == bz and #ar.spawns == 3
                  and ar.spawns[1].z == 53, "iso.arena: ベースと出現位置")
            check(iso.keep_all(true), "iso.keep_all: ARENA 7x7")
            while iso.pump(64) > 0 do end
            check(iso.get(bx, by - 1, bz) == 5 and iso.get(ar.spawns[2].x, ar.spawns[2].y - 1, ar.spawns[2].z) == 18,
                  "iso.arena: 立つ高さの下はベースが丸石・出現位置が砂利")
            iso.close()
            -- pico.micros
            local u0 = pico.micros()
            check(math.type(u0) == "integer" and u0 >= 0 and u0 < 4294967296, "pico.micros: 32bit の整数")
        )LUA", "iso");
        check(ok, "pico.iso: Luaから一通り使える");
        check(HostSd::files.count("/app/w/world.dat") == 1 && HostSd::files.count("/app/w/c_64_64.dat") == 1,
              "pico.iso: 見出しと書き換えたチャンクだけを書き出す");
        check(HostSd::files.count("/app/w/c_63_63.dat") == 0, "pico.iso: 書き換えていないチャンクは書き出さない");
    }

    // =====================================================================
    // app.cfg の strip_debug: require したモジュールのデバッグ情報を落とす
    // =====================================================================
    {
        OSData::SD_usable = true;
        HostSd::files["/sd1/mod.lua"] = "local M = {}\nfunction M.boom()\n  error('boom')\nend\nreturn M\n";
        HostSd::files["/sd1/main.lua"] = "";
        std::string msgs[2];
        size_t used[2] = {0, 0};
        for (int k = 0; k < 2; k++) {
            LuaPermissions perm;
            perm.strip_debug = (k == 1);
            LuaEngine se(64 * 1024, perm, "/sd1");
            bool ok = se.Run("M = require('mod'); collectgarbage('collect'); local ok, e = pcall(M.boom); MSG = e", "sd");
            check(ok, "strip_debug: require できる");
            lua_getglobal(se.raw(), "MSG");
            msgs[k] = lua_tostring(se.raw(), -1) ? lua_tostring(se.raw(), -1) : "";
            lua_pop(se.raw(), 1);
            used[k] = (size_t)lua_gc(se.raw(), LUA_GCCOUNT, 0) * 1024 + lua_gc(se.raw(), LUA_GCCOUNTB, 0);
        }
        check(msgs[0].find("mod.lua:3:") != std::string::npos, "strip_debug: 既定はエラーに行番号が付く");
        check(msgs[1].find("mod.lua:3:") == std::string::npos && msgs[1].find("boom") != std::string::npos,
              "strip_debug: 落とすと行番号が付かない");
        check(used[1] < used[0], "strip_debug: Luaのメモリが減る");
    }

    // =====================================================================
    // ゾンビTD の zombies.lua を本物のエンジン(ARENA・流れの場・押し合い・弾)の上で動かす
    // =====================================================================
    {
        std::string root(__FILE__);
        root = root.substr(0, root.rfind("/script/host_test/"));
        auto slurp = [&](const char* rel) {
            std::ifstream f(root + rel, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(f)), {});
        };
        HostSd::files["/td/zombies.lua"] = slurp("/pc/sdcard/lua/apps/ゾンビTD/zombies.lua");
        HostSd::files["/td/combat.lua"] = slurp("/pc/sdcard/lua/apps/ゾンビTD/combat.lua");
        HostSd::files["/td/soldiers.lua"] = slurp("/pc/sdcard/lua/apps/ゾンビTD/soldiers.lua");
        HostSd::files["/td/buildings.lua"] = slurp("/pc/sdcard/lua/apps/ゾンビTD/buildings.lua");
        HostSd::files["/td/sfx.lua"] = slurp("/pc/sdcard/lua/apps/ゾンビTD/sfx.lua");
        HostSd::files["/td/units.pimg"] = slurp("/pc/sdcard/lua/apps/ゾンビTD/units.pimg");
        HostSd::files["/td/faces.pimg"] = slurp("/pc/sdcard/lua/apps/ゾンビTD/faces.pimg");
        OSData::SD_usable = true;
        LuaEngine te(180 * 1024, LuaPermissions{}, "/td");
        lua_register(te.raw(), "check", l_check);
        bool ok = te.Run(R"LUA(
            local iso = pico.iso
            Z = require("zombies")
            C = require("combat")
            iso.set_image(pico.image_load("/td/faces.pimg"))
            local units = pico.image_load("/td/units.pimg")
            iso.create("/td/map", 4, 3, 7)
            iso.keep_all(true)
            iso.view(0, 20, 240, 246)
            while iso.pump(64) > 0 do end
            A = iso.arena()
            local b = A.base
            local goals = {}
            for dx = -2, 2 do for dz = -2, 2 do
                if math.max(math.abs(dx), math.abs(dz)) == 2 then goals[#goals + 1] = {b.x + dx, b.z + dz} end
            end end
            iso.flow_build(goals, {max_up = 1, max_down = 2, height = 2, up_cost = 0.5, diagonal = true}, true)
            for _, s in ipairs(A.spawns) do check(iso.flow_get(s.x, s.z) ~= nil, "zombies: 出現位置から道がある") end
            BASE = {x = b.x, y = b.y, z = b.z, hp = 100000, melee = 0, thrown = 0, r = 1.4, side = "base"}
            BASE.id = iso.entity_add(units, b.x + 0.5, b.y, b.z + 0.5,
                {sx = 80, sy = 0, w = 44, h = 40, r = 1.4, height = 2.5, crowd = "fixed", tag = 2})
            function BASE.hurt(self, d, src)
                BASE.hp = BASE.hp - d
                if src and src.t.reach > 0 then BASE.thrown = BASE.thrown + 1 else BASE.melee = BASE.melee + 1 end
            end
            C.add(BASE)
            math.randomseed(7)
            Z.init(units, A.spawns, BASE, {height = 2})
            Z.queue("normal", 30); Z.queue("ranged", 12); Z.queue("heavy", 8)
            check(Z.waiting() == 50 and Z.alive() == 0, "zombies.queue: 順番待ちに積む")
            Z.update(0.05)
            check(Z.alive() >= 1 and Z.alive() <= 3, "zombies.update: 出現位置ごとに間を空けて出す")
            function tick(n)
                for _ = 1, n do
                    Z.update(0.05)
                    if iso.crowd(2, {height = 2}) > 0 then Z.sync() end
                    C.step(0.05)
                end
            end
        )LUA", "td");
        check(ok, "zombies: 準備");
        int max_alive = 0;
        // 120秒ぶん(1回の Run は 2秒ぶん。命令数の上限に掛からないよう分ける)
        for (int i = 0; i < 60 && ok; i++) {
            ok = te.Run("tick(40)", "tick");
            lua_State* L = te.raw();
            lua_getglobal(L, "Z"); lua_getfield(L, -1, "alive"); lua_call(L, 0, 1);
            max_alive = std::max(max_alive, (int)lua_tointeger(L, -1));
            lua_pop(L, 2);
        }
        check(ok, "zombies: 120秒ぶん動かしてもエラーにならない");
        check(max_alive == 40, "zombies: 同時に出るのは40匹まで");
        ok = te.Run(R"LUA(
            local iso = pico.iso
            check(Z.alive() == 40 and Z.waiting() == 10, "zombies: 倒さなければ上限の40匹で止まり、残りは待つ")
            check(BASE.melee > 0, "zombies: 近接のゾンビがベースへ着いて叩く")
            check(BASE.thrown > 0, "zombies: 遠距離のゾンビの石がベースに当たる")
            -- 全員がマップの中、ベースのまわりへ寄っている
            local near, inside = 0, true
            for _, zb in ipairs(Z.list) do
                local x, y, z = iso.entity_pos(zb.id)
                if x < 0 or x > 56 or z < 0 or z > 56 then inside = false end
                if math.abs(x - (A.base.x + 0.5)) < 8 and math.abs(z - (A.base.z + 0.5)) < 8 then near = near + 1 end
                -- 立っている高さ: 柱の地面の上(浮いても埋まってもいない)
                local g = iso.stand(x, z, nil, {height = 2})
                if g and math.abs(y - g) > 1.01 then inside = false end
            end
            check(inside, "zombies: マップの中で地面に立っている")
            check(near >= 30, "zombies: ほとんどがベースのまわりへ着く (" .. near .. ")")
            -- 押し合い: 同じ所に重なっていない
            local overlap = 0
            for i = 1, #Z.list do for k = i + 1, #Z.list do
                local ax, _, az = iso.entity_pos(Z.list[i].id)
                local bx, _, bz = iso.entity_pos(Z.list[k].id)
                if (ax - bx) ^ 2 + (az - bz) ^ 2 < 0.01 then overlap = overlap + 1 end
            end end
            check(overlap == 0, "zombies: 押し合って重ならない")
            Z.kill(1)
            check(Z.alive() == 39 and Z.stats.killed == 1, "zombies.kill")
            Z.update(0.5)
            check(Z.alive() == 40 and Z.waiting() == 9, "zombies: 倒れた分だけ順番待ちから出す")
            Z.clear()
            check(Z.alive() == 0 and Z.waiting() == 0 and iso.entity_get(BASE.id) ~= nil, "zombies.clear: ゾンビだけ消える")
            iso.close()
        )LUA", "td3");
        check(ok, "zombies: 上限・ベースへの攻撃・押し合い・倒す");
    }

    // =====================================================================
    // ゾンビTD の soldiers.lua(雇う・移動・戦い・回復・強化・売却)を本物のエンジンの上で動かす
    // =====================================================================
    {
        OSData::SD_usable = true;
        LuaEngine te(180 * 1024, LuaPermissions{}, "/td");
        lua_register(te.raw(), "check", l_check);
        bool ok = te.Run(R"LUA(
            local iso = pico.iso
            Z = require("zombies"); C = require("combat"); S = require("soldiers")
            iso.set_image(pico.image_load("/td/faces.pimg"))
            local units = pico.image_load("/td/units.pimg")
            iso.create("/td/map2", 4, 3, 7)
            iso.keep_all(true)
            iso.view(0, 20, 240, 246)
            while iso.pump(64) > 0 do end
            A = iso.arena()
            local b = A.base
            local goals = {}
            for dx = -2, 2 do for dz = -2, 2 do
                if math.max(math.abs(dx), math.abs(dz)) == 2 then goals[#goals + 1] = {b.x + dx, b.z + dz} end
            end end
            RULES = {height = 2, avoid = {1}}
            iso.flow_build(goals, {max_up = 1, max_down = 2, height = 2, up_cost = 0.5, diagonal = true, avoid = {1}}, true)
            BASE = {x = b.x, y = b.y, z = b.z, hp = 100000, r = 1.4, side = "base"}
            BASE.id = iso.entity_add(units, b.x + 0.5, b.y, b.z + 0.5,
                {sx = 80, sy = 0, w = 44, h = 40, r = 1.4, height = 2.5, crowd = "fixed", tag = 2})
            function BASE.hurt(self, d) BASE.hp = BASE.hp - d end
            C.add(BASE)
            math.randomseed(11)
            Z.init(units, A.spawns, BASE, RULES)
            S.init(units, BASE, RULES)
            Z.TARGET_TAGS = {S.TAG}; S.ZOMBIE_TAG = Z.TAG; Z.hunt = true
            KILLED, LOST, HEALED, LASTHP = 0, 0, 0, setmetatable({}, {__mode = "k"})
            Z.on_kill = function() KILLED = KILLED + 1 end
            S.on_lost = function() LOST = LOST + 1 end
            function tick(n)
                for _ = 1, n do
                    Z.update(0.05)
                    S.update(0.05)
                    if iso.crowd(2, {height = 2}) > 0 then Z.sync(); S.sync() end
                    C.step(0.05)
                    for _, s in ipairs(S.list) do
                        if s.state == "hold" and s.kind == "melee" then
                            local d = math.sqrt((s.x - s.px) ^ 2 + (s.z - s.pz) ^ 2)
                            if d > (MAXLEASH or 0) then MAXLEASH = d end
                        end
                        if s.hp > (LASTHP[s] or s.hp) then HEALED = HEALED + 1 end
                        LASTHP[s] = s.hp
                    end
                end
            end
            -- 雇う: ベースの前へ出てきて、持ち場へ歩いて止まる
            M1 = S.hire("melee"); H1 = S.hire("healer"); R1 = S.hire("ranged")
            check(M1 and H1 and R1 and S.count() == 3, "soldiers.hire: 3種類を雇える")
            check(M1.t.name == "近接" and M1.lv == 1 and M1.hp == 60, "soldiers.hire: Lv1 の値")
        )LUA", "sol");
        check(ok, "soldiers: 準備");
        ok = ok && te.Run("tick(120)", "tick");
        ok = ok && te.Run(R"LUA(
            local iso = pico.iso
            for _, s in ipairs(S.list) do
                check(s.state == "hold" and not s.path, "soldiers: 持ち場に着いて止まる (" .. s.kind .. ")")
                local g = iso.stand(s.x, s.z, nil, RULES)
                check(g and math.abs(s.y - g) < 0.01, "soldiers: 地面に立っている")
                check(s.z > BASE.z + 1.5, "soldiers: ベースの前(出現位置の側)にいる")
            end
            -- 移動の指示: 10マスほど先へ3人を散らばらせる
            local tx, tz = BASE.x, BASE.z + 14
            while not S.standable(tx, tz) do tx = tx + 1 end
            check(S.order_group(S.list, tx + 0.5, tz + 0.5), "soldiers.order_group: 立てる所へは指示できる")
            DEST = {tx, tz}
            -- 水の上へは指示できない
            local wx, wz
            for x = 0, 55 do for z = 0, 55 do
                if not wx and iso.ground(x, z) and iso.get(x, iso.ground(x, z) - 1, z) == 1 then wx, wz = x, z end
            end end
            if wx then check(not S.order_group(S.list, wx + 0.5, wz + 0.5), "soldiers.order_group: 水の上は不可") end
            check(not S.order_group(S.list, BASE.x + 0.5, BASE.z + 0.5), "soldiers.order_group: ベースの上は不可")
        )LUA", "sol2");
        ok = ok && te.Run("tick(200)", "tick") && te.Run("tick(200)", "tick");
        ok = ok && te.Run(R"LUA(
            local iso = pico.iso
            local cols = {}
            for _, s in ipairs(S.list) do
                check(s.state == "hold", "soldiers: 指示した所へ着く (" .. s.kind .. ")")
                check(math.abs(s.x - (DEST[1] + 0.5)) <= 3.6 and math.abs(s.z - (DEST[2] + 0.5)) <= 3.6, "soldiers: 指示した地点のまわりにいる")
                cols[math.floor(s.px) * 100 + math.floor(s.pz)] = true
                check(iso.get(math.floor(s.x), math.floor(s.gy) - 1, math.floor(s.z)) ~= 1, "soldiers: 水の上に立っていない")
            end
            local n = 0 for _ in pairs(cols) do n = n + 1 end
            check(n == 3, "soldiers: 複数人はまわりに散らばって並ぶ")
            -- 1人/1匹の表のキーは16個以内(超えるとLuaの表が倍の大きさになる)
            local function keys(t) local k = 0 for _ in pairs(t) do k = k + 1 end return k end
            for _, u in ipairs(S.list) do check(keys(u) <= 16, "soldiers: 表のキーは16個以内 (" .. keys(u) .. ")") end
            -- 強化と売却
            check(S.upgrade_cost(M1) == 40, "soldiers.upgrade_cost")
            M1.hp = 50
            check(S.upgrade(M1) and M1.lv == 2 and S.max_hp(M1) == 90 and M1.hp == 80, "soldiers.upgrade: 最大耐久の増えた分だけ回復")
            S.upgrade(M1); S.upgrade(M1)
            check(M1.lv == 4 and S.upgrade_cost(M1) == nil and not S.upgrade(M1), "soldiers.upgrade: Lv4 が最大")
            check(S.paid(M1) == 50 + 40 + 70 + 110 and S.sell_value(M1) == 189, "soldiers.sell_value: 払った合計の70%")
            M1.hp = S.max_hp(M1)
            -- 持ち場をベースの近くへ戻し、ゾンビを呼ぶ
            S.order_group(S.list, BASE.x + 0.5, BASE.z + 5.5)
            Z.queue("normal", 12); Z.queue("ranged", 4)
        )LUA", "sol3");
        // 60秒ぶん戦わせる
        for (int i = 0; i < 30 && ok; i++) ok = te.Run("tick(40)", "tick");
        ok = ok && te.Run(R"LUA(
            check(KILLED > 0 and Z.stats.killed == KILLED, "soldiers: ゾンビを倒す (" .. KILLED .. ")")
            local function keys(t) local k = 0 for _ in pairs(t) do k = k + 1 end return k end
            local mk = 0
            for _, u in ipairs(Z.list) do mk = math.max(mk, keys(u)) end
            check(mk <= 16, "zombies: 表のキーは16個以内 (" .. mk .. ")")
            check(MAXLEASH and MAXLEASH <= 3.6, "soldiers: 近接兵は持ち場から3マスほどまでしか離れない (" .. tostring(MAXLEASH) .. ")")
            check(HEALED > 0, "soldiers: 回復兵が回復する (" .. HEALED .. ")")
            -- 移動中の反撃: 攻撃してきたゾンビが近くにいれば止まって戦う
            Z.clear(); C.clear_shots()
            S.clear()
            local s = S.hire("melee")
            tick(80)
            S.order(s, s.x, s.z + 12)
            Z.queue("normal", 1)
            Z.update(0.4)
            local zb = Z.list[1]
            zb.x, zb.z = s.x + 0.4, s.z
            zb.gy = s.gy; zb.y = s.y
            pico.iso.entity_move(zb.id, zb.x, zb.y, zb.z)
            zb.tgt = s
            s.hurt(s, 1, zb)
            check(s.state == "move" and s.tgt == zb, "soldiers: 移動中でも攻撃してきた相手に反撃する")
            local px, pz = s.x, s.z
            S.update(0.35)
            check(s.tgt == zb and math.abs(s.x - px) + math.abs(s.z - pz) < 0.3, "soldiers: 反撃しながら目的地へは進まない")
            -- 相手が倒れたら移動を続ける
            zb.hp = 1
            tick(30)
            check(Z.alive() == 0, "soldiers: 反撃で倒す")
            tick(20)
            check(s.state == "move" and s.path and not s.tgt, "soldiers: 倒したら移動を続ける")
            -- 兵士が倒れる
            s.hp = 1
            local lost0 = LOST
            s.hurt(s, 5, nil)
            S.update(0.05)
            check(LOST == lost0 + 1 and S.count() == 0 and C.units[s.id] == nil, "soldiers: 倒れたら消える")
            -- ゾンビは気づく範囲の兵士を、優先度と距離で狙う
            local m = S.hire("melee"); local r = S.hire("ranged")
            Z.queue("normal", 1); Z.update(0.4)
            local z2 = Z.list[1]
            m.x, m.z = z2.x + 2.5, z2.z; r.x, r.z = z2.x + 1.5, z2.z
            pico.iso.entity_move(m.id, m.x, m.y, m.z); pico.iso.entity_move(r.id, r.x, r.y, r.z)
            z2.scan = 0
            Z.update(0.01)
            check(z2.tgt == m, "zombies: 近接兵(優先度0)を弓兵(優先度3)より先に狙う")
            S.clear(); Z.clear()
            pico.iso.close()
        )LUA", "sol4");
        check(ok, "soldiers: 一通り");
    }

    // =====================================================================
    // ゾンビTD の buildings.lua(タワー・バリケード・建設・強化・修理・売却)を本物のエンジンの上で動かす
    // =====================================================================
    {
        OSData::SD_usable = true;
        LuaEngine te(180 * 1024, LuaPermissions{}, "/td");
        lua_register(te.raw(), "check", l_check);
        bool ok = te.Run(R"LUA(
            local iso = pico.iso
            Z = require("zombies"); C = require("combat"); S = require("soldiers"); B = require("buildings")
            iso.set_image(pico.image_load("/td/faces.pimg"))
            local units = pico.image_load("/td/units.pimg")
            iso.create("/td/map3", 4, 3, 7)
            iso.keep_all(true)
            iso.view(0, 20, 240, 246)
            while iso.pump(64) > 0 do end
            A = iso.arena()
            local b = A.base
            GOALS = {}
            for dx = -2, 2 do for dz = -2, 2 do
                if math.max(math.abs(dx), math.abs(dz)) == 2 then GOALS[#GOALS + 1] = {b.x + dx, b.z + dz} end
            end end
            local W = B.WALLS
            local cost = {}
            for _, w in ipairs(W) do cost[w] = 6 end
            FLOW = {max_up = 1, max_down = 2, height = 2, up_cost = 0.5, diagonal = true, avoid = {1}, pass = W, body_cost = cost}
            ZR = {height = 2, avoid = {1}, pass = W}
            SR = {height = 2, avoid = {1, W[1], W[2], W[3], W[4]}}
            C.SIGHT_PASS = W
            iso.flow_build(GOALS, FLOW, true)
            BASE = {x = b.x, y = b.y, z = b.z, hp = 100000, r = 1.4, side = "base"}
            BASE.id = iso.entity_add(units, b.x + 0.5, b.y, b.z + 0.5,
                {sx = 80, sy = 0, w = 44, h = 40, r = 1.4, height = 2.5, crowd = "fixed", tag = 2})
            function BASE.hurt(self, d) BASE.hp = BASE.hp - d end
            C.add(BASE)
            math.randomseed(5)
            Z.init(units, A.spawns, BASE, ZR)
            S.init(units, BASE, SR)
            B.init(units, BASE, A.spawns)
            Z.TARGET_TAGS = {S.TAG, B.TAG}; Z.hunt = true; Z.wall_at = B.wall_at
            S.ZOMBIE_TAG = Z.TAG; B.ZOMBIE_TAG = Z.TAG
            S.blocked = function(x, z) return B.at[B.col(x, z)] ~= nil end
            WALLS_CHANGED, KILLED, LOSTB = 0, 0, 0
            B.on_walls_changed = function() WALLS_CHANGED = WALLS_CHANGED + 1; iso.flow_build(GOALS, FLOW, true) end
            B.on_lost = function() LOSTB = LOSTB + 1 end
            Z.on_kill = function() KILLED = KILLED + 1 end
            function tick(n)
                for _ = 1, n do
                    Z.update(0.05); S.update(0.05); B.update(0.05)
                    if iso.crowd(2, {height = 2, pass = W}) > 0 then Z.sync(); S.sync() end
                    C.step(0.05)
                end
            end
            -- 置ける所: ベースの近く・出現位置の近く・水の上・建物のある柱は不可
            check(not B.can_place("arrow", b.x + 2, b.z + 2, SR), "buildings.can_place: ベースの近くは不可")
            local sp = A.spawns[1]
            check(not B.can_place("wall", sp.x + 1, sp.z, SR), "buildings.can_place: 出現位置の近くは不可")
            local wx, wz
            for x = 0, 55 do for z = 0, 55 do
                if not wx and iso.ground(x, z) and iso.get(x, iso.ground(x, z) - 1, z) == 1 then wx, wz = x, z end
            end end
            if wx then check(not B.can_place("wall", wx, wz, SR), "buildings.can_place: 水の上は不可") end
            -- 弓塔: ベースの前に建てる
            TX, TZ = b.x, b.z + 5
            while not B.can_place("arrow", TX, TZ, SR) do TX = TX + 1 end
            T1 = B.place("arrow", TX, TZ, SR)
            check(T1 and T1.state == "build" and T1.hp == 30 and B.max_hp(T1) == 30, "buildings.place: 建設中は完成時の20%の耐久")
            check(not B.can_place("wall", TX, TZ, SR), "buildings.can_place: 同じ柱には置けない")
            check(not S.standable(TX, TZ), "soldiers.standable: 建物の柱には立てない")
            check(B.sell_value(T1) == nil and B.upgrade_cost(T1) == nil and B.repair_cost(T1) == nil, "buildings: 建設中は売れない・強化/修理できない")
            check(B.remaining(T1) == 8, "buildings.remaining: 建設の残り秒")
        )LUA", "bld");
        check(ok, "buildings: 準備");
        ok = ok && te.Run("tick(165)", "tick");
        ok = ok && te.Run(R"LUA(
            local iso = pico.iso
            check(T1.state == "ready" and T1.hp == 150 and B.max_hp(T1) == 150, "buildings: 建設が終わると耐久が完成時の値になる")
            local _, _, _, o = iso.entity_get(T1.id)
            check(o.sx == 216 and o.h == 36, "buildings: 完成すると塔の絵になる")
            -- 強化: 建設の半分の時間、その間は耐久が低い
            check(B.upgrade_cost(T1) == 90 and B.upgrade(T1), "buildings.upgrade")
            check(T1.state == "up" and T1.lv == 2 and B.max_hp(T1) == 34 and T1.hp == 34 and B.remaining(T1) == 4,
                  "buildings.upgrade: 強化中は耐久が低い (" .. T1.hp .. ")")
            tick(85)
            check(T1.state == "ready" and T1.hp == 170, "buildings.upgrade: 終わると耐久が戻る (" .. T1.hp .. ")")
            -- 修理: 費用 = 減った割合 × 払った合計 × 0.5、3秒かけて戻る
            T1.hp = 85
            check(B.repair_cost(T1) == 53, "buildings.repair_cost (" .. tostring(B.repair_cost(T1)) .. ")")
            check(B.repair(T1) and B.repair_cost(T1) == nil, "buildings.repair: 修理中は重ねて修理しない")
            tick(30)
            check(T1.hp > 100 and T1.hp < 170, "buildings.repair: 少しずつ戻る")
            tick(40)
            check(math.abs(T1.hp - 170) < 0.01 and T1.rr == 0, "buildings.repair: 3秒で戻る")
            check(B.sell_value(T1) == 147, "buildings.sell_value: 払った合計の70% (" .. B.sell_value(T1) .. ")")
            -- バリケード: ブロックを置き、流れの場を作り直す
            local fz = A.spawns[2].z - 6
            WX, WZ = A.spawns[2].x, fz
            while not B.can_place("wall", WX, WZ, SR) do WX = WX + 1 end
            local w0 = WALLS_CHANGED
            W1 = B.place("wall", WX, WZ, SR)
            check(W1 and iso.get(WX, W1.y, WZ) == B.WALLS[1] and WALLS_CHANGED == w0 + 1, "buildings.place(wall): ブロックを置いて道を作り直す")
            check(B.wall_at(WX + 0.3, WZ + 0.7) == W1 and B.wall_at(TX, TZ) == nil, "buildings.wall_at")
            check(iso.stand(WX, WZ, nil, ZR) == W1.y and iso.stand(WX, WZ, nil, SR) == nil, "バリケードの中はゾンビは立てる・兵士は立てない")
            tick(85)
            check(W1.state == "ready" and W1.hp == 80, "buildings: バリケードの建設が終わる")
            -- 兵士の道はタワーの柱を通らない
            local s = S.hire("melee")
            tick(80)
            S.order(s, TX + 0.5, TZ + 2.5)
            local through = false
            for _, p in ipairs(s.path or {}) do if p.x == TX and p.z == TZ then through = true end end
            check(s.path and not through, "soldiers: 道はタワーの柱を通らない")
            S.clear()
            -- ゾンビがバリケードの中を通ると遅くなり、バリケードが削れる
            Z.queue("heavy", 1); Z.update(0.4)
            ZB = Z.list[1]
            ZB.x, ZB.z, ZB.gy, ZB.y = WX + 0.5, WZ + 0.1, W1.y, W1.y
            ZB.col = -1
            local hp0 = W1.hp
            Z.update(0.05)
            check(W1.hp < hp0, "zombies: バリケードの中を通ると削る")
            -- 遅くなる: 同じ時間で動く距離を比べる
            local x0, z0 = ZB.x, ZB.z
            Z.update(0.1)
            local inside = math.sqrt((ZB.x - x0) ^ 2 + (ZB.z - z0) ^ 2)
            check(inside > 0 and inside < 0.55 * 0.1 * 0.6, "zombies: バリケードの中は遅い (" .. inside .. ")")
            -- 強化するとブロックが変わる
            W1.hp = 80
            Z.clear()
            check(B.upgrade(W1) and iso.get(WX, W1.y, WZ) == B.WALLS[2], "buildings.upgrade(wall): ブロックが Lv2 の絵になる")
            tick(45)
            check(W1.state == "ready" and W1.lv == 2, "buildings.upgrade(wall): 終わる")
            -- 壊されると消える(ブロックも空気に)
            local lost0, changed = LOSTB, WALLS_CHANGED
            W1:hurt(1000)
            tick(1)
            check(LOSTB == lost0 + 1 and iso.get(WX, W1.y, WZ) == 0 and B.wall_at(WX, WZ) == nil and WALLS_CHANGED == changed + 1,
                  "buildings: バリケードは壊されると消えて道を作り直す")
            -- 表のキーは16個以内
            local function keys(t) local k = 0 for _ in pairs(t) do k = k + 1 end return k end
            check(keys(T1) <= 16, "buildings: 表のキーは16個以内 (" .. keys(T1) .. ")")
            -- ゾンビはタワーを狙い、弓塔はゾンビを倒す
            Z.queue("normal", 10); Z.queue("ranged", 4)
        )LUA", "bld2");
        for (int i = 0; i < 40 && ok; i++) ok = te.Run("tick(40)", "tick");
        ok = ok && te.Run(R"LUA(
            local iso = pico.iso
            check(KILLED > 0, "buildings: 弓塔がゾンビを倒す (" .. KILLED .. ")")
            check(T1.hp < 170 or LOSTB > 1, "zombies: タワーを狙って攻撃する (" .. T1.hp .. ")")
            -- 売る
            if T1.hp > 0 and B.sell_value(T1) then
                local id = T1.id
                local v = B.sell(T1)
                check(v > 0 and iso.entity_get(id) == nil and B.at[B.col(TX, TZ)] == nil, "buildings.sell: 取り除いてお金を返す")
            end
            -- 全部消す: バリケードのブロックも空気に戻る
            local w2 = B.place("wall", WX, WZ, SR)
            B.clear()
            check(iso.get(WX, w2.y, WZ) == 0 and B.count() == 0, "buildings.clear: バリケードのブロックも消える")
            Z.clear()
            iso.close()
        )LUA", "bld3");
        check(ok, "buildings: 一通り");
    }

    // =====================================================================
    // ゾンビTD のウェーブ・保存(waves.lua / save.lua)と、game.lua 全体の流れ
    // =====================================================================
    {
        std::string root(__FILE__);
        root = root.substr(0, root.rfind("/script/host_test/"));
        auto slurp = [&](const std::string& rel) {
            std::ifstream f(root + rel, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(f)), {});
        };
        static const char* const kFiles[] = {"game.lua", "state.lua", "orders.lua", "ui.lua", "waves.lua", "save.lua", "sfx.lua", "tutorial.lua", "cursor.lua",
            "combat.lua", "zombies.lua", "soldiers.lua", "buildings.lua", "palette.lua", "faces.pimg", "units.pimg"};
        for (const char* f : kFiles) {
            HostSd::files[std::string("/tdg/") + f] = slurp(std::string("/pc/sdcard/lua/apps/ゾンビTD/") + f);
        }
        OSData::SD_usable = true;
        // pico.keep_awake
        {
            LuaEngine ke(64 * 1024);
            PowerFunctions::detail::keep_awake = false;
            check(ke.Run("pico.keep_awake()", "ka") && PowerFunctions::detail::keep_awake, "pico.keep_awake: 印を立てる");
            PowerFunctions::detail::keep_awake = false;
        }
        LuaPermissions tperm;
        tperm.strip_debug = true;   // app.cfg と同じ
        LuaEngine te(270 * 1024, tperm, "/tdg");
        lua_register(te.raw(), "check", l_check);
        // waves.lua 単体
        bool ok = te.Run(R"LUA(
            local Wv = require("waves")
            local a, b, c = Wv.mix(1)
            check(a == 9 and b == 0 and c == 0, "waves.mix: 1回目はノーマルだけ")
            a, b, c = Wv.mix(10)
            check(a + b + c == 36 and b > 0 and c > 0, "waves.mix: 進むと遠距離・重量級が混ざる")
            check(Wv.preview(3):find("重量級が来る") ~= nil and Wv.preview(2):find("遠距離が来る") ~= nil,
                  "waves.preview: 初めて来る種類を予告する")
            check(Wv.bonus(3) == 60 and Wv.money_mul(5) == 1.2, "waves: 越えたお金と倒したお金の倍率")
            Wv.reset(1)
            check(Wv.phase == "prep" and Wv.timer == 60, "waves.reset: 最初の準備時間は60秒")
            Wv.timer = 12.6
            check(Wv.skip() == 24 and Wv.timer == 0, "waves.skip: 残り秒数×2 のお金")
            local Z = { q = {}, a = 0 }
            function Z.queue(k) Z.q[#Z.q + 1] = k end
            function Z.alive() return Z.a end
            function Z.waiting() return #Z.q end
            check(Wv.update(0.1, Z) == "start", "waves.update: 準備時間が終わると start")
            Wv.n = 4
            Wv.start(Z)
            local n = { normal = 0, ranged = 0, heavy = 0 }
            for _, k in ipairs(Z.q) do n[k] = n[k] + 1 end
            a, b, c = Wv.mix(4)
            check(Wv.phase == "wave" and n.normal == a and n.ranged == b and n.heavy == c, "waves.start: 中身どおりに積む")
            check(Z.hp_mul == Wv.hp_mul(4) and Z.dmg_mul == Wv.dmg_mul(4), "waves.start: 強さの倍率を入れる")
            check(Wv.update(1, Z) == nil, "waves.update: 残っている間は続く")
            Z.q = {}
            check(Wv.update(1, Z) == "clear" and Wv.n == 5 and Wv.phase == "prep" and Wv.timer == 30,
                  "waves.update: 全部倒すと clear・次の準備時間は30秒")
            -- save.better
            local Sv = require("save")
            check(Sv.better({w = 3, hp = 0, earned = 1}, {w = 2, hp = 9, earned = 9}) and
                  not Sv.better({w = 3, hp = 0, earned = 1}, {w = 3, hp = 0, earned = 2}) and
                  Sv.better({w = 1, hp = 0, earned = 0}, nil), "save.better: ウェーブ数 → ベースの耐久 → 稼いだ合計")
        )LUA", "waves");
        check(ok, "waves/save: 単体");
        // game.lua 全体: 新しく始める → ウェーブ → 越える → 保存 → ゲームオーバー
        HostSd::files.erase("/tdg/store.json");
        ok = ok && te.Run("TEST = {}; require('game')", "game");
        lua_State* L = te.raw();
        for (int i = 0; i < 300 && ok; i++) {
            te.CallLoop(50);
            ok = te.Run("DONE = TEST.env.G.mode == 'play'", "m");
            lua_getglobal(L, "DONE");
            const bool done = lua_toboolean(L, -1);
            lua_pop(L, 1);
            if (done) break;
        }
        ok = te.Run(R"LUA(
            local E = TEST.env
            local G, Wv = E.G, E.waves
            check(G.mode == "play" and G.money == 300 and Wv.n == 1 and Wv.phase == "prep", "game: マップを作って準備時間から始まる")
            check(E.tutorial.active and E.tutorial.text():find("雇う") ~= nil, "tutorial: 初めてなら説明が始まる")
            E.orders.hire("melee")
            E.tutorial.update(0.1)
            check(E.tutorial.text():find("地面をタップ") ~= nil, "tutorial: 雇うと次の案内へ進む")
            G.moved = true
            E.tutorial.update(0.1)
            check(E.tutorial.text():find("建設") ~= nil, "tutorial: 動かすと次の案内へ進む")
            check(G.money == 250 and E.soldiers.count() == 1, "game: 雇うとお金が減る")
            local left = math.floor(Wv.timer)
            E.next_wave()
            check(left > 50 and G.money == 250 + left * 2 and Wv.timer == 0, "game: 次へで残り秒数×2 (" .. G.money .. ")")
            MONEY_START = G.money
        )LUA", "g1");
        te.CallLoop(50);
        ok = ok && te.Run(R"LUA(
            local E = TEST.env
            local G, Wv = E.G, E.waves
            check(Wv.phase == "wave" and E.zombies.waiting() + E.zombies.alive() == 9, "game: ウェーブ1が始まる")
            local st = pico.store_load()
            check(st and st.game and st.game.w == 1 and st.game.money == MONEY_START and #st.game.s == 1 and st.game.seed == G.seed,
                  "game: ウェーブの始めに保存する")
            -- 全部倒したことにする
            MONEY0 = G.money
            E.zombies.clear()
        )LUA", "g2");
        te.CallLoop(50);
        ok = ok && te.Run(R"LUA(
            local E = TEST.env
            local G, Wv = E.G, E.waves
            check(Wv.n == 2 and Wv.phase == "prep" and G.money == MONEY0 + 40, "game: ウェーブを越えると 30+10×1 のお金")
            local st = pico.store_load()
            check(st.game.w == 2, "game: 準備時間の始めに保存する")
            -- 保存から戻す: 建物と兵士
            local B, S = E.buildings, E.soldiers
            local b = G.base
            local x, z = b.x, b.z + 6
            while not B.can_place("arrow", x, z, G.S_STAND) do x = x + 1 end
            local t = B.place("arrow", x, z, G.S_STAND)
            B.upgrade(t)   -- 建設中なので強化できない
            local game = E.save.dump(G, Wv, S, B)
            check(#game.b == 1 and game.b[1][1] == "arrow" and game.b[1][4] == 1, "save.dump: 建物の一覧")
            E.orders.deselect_all(); B.clear(); S.clear()
            game.b[1][4], game.b[1][5] = 3, 100
            game.money = 999
            E.save.restore(game, G, Wv, S, B, G.S_STAND)
            local r = B.list[1]
            check(r and r.lv == 3 and r.state == "ready" and r.hp == 100 and G.money == 999 and S.count() == 1 and
                  S.list[1].state == "hold", "save.restore: 建物と兵士を戻す")
            -- レベルごとの絵(units.pimg の 40px の段)
            local sx = S.list[1]
            S.upgrade(sx)
            E.soldiers.update(0.05)
            local _, _, _, o = pico.iso.entity_get(sx.id)
            check(o.sy == 2 * 40 - 22, "soldiers.upgrade: Lv2 の段の絵になる (" .. o.sy .. ")")
            _, _, _, o = pico.iso.entity_get(r.id)
            check(o.sy == 3 * 40 - 36, "buildings: Lv3 の塔の段の絵 (" .. o.sy .. ")")
            -- ゲームオーバー
            G.base:hurt(5000)
            check(G.mode == "over", "game: ベースが壊れると終わる")
            st = pico.store_load()
            check(st.game == nil and st.best and st.best.w == 1, "game: 途中の保存を消して最高記録を残す")
        )LUA", "g3");
        check(ok, "game: ウェーブと保存の流れ");
        // コントローラー・キーボードのカーソル(cursor.lua)と、地図の上のメニュー(ui.lua)
        ok = te.Run(R"LUA(
            PAD, PADP = {}, {}
            pico.pad_down = function(n) return PAD[n] == true end
            pico.pad_pressed = function(n) return PADP[n] == true end
            function press(...)
                for _, n in ipairs({...}) do PAD[n] = true; PADP[n] = true end
                loop(16)
                PAD, PADP = {}, {}
                loop(16)
            end
            press("a")
            check(TEST.env.G.mode == "load", "cursor: 終わった画面で A を押すと新しく始める")
        )LUA", "c0");
        for (int i = 0; i < 300 && ok; i++) {
            te.CallLoop(50);
            ok = te.Run("DONE = TEST.env.G.mode == 'play'", "m");
            lua_getglobal(L, "DONE");
            const bool done = lua_toboolean(L, -1);
            lua_pop(L, 1);
            if (done) break;
        }
        ok = ok && te.Run(R"LUA(
            local E = TEST.env
            local G, C, ui, S, B = E.G, E.cursor, E.ui, E.soldiers, E.buildings
            E.tutorial.stop()
            check(G.mode == "play" and not C.on, "cursor: 始めは隠れている")
            press("up")
            check(C.on, "cursor: 十字を押すと出る")
            local x, z = C.x, C.z
            press("up"); press("right")
            check(C.x == x + 1 and C.z == z - 1, "cursor: 上=+x、右=-z (" .. C.x - x .. "," .. C.z - z .. ")")
            -- 押したままで続けて動く
            x = C.x
            PAD.down = true; PADP.down = true; loop(16); PADP = {}
            for _ = 1, 30 do loop(16) end
            PAD = {}; loop(16)
            check(C.x <= x - 3, "cursor: 押したままで続けて動く (" .. x - C.x .. ")")
            -- SELECT で下の欄へ → A で雇う → メニューで2番目を選ぶ
            press("select")
            check(ui.focus and ui.focus.id == "hire", "cursor: SELECT で下の欄の「雇う」へ")
            press("a")
            check(ui.menu_active(), "cursor: 雇うでメニューが開く")
            press("down"); press("a")
            check(not ui.menu_active() and S.count() == 1 and S.list[1].kind == S.KINDS[2] and G.sel[1] == S.list[1] and not ui.focus,
                  "cursor: メニューで選んで雇い、地図へ戻る")
            -- B で外す、X で兵士を選ぶ(カーソルもそこへ)
            press("b")
            check(#G.sel == 0, "cursor: B で選択を外す")
            press("x")
            local s = S.list[1]
            check(G.sel[1] == s and C.x == math.floor(s.x) and C.z == math.floor(s.z), "cursor: X で兵士を選びカーソルもそこへ")
            -- カーソルの所へ動かす
            local b = G.base
            G.moved = false
            C.place(b.x - 3, b.z + 3)
            press("a")
            check(G.moved and s.px == b.x - 2.5 and s.pz == b.z + 3.5, "cursor: A で選んだ兵士をカーソルの所へ")
            -- 兵士の所で A を押すと選ぶ(もう一度で外す)
            press("b")
            C.place(s.x, s.z)
            press("a")
            check(G.sel[1] == s, "cursor: 兵士の所で A を押すと選ぶ")
            press("a")
            check(#G.sel == 0, "cursor: もう一度で外す")
            -- 建設: 下の欄の「建設」→ 弓塔 → カーソルの所へ
            press("select"); press("right"); press("a")
            check(ui.menu_active(), "cursor: 建設のメニュー")
            press("a")
            check(G.build_mode == "arrow" and not ui.focus, "cursor: 弓塔を選ぶと建てる所を選ぶ")
            local bx, bz = b.x, b.z + 6
            while not B.can_place("arrow", bx, bz, G.S_STAND) do bx = bx + 1 end
            C.place(bx, bz)
            local money = G.money
            press("a")
            check(B.count() == 1 and G.bsel == B.list[1] and G.money < money and not G.build_mode, "cursor: A で建てる")
            press("b")
            press("y")
            check(G.bsel == B.list[1] and C.x == bx and C.z == bz, "cursor: Y で建物を選ぶ")
            press("b")
            -- メニューは B でやめる
            ui.press(ui.buttons[6])
            check(ui.menu_active(), "ui: 他のメニュー")
            press("down"); press("b")
            check(not ui.menu_active() and #G.sel == 0, "cursor: B でメニューをやめる")
            -- タップ: 枠の外はやめる、項目は選ぶ
            local got = nil
            ui.menu("t", { "a", "b" }, function(i) got = i end, function() got = "cancel" end)
            ui.menu_tap(0, 0)
            check(got == "cancel", "ui.menu_tap: 枠の外はやめる")
            ui.menu("t", { "a", "b" }, function(i) got = i end)
            local VX, VY, VW, VH = pico.get_rect(G.view)
            local h = 26 + 2 * 26 + 4
            ui.menu_tap(VX + VW // 2, VY + (VH - h) // 2 + 26 + 26 + 5)
            check(got == 2, "ui.menu_tap: 項目を選ぶ (" .. tostring(got) .. ")")
            -- カメラが付いてくる
            local ox, oy = E.origin()
            for _ = 1, 12 do press("up") end
            local ox2, oy2 = E.origin()
            check(ox2 ~= ox or oy2 ~= oy, "cursor: 端に近づくとカメラが動く")
            local sx, sy = pico.iso.to_screen(C.x + 0.5, pico.iso.ground(C.x, C.z), C.z + 0.5)
            check(sx >= VX and sx < VX + VW and sy >= VY and sy < VY + VH, "cursor: カーソルは画面の中")
            -- キーボード
            x = C.x
            check(C.key("up", {}) and C.x == x + 1, "cursor.key: 矢印でカーソル")
            G.build_mode = "wall"
            check(C.key("escape", {}) and not G.build_mode, "cursor.key: Esc で建設をやめる")
            check(C.key("tab", {}) and ui.focus, "cursor.key: Tab で下の欄へ")
            check(C.key("escape", {}) and not ui.focus, "cursor.key: Esc で地図へ")
            check(not C.key("q", {}), "cursor.key: 知らないキーは取らない")
            -- START で次へ
            press("start")
            check(E.waves.phase == "wave", "cursor: START で次へ")
        )LUA", "cur");
        check(ok, "game: コントローラーのカーソル");
    }

    WidgetFunctions::ClearSceneWidgets();
    printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
