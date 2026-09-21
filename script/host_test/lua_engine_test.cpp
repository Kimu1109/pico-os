// LuaEngine(Lua<->C++バインディング本体)を実際のウィジェット層と繋げて
// 動かすテスト。単体では正しく見えるWidgetProperty/WidgetFactory/ErrorFunctionsの
// 組み合わせが、Lua経由で実際に噛み合うかをここで初めて確認する。
//
// 確認する導線:
//   pico.create → WidgetFunctions::widgetsへ登録 → pico.set/get → WidgetProperty
//   pico.on → タップ相当のcauseOnPressStart() → Luaコールバックが呼ばれる
//   pico.add_child → 生成順に依らず正しい重なり順に入る(順序バグの回帰確認)
//   pico.destroy → ScrollContainerの子を個別に破棄しても二重解放しない
//                  (ScrollContainer::removeChild()追加の回帰確認)
//   スクリプトの構文/実行時エラー → ErrorFunctions::ShowFatal()でダイアログが出る
//   極小メモリ予算 → LuaEngineの構築自体が安全に失敗する(クラッシュしない)
//   CallSetup/CallLoop → Arduino風setup()/loop(dt)の呼び出しと、
//                         loop()のエラー後は以降呼ばれなくなる安全弁
//   pico.draw_*/fill_*/clear_rect/draw_text → OSData::frame(スタブ)への呼び出しが
//                         クラッシュしないことと、描いた範囲がPICO_GFX::MarkDirty()
//                         へ正しく渡ることの確認
//   pico.create("Canvas") + pico.on(id,"render",fn) → render()経由でLuaの
//                         renderコールバックが呼ばれること、Canvas以外への
//                         "render"登録はエラーになること
//   pico.invalidate/pico.mark_dirty → 前者はウィジェットの画面矩形を、後者は
//                         指定した矩形をそのままPICO_GFX::MarkDirty()へ渡すこと
//   pico.image_load/draw_image/image_size/image_free → `.pimg`を固定長スロットへ
//                         デコードして持つ、ウィジェットを介さない画像ハンドルの
//                         発行・描画・解放。ハンドルのgeneration方式(解放後の
//                         使い回しで古いハンドルが新しい画像を指さないこと)、
//                         スロット数/合計バイト数の上限、SD無し・不正な`.pimg`の
//                         失敗経路(nil)を確認する
//   pico.show_message/show_input/show_file_save/show_file_select/show_color →
//                         ダイアログが実際に生成されdialog_rootsへ乗ること、
//                         pico.on(id,"closed",fn)がis_okを伴って呼ばれること、
//                         pico.get(id,"text"/"path"/"value")で結果が読めること、
//                         pico.on()を呼ばなくても閉じたら自動的にDestroyLater
//                         されること、"closed"イベントがダイアログ以外だと
//                         エラーになることを確認する
//   pico.http_request/http_cancel → 実ソケットに触れない範囲(不正なメソッド/URL/
//                         https/送信ボディの上限超過/同時実行数の上限)での
//                         早期拒否がすべてfalseで返ること(luaL_errorにしない)
#include "lua/LuaEngine.hpp"
#include "gui/widgets/Widget.hpp"
#include "gui/widgets/WidgetRegistry.hpp"
#include "gui/widgets/dialogs/MsgDialog.hpp"
#include "gui/widgets/dialogs/InputDialog.hpp"
#include "gui/widgets/dialogs/FileSaveDialog.hpp"
#include "gui/widgets/dialogs/FileSelectDialog.hpp"
#include "gui/widgets/dialogs/ColorDialog.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/Keyboard_Functions.hpp"
#include "OS_Data.hpp"
#include <algorithm>
#include <cstdio>
#include <string>

// ---- モック(widget_factory_test.cppと同じ方針) ----
// MarkDirty()だけは直接描画テストのために最後に渡された矩形を記録する(他のテストは
// 呼び出し回数/中身を見ないのでNoOpのままでも影響しない)
static Rect g_last_dirty{0, 0, 0, 0};
void PICO_GFX::MarkDirty(const Rect& r){ g_last_dirty = r; }
void PICO_GFX::Setup(){}
void PICO_GFX::FlushDirty(){}
void PICO_GFX::DrawDialogBackground(){}
void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}
void KeyboardFunctions::RegisterInputTarget(ITextInputTarget*){}
void KeyboardFunctions::UnregisterInputTarget(ITextInputTarget*){}
void KeyboardFunctions::HideAll(){}

static int failures = 0;
static void check(bool cond, const char* label) {
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if (!cond) failures++;
}

// テスト用の最小`.pimg`を組み立てる(script/generate_pimg.pyのフォーマット参照)。
// 全ピクセルを単一の色indexで塗りつぶすだけの最小ボディを1ランで表現する
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

// Luaスクリプト側からのcheck()。C++側のcheck()と同じくfailuresへ積む
static int l_check(lua_State* L) {
    const bool cond = lua_toboolean(L, 1);
    const char* label = luaL_optstring(L, 2, "(no label)");
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if (!cond) failures++;
    return 0;
}

static const char* kScript = R"LUA(
-- ウィジェット生成とプロパティのget/set往復
btn_id = pico.create("Button")
pico.set(btn_id, "text", "hi")
check(pico.get(btn_id, "text") == "hi", "pico.set/get: textの往復")

pico.set(btn_id, "x", 10)
pico.set(btn_id, "y", 20)
check(pico.get(btn_id, "x") == 10, "pico.get: x")
check(pico.get(btn_id, "y") == 20, "pico.get: y")

