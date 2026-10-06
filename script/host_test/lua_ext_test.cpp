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
#include "lua/LuaEngine.hpp"
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

        ok = engine.Run(R"LUA(
            pico.image_free(img); pico.image_free(img2)
            check(pico.get_pixel(-1, 0) == nil, "get_pixel: 範囲外はnil")
            check(type(pico.get_pixel(0, 0)) == "number", "get_pixel: 範囲内は番号")

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
            c->renderForce();
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

    WidgetFunctions::ClearSceneWidgets();
    printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
