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
void PICO_GFX::MarkDirty(const Rect&){}
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

    // ---- 後片付け(残りのウィジェットも解放し、ASanのリーク検出を素通りさせない) ----
    // 自前でループを回すとDestroy()が子孫ごと解放した後のダングリングポインタを
    // 踏みうる(LayoutContainerの子として既に解放済みのLabelを、コピーしておいた
    // 一覧から独立に触ってしまう)。シーン破棄と同じ手順(WidgetFunctions::ClearSceneWidgets()、
    // 「親を持たないルートだけを都度探し直す」)を使う
    WidgetFunctions::ClearSceneWidgets();

    printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