-- NumberSlider.valueはfloat型プロパティだが、整数リテラルでも設定できること
slider_id = pico.create("NumberSlider")
pico.set(slider_id, "min_value", 0)
pico.set(slider_id, "max_value", 10)
pico.set(slider_id, "value", 5)
check(pico.get(slider_id, "value") == 5, "pico.set: floatプロパティへ整数リテラルを渡しても通る")

-- 異常系(pcallで捕捉して確認)
check(pcall(function() pico.create("NoSuchType") end) == false,
      "pico.create: 未知の種別はエラー")
check(pcall(function() pico.set(btn_id, "no_such_prop", 1) end) == false,
      "pico.set: 未知のプロパティはエラー")
check(pcall(function() pico.set(btn_id, "x", "not a number") end) == false,
      "pico.set: 型不一致はエラー")
check(pcall(function() pico.get(btn_id, "no_such_prop") end) == false,
      "pico.get: 未知のプロパティ名はエラー")
check(pico.get(btn_id, "value") == nil,
      "pico.get: 名前は有効だが対応していない組み合わせ(Buttonにvalueは無い)は例外ではなくnil")

-- コールバック登録(実際に鳴らすのはC++側からcauseOnPressStart()を呼んで確認する)
pressed_count = 0
last_pressed_id = nil
pico.on(btn_id, "press_start", function(id)
    pressed_count = pressed_count + 1
    last_pressed_id = id
end)

-- コンテナへの追加。わざと子→親の順で生成し、生成順に依存しないことを確認する
child_id = pico.create("Label")
container_id = pico.create("LayoutContainer")
pico.add_child(container_id, child_id)

-- ScrollContainerの子を個別にdestroyする経路(二重解放しないことをASanで確認する)
scroll_id = pico.create("ScrollContainer")
scroll_child_id = pico.create("Icon")
pico.add_child(scroll_id, scroll_child_id)
pico.destroy(scroll_child_id)

pico.log("lua_engine_test: スクリプト完了")
)LUA";

