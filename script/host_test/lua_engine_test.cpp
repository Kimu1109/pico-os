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
#include "lua/LuaEngine.hpp"
#include "gui/widgets/Widget.hpp"
#include "gui/widgets/WidgetRegistry.hpp"
#include "gui/widgets/dialogs/MsgDialog.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/Keyboard_Functions.hpp"
#include <algorithm>
#include <cstdio>

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

    // ---- 後片付け(残りのウィジェットも解放し、ASanのリーク検出を素通りさせない) ----
    // 自前でループを回すとDestroy()が子孫ごと解放した後のダングリングポインタを
    // 踏みうる(LayoutContainerの子として既に解放済みのLabelを、コピーしておいた
    // 一覧から独立に触ってしまう)。シーン破棄と同じ手順(WidgetFunctions::ClearSceneWidgets()、
    // 「親を持たないルートだけを都度探し直す」)を使う
    WidgetFunctions::ClearSceneWidgets();

    printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