int main(){
    setvbuf(stdout, nullptr, _IONBF, 0); // LSanがリーク検出時に_exit()するとバッファが飛ぶため

    // ---- 極小予算: 構築自体が安全に失敗する ----
    {
        LuaEngine tiny(100);
        check(!tiny.valid(), "極小予算(100B): LuaEngineの構築が安全に失敗する(クラッシュしない)");
    }

    // ---- 本編 ----
    LuaEngine engine(64 * 1024);
    check(engine.valid(), "LuaEngine: 64KiB予算で構築成功");
    if (!engine.valid()) {
        printf("\nFAILED (failures=%d)\n", ++failures);
        return 1;
    }

    lua_pushcfunction(engine.raw(), l_check);
    lua_setglobal(engine.raw(), "check");

    const bool run_ok = engine.Run(kScript, "lua_engine_test");
    check(run_ok, "スクリプト全体の実行が成功する");

    lua_State* L = engine.raw();

    // ---- 生成されたウィジェットが実際にWidgetFunctionsへ登録されている ----
    lua_getglobal(L, "btn_id");
    const WidgetId btn_id = (WidgetId)lua_tointeger(L, -1);
    lua_pop(L, 1);

    Widget* btn = WidgetRegistry::Resolve(btn_id);
    check(btn != nullptr, "pico.create: WidgetRegistry::Resolve()で実体が引ける");
    check(std::find(WidgetFunctions::widgets.begin(), WidgetFunctions::widgets.end(), btn)
              != WidgetFunctions::widgets.end(),
          "pico.create: WidgetFunctions::widgetsへ登録されている(描画・当たり判定の対象になる)");

    // ---- コールバック: 実際のタップ相当(causeOnPressStart)を発火させる ----
    if (btn) btn->causeOnPressStart();

    lua_getglobal(L, "pressed_count");
    check((int)lua_tointeger(L, -1) == 1, "pico.on: タップでLuaコールバックが1回呼ばれる");
    lua_pop(L, 1);

    lua_getglobal(L, "last_pressed_id");
    check((WidgetId)lua_tointeger(L, -1) == btn_id,
          "pico.on: コールバック引数にタップされたウィジェットのidが渡る");
    lua_pop(L, 1);

    // ---- add_child: 生成順(子が先)に依らず、親より後ろ(=上)へ入る ----
    lua_getglobal(L, "container_id");
    const WidgetId container_id = (WidgetId)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getglobal(L, "child_id");
    const WidgetId child_id = (WidgetId)lua_tointeger(L, -1);
    lua_pop(L, 1);

    Widget* container = WidgetRegistry::Resolve(container_id);
    Widget* child = WidgetRegistry::Resolve(child_id);
    check(child != nullptr && child->getParent() == container,
          "pico.add_child: 親子関係が張られる");
    check(std::find(WidgetFunctions::widgets.begin(), WidgetFunctions::widgets.end(), child)
              == WidgetFunctions::widgets.end(),
          "pico.add_child: 子は一旦フラットリストから外れる(次フレームの再登録待ち)");

    // 実際のUpdateAll()が行う再登録(needs_children_update消化)を模す
    WidgetFunctions::Add(container);
    auto container_it = std::find(WidgetFunctions::widgets.begin(), WidgetFunctions::widgets.end(), container);
    auto child_it = std::find(WidgetFunctions::widgets.begin(), WidgetFunctions::widgets.end(), child);
    check(container_it != WidgetFunctions::widgets.end() && child_it != WidgetFunctions::widgets.end(),
          "pico.add_child: 再登録後は親子ともフラットリストにいる");
    check(child_it > container_it,
          "pico.add_child: 子は親より後ろ(=上)に描かれる位置に入る(生成順が子→親でも直る)");

    // ---- ScrollContainerの子を個別にdestroy → 二重解放しないこと ----
    lua_getglobal(L, "scroll_id");
    const WidgetId scroll_id = (WidgetId)lua_tointeger(L, -1);
    lua_pop(L, 1);
    Widget* scroll = WidgetRegistry::Resolve(scroll_id);
    check(scroll != nullptr, "pico.create: ScrollContainerも生成できる");

    WidgetFunctions::ProcessPendingDeletes(); // pico.destroy(scroll_child_id)を実際に消化する
    if (scroll) {
        WidgetFunctions::Destroy(scroll); // ここでchildren_に残った破棄済みポインタを
                                           // もう一度deleteしていたら二重解放でASanが検出する
    }
    check(true, "pico.destroy: ScrollContainerの子を個別に破棄しても二重解放しない(ASan確認)");

    // ---- エラー系: スクリプトの実行時エラーはErrorFunctions経由でダイアログが出る ----
    const size_t dialogs_before = WidgetFunctions::dialog_roots.size();
    const bool err_ok = engine.Run("error('わざとのエラー')", "err_test");
    check(!err_ok, "Run(): 実行時エラーのスクリプトはfalseを返す");
    check(WidgetFunctions::dialog_roots.size() == dialogs_before + 1,
          "Run(): エラー時にErrorFunctions::ShowFatal()経由でMsgDialogが1枚出る");
    if (WidgetFunctions::dialog_roots.size() > dialogs_before) {
        MsgDialog* dialog = static_cast<MsgDialog*>(WidgetFunctions::dialog_roots.back());
        dialog->causeOnClosed(true);
    }
    WidgetFunctions::ProcessPendingDeletes();

    // ---- 構文エラーも同様 ----
    const bool syntax_ok = engine.Run("this is not lua", "syntax_err_test");
    check(!syntax_ok, "Run(): 構文エラーのスクリプトもfalseを返す");
    if (!WidgetFunctions::dialog_roots.empty()) {
        MsgDialog* dialog = static_cast<MsgDialog*>(WidgetFunctions::dialog_roots.back());
        dialog->causeOnClosed(true);
    }
    WidgetFunctions::ProcessPendingDeletes();

    // ---- Arduino風 setup()/loop(dt) ----
    {
        const bool ok = engine.Run(R"LUA(
            setup_called = 0
            loop_called = 0
            last_dt = -1
            function setup()
                setup_called = setup_called + 1
            end
            function loop(dt)
                loop_called = loop_called + 1
                last_dt = dt
            end
        )LUA", "setup_loop_def");
        check(ok, "setup/loop: 定義スクリプトの実行が成功する");

        engine.CallSetup();
        lua_getglobal(L, "setup_called");
        check((int)lua_tointeger(L, -1) == 1, "CallSetup: setup()が1回呼ばれる");
        lua_pop(L, 1);

        engine.CallLoop(16);
        engine.CallLoop(17);
        lua_getglobal(L, "loop_called");
        check((int)lua_tointeger(L, -1) == 2, "CallLoop: 呼ぶたびにloop()が実行される");
        lua_pop(L, 1);
        lua_getglobal(L, "last_dt");
        check((int)lua_tointeger(L, -1) == 17, "CallLoop: dt引数がLua側へ渡る");
        lua_pop(L, 1);

        // setup/loopが定義されていなくてもno-op(クラッシュしない)であることを別のengineで確認
        LuaEngine engine2(64 * 1024);
        check(engine2.valid(), "setup/loop無し確認用: 2つ目のLuaEngineを構築");
        if (engine2.valid()) {
            const bool ok2 = engine2.Run("x = 1", "no_setup_loop");
            check(ok2, "setup/loop無しスクリプトの実行成功");
            engine2.CallSetup();
            engine2.CallLoop(10);
            check(true, "CallSetup/CallLoop: 未定義でもクラッシュしない(no-op)");
        }
    }

    // ---- loop()のエラーは1回で以降呼ばれなくなる(毎フレームダイアログ防止の安全弁) ----
    {
        const size_t dialogs_before2 = WidgetFunctions::dialog_roots.size();
        const bool ok = engine.Run(R"LUA(
            loop_err_calls = 0
            function loop(dt)
                loop_err_calls = loop_err_calls + 1
                error("わざとのloopエラー")
            end
        )LUA", "loop_err_def");
        check(ok, "loop()エラー用スクリプトの定義自体は成功する");

        engine.CallLoop(1);
        check(WidgetFunctions::dialog_roots.size() == dialogs_before2 + 1,
              "CallLoop: エラー時にErrorFunctions::ShowFatal()でダイアログが出る");
        engine.CallLoop(1);
        engine.CallLoop(1);
        lua_getglobal(L, "loop_err_calls");
        check((int)lua_tointeger(L, -1) == 1,
              "CallLoop: 一度エラーになったら以降呼ばれない(毎フレームダイアログ防止の安全弁)");
        lua_pop(L, 1);

        while (WidgetFunctions::dialog_roots.size() > dialogs_before2) {
            MsgDialog* dialog = static_cast<MsgDialog*>(WidgetFunctions::dialog_roots.back());
            dialog->causeOnClosed(true);
            WidgetFunctions::ProcessPendingDeletes();
        }
    }

    // ---- 直接描画: OSData::frame(スタブ)への呼び出しと、描いた範囲のMarkDirty()を確認 ----
    {
        bool ok = engine.Run("pico.draw_pixel(10, 20, 5)", "draw_pixel_test");
        check(ok, "pico.draw_pixel: エラーなく実行できる");
        check(g_last_dirty.x == 10 && g_last_dirty.y == 20 && g_last_dirty.w == 1 && g_last_dirty.h == 1,
              "pico.draw_pixel: 1x1のdirty矩形が登録される");

        ok = engine.Run("pico.draw_line(0, 0, 10, 20, 5)", "draw_line_test");
        check(ok, "pico.draw_line: エラーなく実行できる");
        check(g_last_dirty.x == 0 && g_last_dirty.y == 0 && g_last_dirty.w == 11 && g_last_dirty.h == 21,
              "pico.draw_line: 始点・終点のバウンディングボックスがdirty矩形になる");

        ok = engine.Run("pico.draw_rect(1, 2, 30, 40, 5)", "draw_rect_test");
        check(ok, "pico.draw_rect: エラーなく実行できる");
        check(g_last_dirty.x == 1 && g_last_dirty.y == 2 && g_last_dirty.w == 30 && g_last_dirty.h == 40,
              "pico.draw_rect: 指定した矩形がそのままdirtyになる");

        ok = engine.Run("pico.fill_rect(1, 2, 30, 40, 5)", "fill_rect_test");
        check(ok, "pico.fill_rect: エラーなく実行できる");

        ok = engine.Run("pico.draw_circle(50, 60, 10, 5)", "draw_circle_test");
        check(ok, "pico.draw_circle: エラーなく実行できる");
        check(g_last_dirty.x == 40 && g_last_dirty.y == 50 && g_last_dirty.w == 21 && g_last_dirty.h == 21,
              "pico.draw_circle: 半径ぶん広げた矩形がdirtyになる");

        ok = engine.Run("pico.fill_circle(50, 60, 10, 5)", "fill_circle_test");
        check(ok, "pico.fill_circle: エラーなく実行できる");

        ok = engine.Run("pico.clear_rect(1, 2, 30, 40)", "clear_rect_test");
        check(ok, "pico.clear_rect: 色を省略してもエラーなく実行できる(既定色PICO_BACKGROUND)");

        ok = engine.Run("pico.draw_text(5, 6, 'hi')", "draw_text_test");
        check(ok, "pico.draw_text: エラーなく実行できる(色/フォントサイズも省略可)");
        check(g_last_dirty.x == 5 && g_last_dirty.y == 6 && g_last_dirty.h > 0,
              "pico.draw_text: 描画位置を起点にした矩形がdirtyになる");

        // 右端ぎりぎり/画面外のx指定でも安全に何もしない(maxWidth<=0を弾く経路)
        ok = engine.Run("pico.draw_text(1000, 6, 'off screen')", "draw_text_offscreen_test");
        check(ok, "pico.draw_text: 画面外のx指定でもクラッシュせず何もしない");
    }

    // ---- LuaCanvas: pico.create("Canvas") + pico.on(id,"render",fn) ----
    WidgetId canvas_id = WidgetIdTools::Invalid();
    {
        const bool ok = engine.Run(R"LUA(
            canvas_id = pico.create("Canvas")
            pico.set(canvas_id, "x", 5)
            pico.set(canvas_id, "y", 6)
            pico.set(canvas_id, "w", 40)
            pico.set(canvas_id, "h", 30)
            render_calls = 0
            pico.on(canvas_id, "render", function(id)
                render_calls = render_calls + 1
                pico.fill_rect(0, 0, 1, 1, 0) -- renderコールバック内でも直接描画APIを呼べる
            end)
        )LUA", "canvas_setup_test");
        check(ok, "pico.create(\"Canvas\") + pico.on(...,\"render\",...)の登録が成功する");

        lua_getglobal(L, "canvas_id");
        canvas_id = (WidgetId)lua_tointeger(L, -1);
        lua_pop(L, 1);

        Widget* canvas = WidgetRegistry::Resolve(canvas_id);
        check(canvas != nullptr, "pico.create(\"Canvas\"): 実体が引ける");
        check(canvas != nullptr && canvas->getWidgetType() == WidgetType::LuaCanvas,
              "pico.create(\"Canvas\"): WidgetType::LuaCanvasとして生成される");
        check(canvas != nullptr && canvas->getX() == 5 && canvas->getY() == 6 &&
                  canvas->getW() == 40 && canvas->getH() == 30,
              "pico.set: x/y/w/hがCanvasにも効く");

        if (canvas) canvas->renderForce(); // FlushDirty()がdirty矩形に重なるウィジェットへ行うforce呼び出しを模す
        lua_getglobal(L, "render_calls");
        check((int)lua_tointeger(L, -1) == 1, "render()経由でLuaのrenderコールバックが呼ばれる");
        lua_pop(L, 1);

        const bool guard_ok = engine.Run(R"LUA(
            local btn2 = pico.create("Button")
            local bound = pcall(function() pico.on(btn2, "render", function() end) end)
            check(bound == false, "pico.on: 'render'イベントはCanvas以外だとエラー")
        )LUA", "render_on_non_canvas_test");
        check(guard_ok, "render非対応ウィジェットへのpico.onが例外として正しく捕捉される");
    }

    // ---- pico.invalidate / pico.mark_dirty ----
    {
        Widget* canvas = WidgetRegistry::Resolve(canvas_id);
        const Rect canvas_screen = canvas ? canvas->getScreenRect() : Rect{0, 0, 0, 0};

        const bool ok = engine.Run("pico.invalidate(canvas_id)", "invalidate_test");
        check(ok, "pico.invalidate: エラーなく実行できる");
        check(canvas != nullptr &&
                  g_last_dirty.x == canvas_screen.x && g_last_dirty.y == canvas_screen.y &&
                  g_last_dirty.w == canvas_screen.w && g_last_dirty.h == canvas_screen.h,
              "pico.invalidate: 対象ウィジェットの画面矩形がdirtyになる");

        const bool ok2 = engine.Run("pico.mark_dirty(11, 22, 33, 44)", "mark_dirty_test");
        check(ok2, "pico.mark_dirty: エラーなく実行できる");
        check(g_last_dirty.x == 11 && g_last_dirty.y == 22 && g_last_dirty.w == 33 && g_last_dirty.h == 44,
              "pico.mark_dirty: 指定した矩形がそのままPICO_GFX::MarkDirty()へ渡る");

        const bool bad_id = engine.Run("pico.invalidate(999999)", "invalidate_bad_id_test");
        check(!bad_id, "pico.invalidate: 無効なIDはエラー");
    }

    // ---- 画像(pico.image_load/draw_image/image_size/image_free) ----
    {
        // SD無しの間は失敗値(nil)を返すだけ(luaL_errorにはしない)
        OSData::SD_usable = false;
        const bool sd_off_ok = engine.Run(
            "check(pico.image_load('/img/a.pimg') == nil, 'pico.image_load: SD無しの間はnil')",
            "image_sd_off_test");
        check(sd_off_ok, "pico.image_load: SD無しテストの実行自体は成功する");

        OSData::SD_usable = true;
        HostSd::files["/img/a.pimg"] = MakePimgBytes(2, 2, false, 5);
        HostSd::files["/img/b.pimg"] = MakePimgBytes(3, 4, false, 1);
        HostSd::files["/img/c.pimg"] = MakePimgBytes(5, 6, false, 2);
        HostSd::files["/img/d.pimg"] = MakePimgBytes(7, 8, false, 3);
        HostSd::files["/img/e.pimg"] = MakePimgBytes(1, 1, false, 4); // スロット枯渇後の追加分
        HostSd::files["/img/missing_body.pimg"] = std::string(); // 5バイトのヘッダすら無い不正ファイル

        const bool load_ok = engine.Run(R"LUA(
            img_a = pico.image_load('/img/a.pimg')
            check(img_a ~= nil, 'pico.image_load: 有効な.pimgはハンドルを返す')

            local w, h = pico.image_size(img_a)
            check(w == 2 and h == 2, 'pico.image_size: 読み込んだ画像の幅・高さが取れる')

            check(pico.image_load('/img/no_such_file.pimg') == nil,
                  'pico.image_load: 存在しないパスはnil')
            check(pico.image_load('/img/missing_body.pimg') == nil,
                  'pico.image_load: ヘッダも読めない不正な.pimgはnil')
        )LUA", "image_load_basic_test");
        check(load_ok, "pico.image_load: 基本テストの実行が成功する");

        const bool draw_ok = engine.Run("pico.draw_image(img_a, 10, 20)", "draw_image_test");
        check(draw_ok, "pico.draw_image: エラーなく実行できる");
        check(g_last_dirty.x == 10 && g_last_dirty.y == 20 && g_last_dirty.w == 2 && g_last_dirty.h == 2,
              "pico.draw_image: 画像サイズ分のdirty矩形が登録される");

        const bool bad_handle_ok = engine.Run(
            "check(pcall(pico.draw_image, 999999, 0, 0) == false, 'pico.draw_image: 無効なハンドルはエラー')",
            "draw_image_bad_handle_test");
        check(bad_handle_ok, "pico.draw_image: 無効ハンドルテストの実行自体は成功する");

        // 残り3スロット(kMaxLuaImages=4のうち1つはimg_aが使用中)を埋めてスロット枯渇を確認する
        const bool fill_ok = engine.Run(R"LUA(
            img_b = pico.image_load('/img/b.pimg')
            img_c = pico.image_load('/img/c.pimg')
            img_d = pico.image_load('/img/d.pimg')
            check(img_b ~= nil and img_c ~= nil and img_d ~= nil,
                  'pico.image_load: 上限枚数までは読み込める')
            check(pico.image_load('/img/e.pimg') == nil,
                  'pico.image_load: スロット上限に達すると以降はnil')
        )LUA", "image_load_fill_test");
        check(fill_ok, "pico.image_load: スロット枯渇テストの実行が成功する");

        // img_bを解放してスロットを1つ空け、再利用後は前回のハンドルが無効化されることを確認する
        // (WidgetRegistryと同じgenerational indexの考え方の回帰確認)
        const bool reuse_ok = engine.Run(R"LUA(
            old_img_b = img_b
            pico.image_free(img_b)
            img_e = pico.image_load('/img/e.pimg')
            check(img_e ~= nil, 'pico.image_free: 解放したスロットは再利用できる')
            check(img_e ~= old_img_b, 'pico.image_free: 再利用後のハンドルは前回発行分と別物になる')
            check(pcall(pico.draw_image, old_img_b, 0, 0) == false,
                  'pico.image_free: 解放済みの古いハンドルはスロット再利用後もエラーのまま(use-after-free検出)')
            pico.image_free(old_img_b) -- 二重解放。pico.destroyと同じく黙って無視されること
            check(true, 'pico.image_free: 二重解放してもエラーにならない')
        )LUA", "image_free_reuse_test");
        check(reuse_ok, "pico.image_free: 再利用/二重解放テストの実行が成功する");
    }

    // ---- 画像: 合計バイト数の上限(kMaxLuaImageBytes) ----
    {
        LuaEngine img_budget_engine(64 * 1024);
        check(img_budget_engine.valid(), "画像バイト予算テスト用にLuaEngineを構築");
        if (img_budget_engine.valid()) {
            lua_pushcfunction(img_budget_engine.raw(), l_check);
            lua_setglobal(img_budget_engine.raw(), "check");

            OSData::SD_usable = true;
            // ヘッダだけ有効(400x400 = 4bppで80000B相当。予算判定はヘッダを読んだ
            // 直後、実ピクセルのデコードより前に行われるのでボディは無くてよい)
            HostSd::files["/img/huge.pimg"] = MakePimgBytes(400, 400, false, 0).substr(0, 5);
            const bool ok = img_budget_engine.Run(
                "check(pico.image_load('/img/huge.pimg') == nil, "
                "'pico.image_load: 合計バイト数の上限を超える場合はnil')",
                "image_budget_test");
            check(ok, "pico.image_load: バイト予算テストの実行が成功する");
        }
    }

    // ---- ダイアログ(pico.show_message/show_input/show_file_save/show_file_select/show_color) ----
    {
        const bool ok = engine.Run(R"LUA(
            msg_id = pico.show_message("本当に削除しますか?", "いいえ", "はい")
            check(msg_id ~= nil, "pico.show_message: ハンドルを返す")
            msg_closed_ok = nil
            pico.on(msg_id, "closed", function(id, is_ok)
                check(id == msg_id, "pico.show_message: closedコールバックへ自分のIDが渡る")
                msg_closed_ok = is_ok
            end)
        )LUA", "show_message_test");
        check(ok, "pico.show_message: セットアップの実行が成功する");

        lua_getglobal(L, "msg_id");
        const WidgetId msg_id = (WidgetId)lua_tointeger(L, -1);
        lua_pop(L, 1);

        Widget* msg_widget = WidgetRegistry::Resolve(msg_id);
        check(msg_widget != nullptr && msg_widget->getWidgetType() == WidgetType::MsgDialog,
              "pico.show_message: MsgDialogとして生成される");
        check(std::find(WidgetFunctions::dialog_roots.begin(), WidgetFunctions::dialog_roots.end(), msg_widget)
                  != WidgetFunctions::dialog_roots.end(),
              "pico.show_message: dialog_rootsへ登録される");

        if (msg_widget) static_cast<MsgDialog*>(msg_widget)->causeOnClosed(true);

        lua_getglobal(L, "msg_closed_ok");
        check(lua_toboolean(L, -1) == 1, "pico.show_message: closedコールバックがis_ok=trueで呼ばれる");
        lua_pop(L, 1);

        WidgetFunctions::ProcessPendingDeletes();
        check(std::find(WidgetFunctions::dialog_roots.begin(), WidgetFunctions::dialog_roots.end(), msg_widget)
                  == WidgetFunctions::dialog_roots.end(),
              "pico.show_message: 閉じると自動的にDestroyLaterされ、dialog_rootsから消える");
    }

    // ---- pico.on()を呼ばなくても、閉じたら自動的に片付くこと ----
    {
        const bool ok = engine.Run(
            "no_listener_id = pico.show_message('通知のみ', 'キャンセル', 'OK')",
            "show_message_no_listener_test");
        check(ok, "pico.show_message: pico.onを呼ばない場合の実行も成功する");

        lua_getglobal(L, "no_listener_id");
        const WidgetId no_listener_id = (WidgetId)lua_tointeger(L, -1);
        lua_pop(L, 1);

        Widget* w = WidgetRegistry::Resolve(no_listener_id);
        check(w != nullptr, "pico.show_message: pico.on無しでも生成はできる");

        if (w) static_cast<MsgDialog*>(w)->causeOnClosed(false);
        WidgetFunctions::ProcessPendingDeletes();
        check(WidgetRegistry::Resolve(no_listener_id) == nullptr,
              "pico.show_message: pico.on(\"closed\")を呼んでいなくても閉じたら自動的に破棄される");
    }

    // ---- 'closed'イベントはダイアログ以外だとエラー ----
    {
        const bool guard_ok = engine.Run(R"LUA(
            local btn3 = pico.create("Button")
            local bound = pcall(function() pico.on(btn3, "closed", function() end) end)
            check(bound == false, "pico.on: 'closed'イベントはダイアログ以外だとエラー")
        )LUA", "closed_on_non_dialog_test");
        check(guard_ok, "'closed'ゲートテストの実行自体は成功する");
    }

    // ---- pico.show_input: 初期テキストの反映とpico.get(\"text\")での読み出し ----
    {
        const bool ok = engine.Run(R"LUA(
            input_id = pico.show_input("お名前", "太郎", true)
            check(input_id ~= nil, "pico.show_input: ハンドルを返す")
            check(pico.get(input_id, "text") == "太郎",
                  "pico.show_input: 初期テキストがpico.get(\"text\")で読める")
            input_closed_ok = nil
            pico.on(input_id, "closed", function(id, is_ok) input_closed_ok = is_ok end)
        )LUA", "show_input_test");
        check(ok, "pico.show_input: セットアップの実行が成功する");

        lua_getglobal(L, "input_id");
        const WidgetId input_id = (WidgetId)lua_tointeger(L, -1);
        lua_pop(L, 1);

        Widget* input_widget = WidgetRegistry::Resolve(input_id);
        check(input_widget != nullptr && input_widget->getWidgetType() == WidgetType::InputDialog,
              "pico.show_input: InputDialogとして生成される");

        if (input_widget) static_cast<InputDialog*>(input_widget)->causeOnClosed(true);

        lua_getglobal(L, "input_closed_ok");
        check(lua_toboolean(L, -1) == 1, "pico.show_input: closedコールバックがis_ok=trueで呼ばれる");
        lua_pop(L, 1);

        WidgetFunctions::ProcessPendingDeletes();
    }

    // ---- pico.show_color: 未選択のままOKするとpico.get(\"value\")が-1 ----
    {
        const bool ok = engine.Run(R"LUA(
            color_id = pico.show_color()
            check(color_id ~= nil, "pico.show_color: ハンドルを返す")
            check(pico.get(color_id, "value") == -1, "pico.show_color: 何も選ばなければ-1")
            color_closed_ok = nil
            pico.on(color_id, "closed", function(id, is_ok) color_closed_ok = is_ok end)
        )LUA", "show_color_test");
        check(ok, "pico.show_color: セットアップの実行が成功する");

        lua_getglobal(L, "color_id");
        const WidgetId color_id = (WidgetId)lua_tointeger(L, -1);
        lua_pop(L, 1);

        Widget* color_widget = WidgetRegistry::Resolve(color_id);
        check(color_widget != nullptr && color_widget->getWidgetType() == WidgetType::ColorDialog,
              "pico.show_color: ColorDialogとして生成される");

        // ColorDialogはcauseOnClosed()相当を公開していないので、実際のタップと同じ経路
        // (children_[1]=button_ok)からcauseOnPressStart()して閉じる
        if (color_widget) {
            const auto& children = color_widget->getChildren();
            check(children.size() >= 2, "pico.show_color: OKボタンを含む子構成");
            if (children.size() >= 2) children[1]->causeOnPressStart();
        }

        lua_getglobal(L, "color_closed_ok");
        check(lua_toboolean(L, -1) == 1, "pico.show_color: closedコールバックがis_ok=trueで呼ばれる");
        lua_pop(L, 1);

        WidgetFunctions::ProcessPendingDeletes();
    }

    // ---- pico.show_file_save / pico.show_file_select ----
    // ホストテストのSdFatスタブはパス→内容のフラットなmapでディレクトリの実体が
    // 無いため(script/host_test/stubs/SdFat.h参照)、getSavePath()/getSelectedPath()の
    // 実際の中身までは確認できない。ここでは生成・dialog_roots登録・closedの配線
    // (is_okの往復)・自動破棄までを見る。実際の選択結果はPCビルドの--shotで確認する
    {
        const bool ok = engine.Run(R"LUA(
            save_id = pico.show_file_save("/")
            check(save_id ~= nil, "pico.show_file_save: ハンドルを返す")
            select_id = pico.show_file_select("/")
            check(select_id ~= nil, "pico.show_file_select: ハンドルを返す")
        )LUA", "show_file_dialogs_test");
        check(ok, "pico.show_file_save/show_file_select: セットアップの実行が成功する");

        lua_getglobal(L, "save_id");
        const WidgetId save_id = (WidgetId)lua_tointeger(L, -1);
        lua_pop(L, 1);
        lua_getglobal(L, "select_id");
        const WidgetId select_id = (WidgetId)lua_tointeger(L, -1);
        lua_pop(L, 1);

        Widget* save_widget = WidgetRegistry::Resolve(save_id);
        Widget* select_widget = WidgetRegistry::Resolve(select_id);
        check(save_widget != nullptr && save_widget->getWidgetType() == WidgetType::FileSaveDialog,
              "pico.show_file_save: FileSaveDialogとして生成される");
        check(select_widget != nullptr && select_widget->getWidgetType() == WidgetType::FileSelectDialog,
              "pico.show_file_select: FileSelectDialogとして生成される");

        // 実際のタップと同じ経路(children_の末尾から2つ目=button_no)で「キャンセル」する
        if (save_widget) {
            const auto& children = save_widget->getChildren();
            if (children.size() >= 1) children.back()->causeOnPressStart();
        }
        if (select_widget) {
            const auto& children = select_widget->getChildren();
            if (children.size() >= 1) children.back()->causeOnPressStart();
        }

        WidgetFunctions::ProcessPendingDeletes();
        check(WidgetRegistry::Resolve(save_id) == nullptr && WidgetRegistry::Resolve(select_id) == nullptr,
              "pico.show_file_save/show_file_select: キャンセルでも自動的に破棄される");
    }

    // ---- ネットワーク(pico.http_request/http_cancel): 実ソケットに触れない早期拒否経路 ----
    // 送信ボディの上限テストで16KiB超の文字列を作るため、それまでの全テストで
    // 積み上がった共有engineの64KiB予算と衝突しないよう、専用の新しいLuaEngineを使う
    // (「画像: 合計バイト数の上限」テストと同じ理由)。
    // ここで見たいのはhttp_request自体の引数チェック等の早期拒否経路なので、
    // LuaPermissions.network=trueを明示的に与える(既定のfalseだと権限自体で
    // 弾かれてしまい、この経路を検証できないため。権限が無い場合の拒否は
    // 下の「権限(LuaPermissions)」ブロックで別途確認する)
    {
        LuaPermissions http_perm;
        http_perm.network = true;
        LuaEngine http_engine(128 * 1024, http_perm);
        check(http_engine.valid(), "ネットワーク早期拒否テスト用にLuaEngineを構築");
        if (http_engine.valid()) {
            lua_pushcfunction(http_engine.raw(), l_check);
            lua_setglobal(http_engine.raw(), "check");

            const bool ok = http_engine.Run(R"LUA(
                check(pcall(pico.http_request, "FOO", "http://127.0.0.1:1/", nil, nil, function() end) == false,
                      "pico.http_request: 未知のメソッドはエラー")

                check(pico.http_request("GET", "not a url", nil, nil, function() end) == false,
                      "pico.http_request: 不正なURLはfalseを返す")

                check(pico.http_request("GET", "https://example.com/", nil, nil, function() end) == false,
                      "pico.http_request: httpsはfalseを返す(未対応)")

                local huge_body = string.rep("a", 20000) -- kMaxHttpBodyBytes(16KiB)超え
                check(pico.http_request("POST", "http://127.0.0.1:1/", huge_body, "text/plain", function() end) == false,
                      "pico.http_request: 送信ボディが上限を超える場合はfalse")

                local started = pico.http_request("GET", "http://127.0.0.1:1/", nil, nil, function() end)
                check(started == true, "pico.http_request: 正常な呼び出しはtrue(開始した)を返す")

                local started2 = pico.http_request("GET", "http://127.0.0.1:1/", nil, nil, function() end)
                check(started2 == false, "pico.http_request: 進行中に2本目を開始しようとするとfalse")

                pico.http_cancel()
                local started3 = pico.http_request("GET", "http://127.0.0.1:1/", nil, nil, function() end)
                check(started3 == true, "pico.http_cancel(): 取り消し後は新しいリクエストを開始できる")
                pico.http_cancel() -- 後片付け(接続を試みる前に取り消すので実ソケットには触れない)
            )LUA", "http_request_reject_test");
            check(ok, "pico.http_request: 早期拒否テストの実行が成功する");
        }
    }

    // ---- 権限(LuaPermissions): ネットワーク拒否 / app_dir外SDアクセスの拒否 ----
    // ここまでのengine/img_budget_engine/http_engineはいずれもapp_dirを省略("/"=
    // 無制限)で構築しており、image_loadのテストが/img/配下を問題なく読めていたのが
    // その証左。ここでは権限の効果そのものを専用のLuaEngineで確認する
    // (LOG_APP_WARNが出ること自体は見ず、戻り値がluaL_errorではなくfalse/nilに
    // なること、app_dir外には実際にI/Oが起きないことを見る)
    {
        // network=false(既定)の間はpico.http_requestが早期にfalseを返す
        LuaEngine no_network_engine(64 * 1024);
        check(no_network_engine.valid(), "権限テスト用にLuaEngineを構築(ネットワーク権限無し)");
        if (no_network_engine.valid()) {
            lua_pushcfunction(no_network_engine.raw(), l_check);
            lua_setglobal(no_network_engine.raw(), "check");
            const bool ok = no_network_engine.Run(
                "check(pico.http_request('GET', 'http://127.0.0.1:1/', nil, nil, function() end) == false, "
                "'pico.http_request: network権限が無ければfalse')",
                "no_network_test");
            check(ok, "LuaPermissions: ネットワーク拒否テストの実行が成功する");
        }

        // sd_outside_app_dir=false(既定)+ app_dir="/lua" の間は、その配下だけ許可される
        LuaEngine confined_engine(64 * 1024, LuaPermissions{}, "/lua");
        check(confined_engine.valid(), "権限テスト用にLuaEngineを構築(app_dir=/lua)");
        if (confined_engine.valid()) {
            lua_pushcfunction(confined_engine.raw(), l_check);
            lua_setglobal(confined_engine.raw(), "check");

            HostSd::files["/lua/inside.txt"] = "ok";
            HostSd::files["/other/outside.txt"] = "ng"; // app_dir外に実在するファイル

            const bool ok = confined_engine.Run(R"LUA(
                check(pico.sd_read('/lua/inside.txt') == 'ok',
                      'pico.sd_read: app_dir配下は読める')
                check(pico.sd_read('/other/outside.txt') == nil,
                      'pico.sd_read: app_dir外はnil')
                check(pico.sd_exists('/other/outside.txt') == false,
                      'pico.sd_exists: app_dir外は実在してもfalse')
                check(pico.sd_write('/other/outside2.txt', 'x') == false,
                      'pico.sd_write: app_dir外への書き込みはfalse')
                check(pico.sd_write('/lua/inside2.txt', 'new') == true,
                      'pico.sd_write: app_dir配下への書き込みはtrue')
                check(pico.sd_read('/lua/inside2.txt') == 'new',
                      'pico.sd_write→pico.sd_read: 書いた内容が読み返せる')
            )LUA", "confined_sd_test");
            check(ok, "LuaPermissions: app_dir配下への閉じ込めテストの実行が成功する");

            check(HostSd::files.count("/other/outside2.txt") == 0,
                  "pico.sd_write: app_dir外への書き込みは拒否時に実際のファイルを作らない");
        }

        // sd_outside_app_dir=trueならapp_dirを指定していてもすり抜けて読み書きできる
        LuaPermissions unrestricted_perm;
        unrestricted_perm.sd_outside_app_dir = true;
        LuaEngine unrestricted_engine(64 * 1024, unrestricted_perm, "/lua");
        check(unrestricted_engine.valid(), "権限テスト用にLuaEngineを構築(sd_outside_app_dir=true)");
        if (unrestricted_engine.valid()) {
            lua_pushcfunction(unrestricted_engine.raw(), l_check);
            lua_setglobal(unrestricted_engine.raw(), "check");
            const bool ok = unrestricted_engine.Run(
                "check(pico.sd_read('/other/outside.txt') == 'ng', "
                "'pico.sd_read: sd_outside_app_dir=trueならapp_dir外も読める')",
                "unrestricted_sd_test");
            check(ok, "LuaPermissions: sd_outside_app_dir=trueテストの実行が成功する");
        }
    }

    // ---- 後片付け(残りのウィジェットも解放し、ASanのリーク検出を素通りさせない) ----
    // 自前でループを回すとDestroy()が子孫ごと解放した後のダングリングポインタを
    // 踏みうる(LayoutContainerの子として既に解放済みのLabelを、コピーしておいた
    // 一覧から独立に触ってしまう)。シーン破棄と同じ手順(WidgetFunctions::ClearSceneWidgets()、
    // 「親を持たないルートだけを都度探し直す」)を使う
    WidgetFunctions::ClearSceneWidgets();

    printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
