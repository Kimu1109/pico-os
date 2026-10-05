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
//                         送信ボディの上限超過/同時実行数の上限)での
//                         早期拒否がすべてfalseで返ること(luaL_errorにしない)
//   pico.on(id,"checked_changed"/"value_changed"/"select_item"/"tab_changed"/
//           "dropdown_changed"/"text_changed",fn) →
//                         Checkbox/NumberSlider/ScrollList/TabBar/DropdownMenu/Textbox
//                         それぞれの既存のC++側コールバック(causeOnChangeChecked等。
//                         text_changedはTextbox::onHide())を実際に鳴らしてLuaへ届くこと、
//                         値そのものはpico.get()で読めること(select_itemの
//                         already_selectedだけは引数で渡ること)、対応しないウィジェット
//                         種別への登録はエラーになることを確認する
//   pico.get_time() → TimeFunctions::timeinfoを直接書き換えて、返るテーブルの
//                      各フィールドが一致することを確認する
//   pico.get_touch() → OSData::touchX/Y/isTouchedを直接書き換えてからcauseOnPressStart()
//                      を発火させ、press_startコールバックの中で同じ値が読めることを確認する
//   実行時間の安全網(lua_sethook) → 終わらないループ(while true do end)を含む
//                      スクリプトがRun()/CallLoop()をハングさせずfalseで戻ること、
//                      打ち切り時もErrorFunctions経由でダイアログが出ること、
//                      Lua側のpcallで捕まえても打ち切りは握り潰せないこと、上限内の
//                      ループは邪魔されないこと、loop()内で打ち切られた場合は
//                      既存のloop_broken_安全弁と重ねて効くことを確認する
//   pico.remove_child → add_child()の逆。破棄せず取り外せること(parentがnullになる・
//                      コンテナのchildren_から外れる・フラットリストへ独立したルート
//                      として戻ること・WidgetIdはまだ有効なこと)、コンテナ以外や
//                      既に子でないウィジェットを指定するとエラーになることを確認する
//   pico.list_add/list_clear/pico.tab_add → ScrollList/DropdownMenuへ項目を足す/
//                      全消しできること、TabBarへタブを足せること(kMaxTabs超過時は
//                      luaL_errorではなくfalseで返ること)、対応しないウィジェット
//                      種別へ呼ぶとエラーになることを確認する
//   pico.canvas_clear/canvas_save/canvas_load → CanvasRasterのw/hリサイズ
//                      (pico.set経由。以前は100x100固定だった)、白紙化、
//                      `.pimg`としての保存/読み込み(script/generate_pimg.pyと
//                      同じRLE形式でSDへ書き出されること)、保存時と異なるサイズの
//                      .pimgを読み込むとキャンバス自体がそのサイズへ合わせ直される
//                      こと、画面サイズを超えるwidth/heightを名乗るファイルは
//                      拒否すること、CanvasRaster以外・無効IDはエラーになることを
//                      確認する
//   細部のプロパティ → NumberInputのtext(setNum/getNum)・Iconのicon_opaque
//                      (getOpaque)・GridContainerのh_align/v_align(getHAlign/
//                      getVAlign)・Buttonのicon_id/icon_size(setIcon()。以前は
//                      icon_idがget専用でicon_sizeはgetすら無かった)がget/set
//                      往復できること、icon_size変更でw/h未指定なら箱の大きさも
//                      追従することを確認する
#include "lua/LuaEngine.hpp"
#include "functions/Battery_Functions.hpp"
#include "functions/Notification_Functions.hpp"
#include "functions/Sound_Functions.hpp"
#include "functions/Pad_Functions.hpp"
#include "gui/widgets/Widget.hpp"
#include "gui/widgets/WidgetRegistry.hpp"
#include "gui/widgets/Checkbox.hpp"
#include "gui/widgets/NumberSlider.hpp"
#include "gui/widgets/ScrollList.hpp"
#include "gui/widgets/TabBar.hpp"
#include "gui/widgets/DropdownMenu.hpp"
#include "gui/widgets/Textbox.hpp"
#include "gui/widgets/CanvasRaster.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/WidgetFactory.hpp"
#include "gui/widgets/interfaces/ITextInputTarget.hpp"
#include "gui/widgets/dialogs/MsgDialog.hpp"
#include "gui/widgets/dialogs/InputDialog.hpp"
#include "gui/widgets/dialogs/FileSaveDialog.hpp"
#include "gui/widgets/dialogs/FileSelectDialog.hpp"
#include "gui/widgets/dialogs/ColorDialog.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/Keyboard_Functions.hpp"
#include "functions/Time_Functions.hpp"
#include "OS_Data.hpp"
#include <algorithm>
#include <cstdarg>
#include <cstdlib>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include "gui/scenes/LuaScene.hpp"
#include <string>

// ---- モック(widget_factory_test.cppと同じ方針) ----
// MarkDirty()だけは直接描画テストのために最後に渡された矩形を記録する(他のテストは
// 呼び出し回数/中身を見ないのでNoOpのままでも影響しない)
static Rect g_last_dirty{0, 0, 0, 0};
void PICO_GFX::MarkDirty(const Rect& r){ g_last_dirty = r; }
void PICO_GFX::Setup(){}
void PICO_GFX::FlushDirty(){}
void PICO_GFX::DrawDialogBackground(){}
void LogFunctions::Log(LogType, const char* fmt, ...){
    // 環境変数 LUA_TEST_VERBOSE があればログを標準エラーへ出す(テストが落ちた理由を見るため)
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

// text_changedイベントのテスト用: 実機のオンスクリーンキーボード無しに
// Textbox::onHide()(キーボードを閉じて確定した相当)を直接呼ぶための最小限の
// ITextInputWidget実装(widget_factory_test.cppのフェイクと同じ方針)
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

    // ---- pico.remove_child: add_childの逆。破棄せずに取り外す ----
    {
        const bool ok = engine.Run("pico.remove_child(container_id, child_id)", "remove_child_test");
        check(ok, "pico.remove_child: 実行が成功する");

        check(child->getParent() == nullptr,
              "pico.remove_child: 取り外した子のparentはnullになる");
        check(std::find(container->getChildren().begin(), container->getChildren().end(), child)
                  == container->getChildren().end(),
              "pico.remove_child: コンテナのchildren_からも外れる");
        check(std::find(WidgetFunctions::widgets.begin(), WidgetFunctions::widgets.end(), child)
                  != WidgetFunctions::widgets.end(),
              "pico.remove_child: 破棄されず、独立したルートとしてフラットリストへ戻る");
        check(WidgetRegistry::Resolve(child_id) == child,
              "pico.remove_child: WidgetIdはまだ有効(破棄されていない)");

        // 対応外のウィジェット種別(コンテナではない)へのremove_childはエラー
        const bool guard1_ok = engine.Run(R"LUA(
            local btn = pico.create("Button")
            local bound = pcall(function() pico.remove_child(btn, child_id) end)
            check(bound == false, "pico.remove_child: コンテナ以外を第1引数にするとエラー")
        )LUA", "remove_child_non_container_test");
        check(guard1_ok, "remove_child非対応ウィジェットへの呼び出しが例外として正しく捕捉される");

        // 指定したコンテナの子ではない(既に取り外し済み)場合もエラー
        const bool guard2_ok = engine.Run(R"LUA(
            local bound = pcall(function() pico.remove_child(container_id, child_id) end)
            check(bound == false, "pico.remove_child: 既に子でないウィジェットを指定するとエラー")
        )LUA", "remove_child_not_a_child_test");
        check(guard2_ok, "remove_child: 子でない場合の例外が正しく捕捉される");
    }

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

    // ---- 実行時間の安全網(暴走防止): 命令数の上限で打ち切る ----
    {
        // 終わらないループはlua_sethook(LUA_MASKCOUNT)経由で打ち切られ、
        // Run()は(構文/実行時エラーと同じ形で)falseを返す。テストプロセス自体が
        // ハングしないことそのものが最大の確認点(ハングすればこのテストは
        // 永遠に戻ってこない)
        const size_t dialogs_before = WidgetFunctions::dialog_roots.size();
        const bool infinite_ok = engine.Run("while true do end", "infinite_loop_test");
        check(!infinite_ok, "実行時間の安全網: 終わらないループはRun()をfalseで戻す(ハングしない)");
        check(WidgetFunctions::dialog_roots.size() == dialogs_before + 1,
              "実行時間の安全網: 打ち切り時もErrorFunctions::ShowFatal()経由でダイアログが出る");
        if (WidgetFunctions::dialog_roots.size() > dialogs_before) {
            MsgDialog* dialog = static_cast<MsgDialog*>(WidgetFunctions::dialog_roots.back());
            dialog->causeOnClosed(true);
        }
        WidgetFunctions::ProcessPendingDeletes();

        // Lua自身のpcallで内側の打ち切りエラーを捕まえても、打ち切りは握り潰せない
        // (2026-10-03から。以前はpcallで捕まえれば続けられたので、pcallで包んで繰り返す
        // スクリプトを止められなかった)。詳しくはlua_sandbox_test.cpp
        const size_t dialogs_before_caught = WidgetFunctions::dialog_roots.size();
        const bool caught_ok = engine.Run(R"LUA(
            local ok, err = pcall(function() while true do end end)
            check(false, "実行時間の安全網: 打ち切りをpcallで捕まえた後の行は実行されない")
        )LUA", "caught_infinite_loop_test");
        check(!caught_ok, "実行時間の安全網: pcallで打ち切りを捕まえても、スクリプト全体が打ち切られる");
        check(WidgetFunctions::dialog_roots.size() == dialogs_before_caught + 1,
              "実行時間の安全網: pcallで包んでいても打ち切りのダイアログが1枚だけ出る");
        if (WidgetFunctions::dialog_roots.size() > dialogs_before_caught) {
            MsgDialog* dialog = static_cast<MsgDialog*>(WidgetFunctions::dialog_roots.back());
            dialog->causeOnClosed(true);
        }
        WidgetFunctions::ProcessPendingDeletes();

        // 妥当な範囲のループは打ち切られず最後まで実行できる
        const bool bounded_ok = engine.Run(R"LUA(
            local sum = 0
            for i = 1, 10000 do sum = sum + i end
            check(sum == 50005000, "実行時間の安全網: 上限内のループは邪魔されず最後まで実行できる")
        )LUA", "bounded_loop_test");
        check(bounded_ok, "実行時間の安全網: 上限内のループを含むスクリプトはRun()がtrueを返す");

        // loop(dt)内の終わらないループも同様に打ち切られ、既存のloop_broken_安全弁により
        // 以降loop()が呼ばれなくなる(2つの安全網が重ねて効く)。loop_broken_は一度
        // 立ったら戻らないラッチなので、この後の「Arduino風 setup()/loop(dt)」テストを
        // 巻き込まないよう、共有のengineではなく専用のLuaEngineを使う
        LuaEngine loop_engine(64 * 1024);
        check(loop_engine.valid(), "実行時間の安全網: loop()テスト用に専用のLuaEngineを構築");
        if (loop_engine.valid()) {
            const bool loop_setup_ok = loop_engine.Run(
                "function loop(dt) while true do end end",
                "infinite_loop_in_loop_test");
            check(loop_setup_ok, "実行時間の安全網: loop()自体の定義(まだ呼んでいない)は正常に読み込める");
            const size_t dialogs_before2 = WidgetFunctions::dialog_roots.size();
            loop_engine.CallLoop(16);
            check(WidgetFunctions::dialog_roots.size() == dialogs_before2 + 1,
                  "実行時間の安全網: loop(dt)内の終わらないループも打ち切られダイアログが出る");
            if (WidgetFunctions::dialog_roots.size() > dialogs_before2) {
                MsgDialog* dialog = static_cast<MsgDialog*>(WidgetFunctions::dialog_roots.back());
                dialog->causeOnClosed(true);
            }
            WidgetFunctions::ProcessPendingDeletes();
            const size_t dialogs_before3 = WidgetFunctions::dialog_roots.size();
            loop_engine.CallLoop(16); // loop_broken_によりもう呼ばれないはず(ダイアログが増えない)
            check(WidgetFunctions::dialog_roots.size() == dialogs_before3,
                  "実行時間の安全網: 打ち切り後はloop_broken_により以降loop()自体が呼ばれない");
        }
    }

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

    // ---- ウィジェット固有イベント: checked_changed(Checkbox) ----
    {
        const bool ok = engine.Run(R"LUA(
            cb_id = pico.create("Checkbox")
            cb_changed_count = 0
            cb_last_checked = nil
            pico.on(cb_id, "checked_changed", function(id)
                cb_changed_count = cb_changed_count + 1
                cb_last_checked = pico.get(id, "checked")
            end)
        )LUA", "checkbox_setup_test");
        check(ok, "pico.on(...,\"checked_changed\",...)の登録が成功する");

        lua_getglobal(L, "cb_id");
        const WidgetId cb_id = (WidgetId)lua_tointeger(L, -1);
        lua_pop(L, 1);
        Checkbox* cb = static_cast<Checkbox*>(WidgetRegistry::Resolve(cb_id));
        check(cb != nullptr, "pico.create(\"Checkbox\"): 実体が引ける");

        // Checkbox::causeOnPressStart()が実際のチェック状態反転+causeOnChangeChecked()を行う
        // (タップ相当)。値そのものはコールバック引数ではなくpico.get()で読む設計を確認する
        if (cb) cb->causeOnPressStart();
        lua_getglobal(L, "cb_changed_count");
        check((int)lua_tointeger(L, -1) == 1, "checked_changed: タップで1回呼ばれる");
        lua_pop(L, 1);
        lua_getglobal(L, "cb_last_checked");
        check(lua_toboolean(L, -1) == true,
              "checked_changed: pico.get(id,\"checked\")で変更後の値が読める");
        lua_pop(L, 1);

        const bool guard_ok = engine.Run(R"LUA(
            local btn3 = pico.create("Button")
            local bound = pcall(function() pico.on(btn3, "checked_changed", function() end) end)
            check(bound == false, "pico.on: 'checked_changed'イベントはCheckbox以外だとエラー")
        )LUA", "checked_changed_guard_test");
        check(guard_ok, "checked_changed非対応ウィジェットへのpico.onが例外として正しく捕捉される");
    }

    // ---- ウィジェット固有イベント: value_changed(NumberSlider) ----
    {
        const bool ok = engine.Run(R"LUA(
            ns_id = pico.create("NumberSlider")
            pico.set(ns_id, "min_value", 0)
            pico.set(ns_id, "max_value", 100)
            ns_changed_count = 0
            ns_last_value = nil
            pico.on(ns_id, "value_changed", function(id)
                ns_changed_count = ns_changed_count + 1
                ns_last_value = pico.get(id, "value")
            end)
        )LUA", "number_slider_setup_test");
        check(ok, "pico.on(...,\"value_changed\",...)の登録が成功する");

        lua_getglobal(L, "ns_id");
        const WidgetId ns_id = (WidgetId)lua_tointeger(L, -1);
        lua_pop(L, 1);
        NumberSlider* ns = static_cast<NumberSlider*>(WidgetRegistry::Resolve(ns_id));
        check(ns != nullptr, "pico.create(\"NumberSlider\"): 実体が引ける");

        // setValue()がcauseOnValueChanged()を呼ぶ(NumberSlider.hppのsetValue参照)
        if (ns) ns->setValue(42);
        lua_getglobal(L, "ns_changed_count");
        check((int)lua_tointeger(L, -1) == 1, "value_changed: 値変更で1回呼ばれる");
        lua_pop(L, 1);
        lua_getglobal(L, "ns_last_value");
        check(lua_tonumber(L, -1) == 42, "value_changed: pico.get(id,\"value\")で変更後の値が読める");
        lua_pop(L, 1);

        const bool guard_ok = engine.Run(R"LUA(
            local btn4 = pico.create("Button")
            local bound = pcall(function() pico.on(btn4, "value_changed", function() end) end)
            check(bound == false, "pico.on: 'value_changed'イベントはNumberSlider以外だとエラー")
        )LUA", "value_changed_guard_test");
        check(guard_ok, "value_changed非対応ウィジェットへのpico.onが例外として正しく捕捉される");
    }

    // ---- ウィジェット固有イベント: select_item(ScrollList) ----
    {
        const bool ok = engine.Run(R"LUA(
            sl_id = pico.create("ScrollList")
            sl_select_count = 0
            sl_last_index = nil
            sl_last_already_selected = nil
            pico.on(sl_id, "select_item", function(id, already_selected)
                sl_select_count = sl_select_count + 1
                sl_last_index = pico.get(id, "selected_index")
                sl_last_already_selected = already_selected
            end)
        )LUA", "scroll_list_setup_test");
        check(ok, "pico.on(...,\"select_item\",...)の登録が成功する");

        lua_getglobal(L, "sl_id");
        const WidgetId sl_id = (WidgetId)lua_tointeger(L, -1);
        lua_pop(L, 1);
        ScrollList* sl = static_cast<ScrollList*>(WidgetRegistry::Resolve(sl_id));
        check(sl != nullptr, "pico.create(\"ScrollList\"): 実体が引ける");

        if (sl) {
            ScrollListTools::Item item;
            item.text.assign("item0");
            sl->add(item);
            sl->setSelectedIndex(0);
            sl->causeOnSelectItem(false); // 新規選択(2回目のタップではない)
        }
        lua_getglobal(L, "sl_select_count");
        check((int)lua_tointeger(L, -1) == 1, "select_item: 選択で1回呼ばれる");
        lua_pop(L, 1);
        lua_getglobal(L, "sl_last_index");
        check((int)lua_tointeger(L, -1) == 0, "select_item: pico.get(id,\"selected_index\")で選択indexが読める");
        lua_pop(L, 1);
        lua_getglobal(L, "sl_last_already_selected");
        check(lua_toboolean(L, -1) == false,
              "select_item: already_selectedはpermanentプロパティではなく引数で渡る(false)");
        lua_pop(L, 1);

        // 同じ項目を選び直す(already_selected=true)経路も確認する
        if (sl) sl->causeOnSelectItem(true);
        lua_getglobal(L, "sl_last_already_selected");
        check(lua_toboolean(L, -1) == true, "select_item: already_selected=trueも正しく渡る");
        lua_pop(L, 1);

        const bool guard_ok = engine.Run(R"LUA(
            local btn5 = pico.create("Button")
            local bound = pcall(function() pico.on(btn5, "select_item", function() end) end)
            check(bound == false, "pico.on: 'select_item'イベントはScrollList以外だとエラー")
        )LUA", "select_item_guard_test");
        check(guard_ok, "select_item非対応ウィジェットへのpico.onが例外として正しく捕捉される");
    }

    // ---- ウィジェット固有イベント: tab_changed(TabBar) ----
    {
        const bool ok = engine.Run(R"LUA(
            tb_id = pico.create("TabBar")
            tb_changed_count = 0
            tb_last_selected = nil
            pico.on(tb_id, "tab_changed", function(id)
                tb_changed_count = tb_changed_count + 1
                tb_last_selected = pico.get(id, "tab_selected")
            end)
        )LUA", "tab_bar_setup_test");
        check(ok, "pico.on(...,\"tab_changed\",...)の登録が成功する");

        lua_getglobal(L, "tb_id");
        const WidgetId tb_id = (WidgetId)lua_tointeger(L, -1);
        lua_pop(L, 1);
        TabBar* tb = static_cast<TabBar*>(WidgetRegistry::Resolve(tb_id));
        check(tb != nullptr, "pico.create(\"TabBar\"): 実体が引ける");

        if (tb) {
            tb->addTab("A");
            tb->addTab("B");
            tb->setSelected(1, true); // notify=trueでon_changedも鳴らす(タップ経由と同じ扱い)
        }
        lua_getglobal(L, "tb_changed_count");
        check((int)lua_tointeger(L, -1) == 1, "tab_changed: 選択変更で1回呼ばれる");
        lua_pop(L, 1);
        lua_getglobal(L, "tb_last_selected");
        check((int)lua_tointeger(L, -1) == 1, "tab_changed: pico.get(id,\"tab_selected\")で選択indexが読める");
        lua_pop(L, 1);

        const bool guard_ok = engine.Run(R"LUA(
            local btn6 = pico.create("Button")
            local bound = pcall(function() pico.on(btn6, "tab_changed", function() end) end)
            check(bound == false, "pico.on: 'tab_changed'イベントはTabBar以外だとエラー")
        )LUA", "tab_changed_guard_test");
        check(guard_ok, "tab_changed非対応ウィジェットへのpico.onが例外として正しく捕捉される");
    }

    // ---- ウィジェット固有イベント: dropdown_changed(DropdownMenu) ----
    {
        const bool ok = engine.Run(R"LUA(
            dd_id = pico.create("DropdownMenu")
            pico.list_add(dd_id, "A")
            pico.list_add(dd_id, "B")
            dd_changed_count = 0
            dd_last_selected = nil
            pico.on(dd_id, "dropdown_changed", function(id)
                dd_changed_count = dd_changed_count + 1
                dd_last_selected = pico.get(id, "selected_index")
            end)
        )LUA", "dropdown_setup_test");
        check(ok, "pico.on(...,\"dropdown_changed\",...)の登録が成功する(list_addで項目追加込み)");

        lua_getglobal(L, "dd_id");
        const WidgetId dd_id = (WidgetId)lua_tointeger(L, -1);
        lua_pop(L, 1);
        DropdownMenu* dd = static_cast<DropdownMenu*>(WidgetRegistry::Resolve(dd_id));
        check(dd != nullptr, "pico.create(\"DropdownMenu\"): 実体が引ける");

        // DropdownMenuは子(内部のScrollList)を経由してしかタップ選択を再現できない
        // (setSelectedIndex()は表示の初期化用で、on_changedを意図的に飛ばす設計のため)
        if (dd) {
            ScrollList* inner = static_cast<ScrollList*>(dd->getChildren()[0]);
            inner->setSelectedIndex(0);
            inner->causeOnSelectItem(false); // 実際のタップ確定と同じ経路
        }
        lua_getglobal(L, "dd_changed_count");
        check((int)lua_tointeger(L, -1) == 1, "dropdown_changed: 選択確定で1回呼ばれる");
        lua_pop(L, 1);
        lua_getglobal(L, "dd_last_selected");
        check((int)lua_tointeger(L, -1) == 0,
              "dropdown_changed: pico.get(id,\"selected_index\")で選択indexが読める");
        lua_pop(L, 1);

        const bool guard_ok = engine.Run(R"LUA(
            local btn7 = pico.create("Button")
            local bound = pcall(function() pico.on(btn7, "dropdown_changed", function() end) end)
            check(bound == false, "pico.on: 'dropdown_changed'イベントはDropdownMenu以外だとエラー")
        )LUA", "dropdown_changed_guard_test");
        check(guard_ok, "dropdown_changed非対応ウィジェットへのpico.onが例外として正しく捕捉される");
    }

    // ---- ウィジェット固有イベント: text_changed(Textbox) ----
    {
        const bool ok = engine.Run(R"LUA(
            tx_id = pico.create("Textbox")
            tx_changed_count = 0
            tx_last_text = nil
            pico.on(tx_id, "text_changed", function(id)
                tx_changed_count = tx_changed_count + 1
                tx_last_text = pico.get(id, "text")
            end)
        )LUA", "textbox_setup_test");
        check(ok, "pico.on(...,\"text_changed\",...)の登録が成功する");

        lua_getglobal(L, "tx_id");
        const WidgetId tx_id = (WidgetId)lua_tointeger(L, -1);
        lua_pop(L, 1);
        using TextboxT = Textbox<WidgetFactory::kTextboxCapacity>;
        TextboxT* tx = static_cast<TextboxT*>(WidgetRegistry::Resolve(tx_id));
        check(tx != nullptr, "pico.create(\"Textbox\"): 実体が引ける");

        // 実機のオンスクリーンキーボードを介さず、「キーボードを閉じて確定した」
        // 相当のonHide()を直接呼ぶ(on_text_changed()の発火場所そのもの)
        if (tx) {
            FakeKeyboard kb;
            kb.text.assign("hello");
            tx->onHide(&kb);
        }
        lua_getglobal(L, "tx_changed_count");
        check((int)lua_tointeger(L, -1) == 1, "text_changed: onHide()確定で1回呼ばれる");
        lua_pop(L, 1);
        lua_getglobal(L, "tx_last_text");
        check(std::string(lua_tostring(L, -1)) == "hello",
              "text_changed: pico.get(id,\"text\")で確定後の値が読める");
        lua_pop(L, 1);

        const bool guard_ok = engine.Run(R"LUA(
            local btn8 = pico.create("Button")
            local bound = pcall(function() pico.on(btn8, "text_changed", function() end) end)
            check(bound == false, "pico.on: 'text_changed'イベントはTextbox以外だとエラー")
        )LUA", "text_changed_guard_test");
        check(guard_ok, "text_changed非対応ウィジェットへのpico.onが例外として正しく捕捉される");
    }

    // ---- pico.get_time() ----
    {
        // TimeFunctions::timeinfoを直接書き換えて、返る値がそのまま反映されることを確認する
        // (Setup()/Update()はNTP同期やmillis()に依存するためここでは呼ばず、
        // struct tmを直接埋める)
        TimeFunctions::timeinfo.tm_year = 2026 - 1900;
        TimeFunctions::timeinfo.tm_mon = 9 - 1; // 0始まり(0=1月)
        TimeFunctions::timeinfo.tm_mday = 21;
        TimeFunctions::timeinfo.tm_hour = 13;
        TimeFunctions::timeinfo.tm_min = 45;
        TimeFunctions::timeinfo.tm_sec = 6;
        TimeFunctions::timeinfo.tm_wday = 1; // 月曜
        TimeFunctions::year = 2026;
        TimeFunctions::month = 9;

        const bool ok = engine.Run(R"LUA(
            local t = pico.get_time()
            check(t.year == 2026, "pico.get_time: year")
            check(t.month == 9, "pico.get_time: month")
            check(t.day == 21, "pico.get_time: day")
            check(t.hour == 13, "pico.get_time: hour")
            check(t.min == 45, "pico.get_time: min")
            check(t.sec == 6, "pico.get_time: sec")
            check(t.wday == 1, "pico.get_time: wday")
        )LUA", "get_time_test");
        check(ok, "pico.get_time(): スクリプトの実行が成功する");
    }

    // ---- pico.get_touch() ----
    {
        // OSData::touchX/Y/isTouchedを直接書き換えてから、実際のタップ相当
        // (causeOnPressStart())を発火させ、press_startコールバックの中で
        // pico.get_touch()が同じ値を読めることを確認する(WidgetFunctions::HitTest()
        // が当たり判定に使う値と同じであることの裏付け。src/functions/Widget_Functions.cpp参照)
        const bool setup_ok = engine.Run(R"LUA(
            touch_btn = pico.create("Button")
            touch_seen_x, touch_seen_y, touch_seen_touched = nil, nil, nil
            pico.on(touch_btn, "press_start", function(id)
                touch_seen_x, touch_seen_y, touch_seen_touched = pico.get_touch()
            end)
        )LUA", "get_touch_setup_test");
        check(setup_ok, "pico.get_touch(): 準備スクリプトの実行が成功する");

        lua_State* L2 = engine.raw();
        lua_getglobal(L2, "touch_btn");
        const WidgetId touch_btn_id = (WidgetId)lua_tointeger(L2, -1);
        lua_pop(L2, 1);
        Widget* touch_btn = WidgetRegistry::Resolve(touch_btn_id);

        OSData::touchX = 123;
        OSData::touchY = 45;
        OSData::isTouched = true;
        if (touch_btn) touch_btn->causeOnPressStart();
        OSData::isTouched = false; // 後続のテストへ影響しないよう戻す

        lua_getglobal(L2, "touch_seen_x");
        check((int)lua_tointeger(L2, -1) == 123, "pico.get_touch: press_start内でxが読める");
        lua_pop(L2, 1);
        lua_getglobal(L2, "touch_seen_y");
        check((int)lua_tointeger(L2, -1) == 45, "pico.get_touch: press_start内でyが読める");
        lua_pop(L2, 1);
        lua_getglobal(L2, "touch_seen_touched");
        check(lua_toboolean(L2, -1) == 1, "pico.get_touch: press_start内でis_touchedがtrue");
        lua_pop(L2, 1);
    }

    // ---- 外部コントローラー(pico.pad_connected / pad_down / pad_pressed / pad_released) ----
    {
        // USBシリアルの代わりにHostSerialへ行を流し、PadFunctions::UpdateAt()でフレームを進める
        PadFunctions::Setup();
        PadFunctions::UpdateAt(10000);
        bool ok = engine.Run(R"LUA(
            if pico.pad_connected() then error("最初はつながっていないはず") end
            if pico.pad_down("a") then error("押していないはず") end
        )LUA", "pad_idle_test");
        check(ok, "pico.pad_*: 何もつながっていなければfalse");

        HostSerial::Feed("pad 0011\n"); // 上 + A
        PadFunctions::UpdateAt(10016);
        ok = engine.Run(R"LUA(
            if not pico.pad_connected() then error("つながったはず") end
            if not (pico.pad_down("a") and pico.pad_down("up")) then error("上とAを押しているはず") end
            if not pico.pad_pressed("a") then error("このフレームで押したはず") end
            if pico.pad_down("b") or pico.pad_released("a") then error("Bは押していない/Aは離していない") end
        )LUA", "pad_down_test");
        check(ok, "pico.pad_*: 同時押しと押した瞬間が読める");

        HostSerial::Feed("pad 0001\n");
        PadFunctions::UpdateAt(10032);
        ok = engine.Run(R"LUA(
            if not pico.pad_released("a") then error("Aを離したはず") end
            if pico.pad_pressed("up") then error("上は押し続けているだけ") end
        )LUA", "pad_released_test");
        check(ok, "pico.pad_*: 離した瞬間が読める");

        ok = engine.Run("pico.pad_down('jump')", "pad_bad_name_test");
        check(!ok, "pico.pad_down: 知らないボタン名はエラー");

        PadFunctions::Setup(); // 後続のテストへ影響しないよう戻す
    }

    // ---- 音(pico.sound_play / sound_stop / sound_playing / note_freq / beep) ----
    {
        // 2コア目の代わりにCore1StepAt()をここで回す(アンプは未接続=時間で進むだけ)
        SoundFunctions::SetupAt(0);
        {
            LuaEngine snd(200 * 1024);
            lua_register(snd.raw(), "check", l_check);
            const bool ok = snd.Run(R"LUA(
                check(math.abs(pico.note_freq("A4") - 440) < 0.01, "pico.note_freq: A4 = 440Hz")
                check(math.abs(pico.note_freq(60) - 261.63) < 0.01, "pico.note_freq: 60 = C4")
                check(pico.note_freq("H4") == nil, "pico.note_freq: 読めない音名はnil")
                check(pico.note_freq(200) == nil, "pico.note_freq: 範囲外はnil")
                check(pico.sound_play(2, 220, 0, {wave = "triangle", volume = 10, envelope = -3}) == true,
                      "pico.sound_play: 積めたらtrue")
                check(pico.sound_playing() == true, "pico.sound_playing: 積んだ直後から鳴っている扱い")
                check(not pcall(pico.sound_play, 0, 440, 100), "pico.sound_play: チャンネル0はエラー(1始まり)")
                check(not pcall(pico.sound_play, 999, 440, 100), "pico.sound_play: 範囲外のチャンネルはエラー")
                check(not pcall(pico.sound_play, 1, 440, 100, {wave = "sine"}), "pico.sound_play: 不明な波形はエラー")
                check(not pcall(pico.sound_play, 1, 440, 100, "pulse50"), "pico.sound_play: 4番目は表")
            )LUA", "sound_test");
            check(ok, "音: スクリプトの実行が成功する");
            SoundFunctions::Core1StepAt(0);
            check(SoundFunctions::ActiveChannels() == 0x2, "pico.sound_play: 2コア目が受け取るとチャンネル2(ch1)が鳴る");

            snd.Run(R"LUA(
                pico.sound_play(2, 0, 100)
            )LUA", "sound_rest_test");
            SoundFunctions::Core1StepAt(0);
            check(SoundFunctions::ActiveChannels() == 0, "pico.sound_play: 周波数0は止める(休符)");

            snd.Run(R"LUA(
                pico.sound_play(4, 110, 0, {wave = "noise"})
            )LUA", "sound_sustain_test");
            SoundFunctions::Core1StepAt(0);
            lua_getglobal(snd.raw(), "pico");
            lua_getfield(snd.raw(), -1, "sound_playing");
            lua_pushinteger(snd.raw(), 4);
            lua_call(snd.raw(), 1, 1);
            check(lua_toboolean(snd.raw(), -1) == 1, "pico.sound_playing(4): 鳴っているチャンネルはtrue");
            lua_pop(snd.raw(), 2);
        }
        //LuaEngineを壊すと(=アプリを閉じると)鳴らしっぱなしの音も止まる
        SoundFunctions::Core1StepAt(0);
        check(SoundFunctions::ActiveChannels() == 0 && !SoundFunctions::IsPlaying(),
              "音: 音を使ったアプリを閉じると全部止まる");
    }

    // ---- 曲(pico.music_play_text / music_play / music_stop / music_playing) ----
    {
        {
            LuaEngine mus(200 * 1024);
            lua_register(mus.raw(), "check", l_check);
            const bool ok = mus.Run(R"LUA(
                local ok, err = pico.music_play_text("A c v99")
                check(ok == nil and err == "1行5列: v の後ろは0〜15です", "pico.music_play_text: 誤りは nil, 行列つきの理由")
                check(pico.music_playing() == false, "pico.music_playing: 読めなかったときは鳴らない")
                check(pico.music_play_text("A L l8 c d e f") == true, "pico.music_play_text: 読めたら true")
                check(pico.music_playing() == true, "pico.music_playing: 頼んだ直後から true")
                local ok2, err2 = pico.music_play("/music/nothing.mml")
                check(ok2 == nil and type(err2) == "string", "pico.music_play: 開けなければ nil, 理由")
            )LUA", "music_test");
            check(ok, "曲: スクリプトの実行が成功する");
            SoundFunctions::Core1StepAt(0);
            check(SoundFunctions::MusicPlaying(), "曲: 2コア目が鳴らしている");
        }
        //アプリを閉じると曲も止まる
        SoundFunctions::Core1StepAt(0);
        check(!SoundFunctions::MusicPlaying(), "曲: 曲を使ったアプリを閉じると止まる");
    }

    // ---- WAV(pico.wav_play / wav_stop / wav_playing) ----
    {
        const bool sd_before = OSData::SD_usable;
        OSData::SD_usable = true;
        {
            //16bitモノラル22050Hz・1000サンプルの最小のWAV
            std::string data(2000, '\0');
            auto u32 = [](uint32_t v){ std::string s; for(int i = 0; i < 4; i++) s += (char)((v >> (i * 8)) & 0xFF); return s; };
            auto u16 = [](uint16_t v){ std::string s; s += (char)(v & 0xFF); s += (char)(v >> 8); return s; };
            std::string fmt = u16(1) + u16(1) + u32(22050) + u32(44100) + u16(2) + u16(16);
            std::string body = "WAVE" + std::string("fmt ") + u32(16) + fmt + "data" + u32(2000) + data;
            HostSd::files["/wav/a.wav"] = "RIFF" + u32((uint32_t)body.size()) + body;
            HostSd::files["/wav/bad.wav"] = "not a wav";
        }
        {
            LuaEngine wav(200 * 1024, LuaPermissions{}, "/wav");
            lua_register(wav.raw(), "check", l_check);
            const bool ok = wav.Run(R"LUA(
                local ok, err = pico.wav_play("/wav/bad.wav")
                check(ok == nil and type(err) == "string" and #err > 0, "pico.wav_play: 読めなければ nil, 理由")
                check(pico.wav_playing() == false, "pico.wav_playing: 読めなかったときは鳴らない")
                local ok2, err2 = pico.wav_play("/other/a.wav")
                check(ok2 == nil and type(err2) == "string", "pico.wav_play: アプリの外は権限が無ければ nil, 理由")
                check(pico.wav_play("/wav/a.wav", {loop = true, volume = 30}) == true, "pico.wav_play: 読めたら true")
                check(pico.wav_playing() == true, "pico.wav_playing: 鳴らした直後から true")
                local e1 = pcall(pico.wav_play, "/wav/a.wav", {loop = 1})
                check(e1 == false, "pico.wav_play: loopが真偽値でなければエラー")
                local e2 = pcall(pico.wav_play, "/wav/a.wav", 5)
                check(e2 == false, "pico.wav_play: 2つ目が表でなければエラー")
            )LUA", "wav_test");
            check(ok, "WAV: スクリプトの実行が成功する");
            check(SoundFunctions::WavPlaying(), "WAV: 鳴っている");
        }
        check(!SoundFunctions::WavPlaying(), "WAV: WAVを使ったアプリを閉じると止まる");
        {
            LuaEngine wav(200 * 1024, LuaPermissions{}, "/wav");
            lua_register(wav.raw(), "check", l_check);
            wav.Run(R"LUA(
                pico.wav_play("/wav/a.wav", {loop = true})
                pico.wav_stop()
                check(pico.wav_playing() == false, "pico.wav_stop: 止まる")
            )LUA", "wav_stop_test");
        }
        OSData::SD_usable = sd_before;
    }

    // ---- pico.list_add / pico.list_clear(ScrollList/DropdownMenu) / pico.tab_add(TabBar) ----
    {
        const bool ok = engine.Run(R"LUA(
            sl_pop = pico.create("ScrollList")
            pico.list_add(sl_pop, "one")
            pico.list_add(sl_pop, "two")
            check(pico.get(sl_pop, "item_count") == 2, "pico.list_add: ScrollListへ2件追加")
            pico.list_clear(sl_pop)
            check(pico.get(sl_pop, "item_count") == 0, "pico.list_clear: ScrollListが空になる")

            dd_pop = pico.create("DropdownMenu")
            pico.list_add(dd_pop, "a")
            pico.list_add(dd_pop, "b")
            pico.list_add(dd_pop, "c")
            check(pico.get(dd_pop, "item_count") == 3, "pico.list_add: DropdownMenuへ3件追加")
            pico.list_clear(dd_pop)
            check(pico.get(dd_pop, "item_count") == 0, "pico.list_clear: DropdownMenuが空になる")

            tabbar = pico.create("TabBar")
            check(pico.tab_add(tabbar, "A") == true, "pico.tab_add: 1本目は成功")
            check(pico.tab_add(tabbar, "B") == true, "pico.tab_add: 2本目は成功")
            check(pico.tab_add(tabbar, "C") == true, "pico.tab_add: 3本目は成功")
            check(pico.tab_add(tabbar, "D") == true, "pico.tab_add: 4本目(kMaxTabs)は成功")
            check(pico.tab_add(tabbar, "E") == false,
                  "pico.tab_add: 5本目はkMaxTabs超過でfalse(luaL_errorにはしない)")
            check(pico.get(tabbar, "tab_count") == 4, "pico.tab_add: tab_countが4のまま")

            local other = pico.create("Button")
            check(pcall(function() pico.list_add(other, "x") end) == false,
                  "pico.list_add: ScrollList/DropdownMenu以外はエラー")
            check(pcall(function() pico.list_clear(other) end) == false,
                  "pico.list_clear: ScrollList/DropdownMenu以外はエラー")
            check(pcall(function() pico.tab_add(other, "x") end) == false,
                  "pico.tab_add: TabBar以外はエラー")
        )LUA", "list_tab_test");
        check(ok, "pico.list_add/list_clear/tab_add: スクリプトの実行が成功する");
    }

    // ---- 細部のプロパティ: NumberInputのtext / Iconのicon_opaque / GridContainerのh_align・v_align ----
    //                        / Buttonのicon_id・icon_size(アイコンボタン化) ----
    {
        const bool ok = engine.Run(R"LUA(
            ni = pico.create("NumberInput")
            pico.set(ni, "text", "123")
            check(pico.get(ni, "text") == "123", "NumberInput: textの往復(setNum/getNum)")

            ic2 = pico.create("Icon")
            pico.set(ic2, "icon_opaque", true)
            check(pico.get(ic2, "icon_opaque") == true, "Icon: icon_opaqueの往復(getOpaque追加)")

            gc2 = pico.create("GridContainer")
            pico.set(gc2, "h_align", 1)
            pico.set(gc2, "v_align", 2)
            check(pico.get(gc2, "h_align") == 1, "GridContainer: h_alignの往復(getHAlign追加)")
            check(pico.get(gc2, "v_align") == 2, "GridContainer: v_alignの往復(getVAlign追加)")

            -- スクラッチパッド実装時の細部の穴埋め: Buttonのicon_id/icon_sizeは
            -- 元々getIconId()経由の読み取りしかできず、setは非対応(icon_sizeは
            -- getすら無かった)だった
            btn_icon = pico.create("Button")
            pico.set(btn_icon, "icon_id", 53) -- Brush
            pico.set(btn_icon, "icon_size", 2) -- Px32
            check(pico.get(btn_icon, "icon_id") == 53, "Button: icon_idの往復(setIcon追加)")
            check(pico.get(btn_icon, "icon_size") == 2, "Button: icon_sizeの往復(get/set追加)")
        )LUA", "property_gap_test");
        check(ok, "細部のプロパティ: スクリプトの実行が成功する");

        lua_getglobal(L, "btn_icon");
        const WidgetId btn_icon_id = (WidgetId)lua_tointeger(L, -1);
        lua_pop(L, 1);
        Button* btn_icon_w = static_cast<Button*>(WidgetRegistry::Resolve(btn_icon_id));
        check(btn_icon_w != nullptr && btn_icon_w->getHasIcon(),
              "Button: setIcon()経由でhas_icon=trueになりアイコンボタンとして描かれる");
        // w/hを明示指定していないので、icon_size=Px32(32px)へ箱の大きさも追従する
        // (getW()/getH()は枠・立体表示を含む全体の大きさなので、32 + 枠のぶん)
        check(btn_icon_w != nullptr &&
              btn_icon_w->getW() == 32 + Button::kFrameExtra && btn_icon_w->getH() == 32 + Button::kFrameExtra,
              "Button: icon_size変更で(w/h未指定なら)箱の大きさも追従する");
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

        // 一部だけ描く(スプライトシートの切り出し)。はみ出す分は画像の大きさへ削られる
        const bool part_ok = engine.Run("pico.draw_image_part(img_a, 30, 40, 1, 0, 5, 5)", "draw_image_part_test");
        check(part_ok, "pico.draw_image_part: エラーなく実行できる");
        check(g_last_dirty.x == 30 && g_last_dirty.y == 40 && g_last_dirty.w == 1 && g_last_dirty.h == 2,
              "pico.draw_image_part: 画像の内側へ削った大きさのdirty矩形が登録される");
        // 描いたあとも呼び出し前のクリップ(renderの中ならdirty矩形)へ戻っている
        OSData::frame->setClipRect(3, 4, 50, 60);
        const bool part_clip_ok = engine.Run(R"LUA(
            pico.draw_image_part(img_a, 0, 0, 0, 0, 2, 2)
            local x, y, w, h = pico.get_draw_area()
            check(x == 3 and y == 4 and w == 50 and h == 60,
                  'pico.draw_image_part/get_draw_area: クリップは元へ戻り、get_draw_areaで読める')
            pico.draw_image_part(img_a, 0, 0, 5, 5, 2, 2) -- 画像の外だけを指す: 何もしない
            check(pcall(pico.draw_image_part, 999999, 0, 0, 0, 0, 1, 1) == false,
                  'pico.draw_image_part: 無効なハンドルはエラー')
        )LUA", "draw_image_part_clip_test");
        OSData::frame->clearClipRect();
        check(part_clip_ok, "pico.draw_image_part: クリップのテストの実行自体は成功する");

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

    // ---- ラスタキャンバス(pico.canvas_clear/canvas_save/canvas_load) ----
    // CanvasRasterは元々w/hが100x100固定でリサイズできず、クリア/保存/読み込みの
    // 手段も無かった(LuaEngine.hppの「ラスタキャンバスの保存/読み込み」参照)。
    {
        WidgetId canvas_id = WidgetIdTools::Invalid();
        const bool resize_ok = engine.Run(R"LUA(
            canvas_id = pico.create("CanvasRaster")
            check(pico.get(canvas_id, "w") == 100 and pico.get(canvas_id, "h") == 100,
                  "pico.create(\"CanvasRaster\"): 生成直後は100x100")
            pico.set(canvas_id, "w", 6)
            pico.set(canvas_id, "h", 4)
            check(pico.get(canvas_id, "w") == 6 and pico.get(canvas_id, "h") == 4,
                  "pico.set: CanvasRasterのw/hがリサイズできる(以前は非対応だった)")
        )LUA", "canvas_resize_test");
        check(resize_ok, "CanvasRasterのリサイズテストの実行が成功する");

        lua_getglobal(L, "canvas_id");
        canvas_id = (WidgetId)lua_tointeger(L, -1);
        lua_pop(L, 1);

        CanvasRaster* cr = static_cast<CanvasRaster*>(WidgetRegistry::Resolve(canvas_id));
        check(cr != nullptr, "pico.create(\"CanvasRaster\"): 実体が引ける");

        // ドラッグ中の再描画範囲: 1回動くごとにキャンバス全体をdirtyにすると、実機では
        // 合成(スプライト複製)と液晶への転送が画面ほぼ1枚ぶん走り、線がカクカクになった。
        // 描いた線分の周り(ブラシ半径ぶん広げた外接矩形)だけがdirtyになることを固定する
        if (cr) {
            cr->resize(60, 60); // 6x4では線分の範囲がキャンバス全体と区別できないため一時的に広げる
            const Rect g = cr->getScreenRect();
            cr->setBrushRadius(2.0f);
            OSData::touchX = g.x + 1;
            OSData::touchY = g.y + 1;
            g_last_dirty = Rect{0, 0, 0, 0};
            cr->causeOnPressStart();
            check(g_last_dirty.x == g.x && g_last_dirty.y == g.y &&
                  g_last_dirty.w == 5 && g_last_dirty.h == 5,
                  "CanvasRaster: 触れた瞬間に点を打ち、その周り(キャンバス内に切り詰め)だけをdirtyにする");

            OSData::touchX = g.x + 4;
            OSData::touchY = g.y + 3;
            g_last_dirty = Rect{0, 0, 0, 0};
            cr->causeOnPressMove();
            check(g_last_dirty.x == g.x && g_last_dirty.y == g.y &&
                  g_last_dirty.w == 8 && g_last_dirty.h == 7,
                  "CanvasRaster: ドラッグは線分の外接矩形+半径ぶんだけをdirtyにする(キャンバス全体ではない)");

            // 1px未満でも動けば繋ぐ(以前は2px未満の動きを捨てていた)
            OSData::touchX = g.x + 5;
            g_last_dirty = Rect{0, 0, 0, 0};
            cr->causeOnPressMove();
            check(g_last_dirty.w > 0, "CanvasRaster: 1pxの動きでも線を繋ぐ");

            // 指を離したフレームはcauseOnPressMove()が来ないので、最後の区間は離した時に繋ぐ
            OSData::touchX = g.x + 9;
            g_last_dirty = Rect{0, 0, 0, 0};
            cr->causeOnPressEnd();
            check(g_last_dirty.w > 0 && g_last_dirty.w < g.w,
                  "CanvasRaster: 離した瞬間に最後の区間を繋ぐ");
            OSData::touchX = 0;
            OSData::touchY = 0;
            cr->resize(6, 4);
        }

        // 全ピクセルを既知の色(9=PICO_BLUE)で塗る。タッチのドラッグ(causeOnPressMove)
        // を模すより単純なため直接スプライトへ書き込む(ドラッグでの自由線描画自体は
        // CanvasRaster既存の機能で、今回追加した保存/読み込みの対象ではない)
        if (cr) {
            for (int py = 0; py < cr->getH(); ++py)
                for (int px = 0; px < cr->getW(); ++px)
                    cr->getSprite()->writePixel(px, py, 9);
        }

        OSData::SD_usable = false;
        const bool sd_off_ok = engine.Run(R"LUA(
            check(pico.canvas_save(canvas_id, '/canvas/a.pimg') == false,
                  'pico.canvas_save: SD無しの間はfalse')
            check(pico.canvas_load(canvas_id, '/canvas/a.pimg') == false,
                  'pico.canvas_load: SD無しの間はfalse')
        )LUA", "canvas_sd_off_test");
        check(sd_off_ok, "pico.canvas_save/load: SD無しテストの実行自体は成功する");
        OSData::SD_usable = true;

        const bool save_ok = engine.Run(
            "check(pico.canvas_save(canvas_id, '/canvas/a.pimg') == true, "
            "'pico.canvas_save: 保存が成功する')",
            "canvas_save_test");
        check(save_ok, "pico.canvas_save: テストの実行が成功する");
        check(HostSd::files.count("/canvas/a.pimg") == 1,
              "pico.canvas_save: SDへ実際にファイルができる");
        check(HostSd::files["/canvas/a.pimg"] == MakePimgBytes(6, 4, false, 9),
              "pico.canvas_save: 書き出したバイト列がscript/generate_pimg.pyと同じRLE形式になる");

        const bool clear_ok = engine.Run("pico.canvas_clear(canvas_id)", "canvas_clear_test");
        check(clear_ok, "pico.canvas_clear: エラーなく実行できる");
        check(cr != nullptr && cr->getSprite()->readPixelValue(0, 0) == 15,
              "pico.canvas_clear: 白(PICO_WHITE)へ戻る");

        const bool load_ok = engine.Run(
            "check(pico.canvas_load(canvas_id, '/canvas/a.pimg') == true, "
            "'pico.canvas_load: 読み込みが成功する')",
            "canvas_load_test");
        check(load_ok, "pico.canvas_load: テストの実行が成功する");
        check(cr != nullptr && cr->getSprite()->readPixelValue(0, 0) == 9 &&
                  cr->getSprite()->readPixelValue(5, 3) == 9,
              "pico.canvas_load: 保存した内容が読み戻せる");

        // 保存時と違うサイズの.pimgを読み込むと、キャンバス自体がそのサイズへ
        // 合わせ直される(CanvasRaster::resize()。この時点で旧内容は消える)
        HostSd::files["/canvas/small.pimg"] = MakePimgBytes(3, 2, false, 1);
        const bool resize_load_ok = engine.Run(R"LUA(
            check(pico.canvas_load(canvas_id, '/canvas/small.pimg') == true,
                  'pico.canvas_load: 異なるサイズの.pimgも読み込める')
            check(pico.get(canvas_id, "w") == 3 and pico.get(canvas_id, "h") == 2,
                  'pico.canvas_load: キャンバス自体が画像サイズへリサイズされる')
        )LUA", "canvas_load_resize_test");
        check(resize_load_ok, "pico.canvas_load: サイズ違いの読み込みテストの実行が成功する");
        check(cr != nullptr && cr->getSprite()->readPixelValue(0, 0) == 1,
              "pico.canvas_load: リサイズ後の内容も正しくデコードされる");

        // 画面サイズを超える(壊れた/悪意のある)width/heightを名乗るファイルは
        // ヘッダを読んだ直後、デコードより前に拒否する(ボディが無くてもここで弾かれる)
        std::string huge_header;
        huge_header += (char)0xFF; huge_header += (char)0xFF; // width=65535
        huge_header += (char)0xFF; huge_header += (char)0xFF; // height=65535
        huge_header += (char)0x00;
        HostSd::files["/canvas/huge.pimg"] = huge_header;
        const bool huge_ok = engine.Run(R"LUA(
            check(pico.canvas_load(canvas_id, '/canvas/huge.pimg') == false,
                  'pico.canvas_load: 画面サイズを超えるwidth/heightは拒否する')
            check(pico.get(canvas_id, "w") == 3 and pico.get(canvas_id, "h") == 2,
                  'pico.canvas_load: 拒否時はキャンバスのサイズも変わらない')
        )LUA", "canvas_load_oversize_test");
        check(huge_ok, "pico.canvas_load: サイズ超過拒否テストの実行が成功する");

        // 対象がCanvasRaster以外、または無効なIDだとエラー(pico.on(render/closed)と同じ扱い)
        const bool wrong_type_ok = engine.Run(R"LUA(
            local btn = pico.create("Button")
            check(pcall(pico.canvas_clear, btn) == false,
                  'pico.canvas_clear: CanvasRaster以外はエラー')
            check(pcall(pico.canvas_save, btn, '/tmp/x.pimg') == false,
                  'pico.canvas_save: CanvasRaster以外はエラー')
            check(pcall(pico.canvas_load, btn, '/tmp/x.pimg') == false,
                  'pico.canvas_load: CanvasRaster以外はエラー')
            check(pcall(pico.canvas_clear, 999999) == false,
                  'pico.canvas_clear: 無効なIDはエラー')
        )LUA", "canvas_wrong_type_test");
        check(wrong_type_ok, "pico.canvas_*: 対象種別/ID検証テストの実行が成功する");
    }

    // ---- ペイント向けの追加(直線/塗りつぶしモード・filled・元に戻す・keep_size読込) ----
    {
        const bool setup_ok = engine.Run(R"LUA(
            paint_id = pico.create("CanvasRaster")
            pico.set(paint_id, "w", 20)
            pico.set(paint_id, "h", 12)
            pico.set(paint_id, "canvas_mode", 4)
            check(pico.get(paint_id, "canvas_mode") == 4, "canvas_mode: 4(直線)を受け付ける")
            pico.set(paint_id, "canvas_mode", 5)
            check(pico.get(paint_id, "canvas_mode") == 5, "canvas_mode: 5(塗りつぶし)を受け付ける")
            check(pcall(pico.set, paint_id, "canvas_mode", 6) == false,
                  "canvas_mode: 範囲外はエラー")
            check(pico.get(paint_id, "canvas_mode") == 5, "canvas_mode: 範囲外を渡しても変わらない")
            check(pico.get(paint_id, "filled") == false, "filled: 既定は輪郭のみ")
            check(pico.get(paint_id, "undo_enabled") == false, "undo_enabled: 既定は無効")
            check(pico.canvas_undo(paint_id) == false, "pico.canvas_undo: 無効の間はfalse")
            pico.set(paint_id, "undo_enabled", true)
            check(pico.get(paint_id, "undo_enabled") == true, "undo_enabled: 有効にできる")
            check(pico.canvas_undo(paint_id) == false, "pico.canvas_undo: まだ何も描いていなければfalse")
            pico.set(paint_id, "color", 12)
        )LUA", "paint_setup_test");
        check(setup_ok, "ペイント向けの設定テストの実行が成功する");

        lua_getglobal(L, "paint_id");
        const WidgetId paint_id = (WidgetId)lua_tointeger(L, -1);
        lua_pop(L, 1);
        CanvasRaster* pc = static_cast<CanvasRaster*>(WidgetRegistry::Resolve(paint_id));
        check(pc != nullptr, "ペイント用のCanvasRasterが引ける");

        if (pc) {
            LGFX_Sprite* sp = pc->getSprite();
            const Rect g = pc->getScreenRect();

            // 黒(0)の枠 x=2..10, y=2..8 を描き、その内側を塗りつぶす
            for (int x = 2; x <= 10; x++) { sp->writePixel(x, 2, 0); sp->writePixel(x, 8, 0); }
            for (int y = 2; y <= 8; y++) { sp->writePixel(2, y, 0); sp->writePixel(10, y, 0); }

            OSData::touchX = g.x + 5;
            OSData::touchY = g.y + 5;
            g_last_dirty = Rect{0, 0, 0, 0};
            pc->causeOnPressStart();
            pc->causeOnPressEnd();

            bool inside = true, border = true, outside = true;
            for (int y = 0; y < 12; y++) {
                for (int x = 0; x < 20; x++) {
                    const uint32_t v = sp->readPixelValue(x, y);
                    const bool on_border = (x >= 2 && x <= 10 && (y == 2 || y == 8)) ||
                                           (y >= 2 && y <= 8 && (x == 2 || x == 10));
                    const bool in = x > 2 && x < 10 && y > 2 && y < 8;
                    if (on_border && v != 0) border = false;
                    else if (in && v != 12) inside = false;
                    else if (!on_border && !in && v != 15) outside = false;
                }
            }
            check(inside, "塗りつぶし: 枠の内側が塗られる");
            check(border && outside, "塗りつぶし: 枠と枠の外へは漏れない");
            check(g_last_dirty.x == g.x + 3 && g_last_dirty.y == g.y + 3 &&
                  g_last_dirty.w == 7 && g_last_dirty.h == 5,
                  "塗りつぶし: 塗った範囲だけをdirtyにする");

            // 元に戻す: 1回目で塗る前へ、2回目でやり直し
            const bool undo_ok = engine.Run(R"LUA(
                check(pico.canvas_undo(paint_id) == true, "pico.canvas_undo: 塗りつぶしを戻せる")
            )LUA", "paint_undo_test");
            check(undo_ok && sp->readPixelValue(5, 5) == 15 && sp->readPixelValue(2, 2) == 0,
                  "pico.canvas_undo: 塗る前の状態に戻る");
            engine.Run("pico.canvas_undo(paint_id)", "paint_redo_test");
            check(sp->readPixelValue(5, 5) == 12, "pico.canvas_undo: もう一度呼ぶとやり直しになる");

            // 同じ色の所を塗っても何も変わらない(元に戻すの控えも取り直さない)
            pc->causeOnPressStart();
            pc->causeOnPressEnd();
            engine.Run("pico.canvas_undo(paint_id)", "paint_undo_again_test");
            check(sp->readPixelValue(5, 5) == 15,
                  "塗りつぶし: 同色を塗っても控えを上書きしない(直前の塗りつぶしを戻せる)");

            // 塗りつぶしの四角形は、左上へ向かってドラッグしても同じ範囲になる
            pc->canvasClear();
            engine.Run(R"LUA(
                pico.set(paint_id, "canvas_mode", 1)
                pico.set(paint_id, "filled", true)
                pico.set(paint_id, "color", 9)
            )LUA", "paint_rect_setup");
            OSData::touchX = g.x + 8; OSData::touchY = g.y + 6;
            pc->causeOnPressStart();
            OSData::touchX = g.x + 2; OSData::touchY = g.y + 2;
            pc->causeOnPressMove();
            pc->causeOnPressEnd();
            bool rect_ok = true;
            for (int y = 0; y < 12; y++)
                for (int x = 0; x < 20; x++) {
                    const bool in = x >= 2 && x <= 8 && y >= 2 && y <= 6;
                    if (sp->readPixelValue(x, y) != (uint32_t)(in ? 9 : 15)) rect_ok = false;
                }
            check(rect_ok, "四角形(塗りつぶし): 逆向きのドラッグでも始点と終点を対角とする範囲を塗る");

            // 種(シード)の置き場が溢れても取りこぼさない: 白地に黒い点を格子状に置くと
            // 1行ごとに多数の連なりができ、固定長(256)の種の置き場を超える
            pc->resize(120, 120);
            sp = pc->getSprite();
            for (int y = 1; y < 120; y += 2)
                for (int x = 1; x < 120; x += 2) sp->writePixel(x, y, 0);
            engine.Run(R"LUA(
                pico.set(paint_id, "canvas_mode", 5)
                pico.set(paint_id, "color", 10)
            )LUA", "paint_overflow_setup");
            const Rect g2 = pc->getScreenRect();
            OSData::touchX = g2.x; OSData::touchY = g2.y;
            pc->causeOnPressStart();
            pc->causeOnPressEnd();
            bool all_filled = true;
            for (int y = 0; y < 120; y++)
                for (int x = 0; x < 120; x++) {
                    const bool dot = (x % 2 == 1) && (y % 2 == 1);
                    if (sp->readPixelValue(x, y) != (uint32_t)(dot ? 0 : 10)) all_filled = false;
                }
            check(all_filled, "塗りつぶし: 種の置き場が溢れる形でも全域を塗り、黒い点は残す");
            OSData::touchX = 0; OSData::touchY = 0;

            // keep_size: 大きさを変えず、白紙にしてから左上に合わせて読む
            pc->resize(20, 12);
            sp = pc->getSprite();
            for (int y = 0; y < 12; y++)
                for (int x = 0; x < 20; x++) sp->writePixel(x, y, 9);
            HostSd::files["/canvas/tiny.pimg"] = MakePimgBytes(3, 2, false, 1);
            const bool keep_ok = engine.Run(R"LUA(
                check(pico.canvas_load(paint_id, '/canvas/tiny.pimg', true) == true,
                      'pico.canvas_load(keep_size): 読み込める')
                check(pico.get(paint_id, "w") == 20 and pico.get(paint_id, "h") == 12,
                      'pico.canvas_load(keep_size): キャンバスの大きさは変わらない')
            )LUA", "paint_keep_size_test");
            check(keep_ok, "pico.canvas_load(keep_size): テストの実行が成功する");
            check(sp->readPixelValue(0, 0) == 1 && sp->readPixelValue(2, 1) == 1 &&
                      sp->readPixelValue(3, 0) == 15 && sp->readPixelValue(0, 2) == 15,
                  "pico.canvas_load(keep_size): 左上に画像、残りは白");
            engine.Run("pico.canvas_undo(paint_id)", "paint_undo_load_test");
            check(sp->readPixelValue(5, 5) == 9, "pico.canvas_load(keep_size): 読み込みも元に戻せる");

            engine.Run(R"LUA(
                pico.set(paint_id, "undo_enabled", false)
                check(pico.get(paint_id, "undo_enabled") == false, "undo_enabled: 無効に戻すとバッファを手放す")
                check(pico.canvas_undo(paint_id) == false, "pico.canvas_undo: 無効に戻した後はfalse")
                check(pcall(pico.canvas_undo, pico.create("Button")) == false,
                      "pico.canvas_undo: CanvasRaster以外はエラー")
            )LUA", "paint_undo_disable_test");
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
            save_id = pico.show_file_save("/", "memo.pimg")
            check(save_id ~= nil, "pico.show_file_save: ハンドルを返す")
            check(pico.get(save_id, "path") == "/memo.pimg",
                  "pico.show_file_save: 第2引数がファイル名欄の初期値になる")
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

                -- httpsも受け付ける(接続はHttp_Transportが担う)。接続を試みる前に取り消すので実ソケットには触れない
                check(type(pico.http_request("GET", "https://127.0.0.1:1/", nil, nil, function() end)) == "number",
                      "pico.http_request: httpsも開始できる(リクエストIDが返る)")
                pico.http_cancel()

                local huge_body = string.rep("a", 20000) -- kMaxHttpBodyBytes(16KiB)超え
                check(pico.http_request("POST", "http://127.0.0.1:1/", huge_body, "text/plain", function() end) == false,
                      "pico.http_request: 送信ボディが上限を超える場合はfalse")

                local started = pico.http_request("GET", "http://127.0.0.1:1/", nil, nil, function() end)
                check(type(started) == "number", "pico.http_request: 正常な呼び出しはリクエストIDを返す")

                -- 走っているのは1本だけ。2本目以降は順番待ち(最大4本)。それを超えるとfalse
                local queued = {}
                for i = 1, 4 do
                    queued[i] = pico.http_request("GET", "http://127.0.0.1:1/", nil, nil, function() end)
                    check(type(queued[i]) == "number" and queued[i] ~= started, "pico.http_request: 進行中でも順番待ちにできる " .. i)
                end
                check(pico.http_request("GET", "http://127.0.0.1:1/", nil, nil, function() end) == false,
                      "pico.http_request: 待たせられる数(4)を超えるとfalse")
                check(pico.http_cancel(queued[2]) == true, "pico.http_cancel(id): 順番待ちの1本だけ取り消せる")
                check(pico.http_cancel(queued[2]) == false, "pico.http_cancel(id): 二重に取り消すとfalse")
                check(type(pico.http_request("GET", "http://127.0.0.1:1/", nil, nil, function() end)) == "number",
                      "pico.http_request: 取り消した分だけ空く")

                pico.http_cancel()
                local started3 = pico.http_request("GET", "http://127.0.0.1:1/", nil, nil, function() end)
                check(type(started3) == "number", "pico.http_cancel(): 全部取り消したあとは新しいリクエストを開始できる")

                -- opts.headers の誤り
                check(not pcall(pico.http_request, "GET", "http://127.0.0.1:1/", nil, nil, function() end, {headers = {["A\r\nB"] = "x"}}),
                      "pico.http_request: ヘッダ名に改行は使えない")
                check(not pcall(pico.http_request, "GET", "http://127.0.0.1:1/", nil, nil, function() end, {headers = {A = "x\r\nHost: evil"}}),
                      "pico.http_request: ヘッダの値に改行は使えない")
                check(not pcall(pico.http_request, "GET", "http://127.0.0.1:1/", nil, nil, function() end, {headers = {Host = "evil"}}),
                      "pico.http_request: Hostは指定できない")
                check(not pcall(pico.http_request, "GET", "http://127.0.0.1:1/", nil, nil, function() end, {headers = {["Content-Length"] = 5}}),
                      "pico.http_request: Content-Lengthは指定できない")
                check(not pcall(pico.http_request, "GET", "http://127.0.0.1:1/", nil, nil, function() end, {headers = {A = {}}}),
                      "pico.http_request: ヘッダの値は文字列か数値")
                check(not pcall(pico.http_request, "GET", "http://127.0.0.1:1/", nil, nil, function() end, {headers = {A = string.rep("x", 600)}}),
                      "pico.http_request: ヘッダの合計に上限がある")
                check(not pcall(pico.http_request, "GET", "http://127.0.0.1:1/", nil, nil, function() end, 5),
                      "pico.http_request: optsはテーブル")
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

                -- pico.canvas_save/canvas_loadもpico.sd_*と同じくapp_dir_の
                -- 配下だけに閉じる(SdPathAllowed()を共通で通るため)
                perm_canvas_id = pico.create("CanvasRaster")
                check(pico.canvas_save(perm_canvas_id, '/other/outside_canvas.pimg') == false,
                      'pico.canvas_save: app_dir外への保存はfalse')
                check(pico.canvas_save(perm_canvas_id, '/lua/inside_canvas.pimg') == true,
                      'pico.canvas_save: app_dir配下への保存はtrue')
                check(pico.canvas_load(perm_canvas_id, '/other/outside.txt') == false,
                      'pico.canvas_load: app_dir外からの読み込みはfalse')
            )LUA", "confined_sd_test");
            check(ok, "LuaPermissions: app_dir配下への閉じ込めテストの実行が成功する");

            check(HostSd::files.count("/other/outside2.txt") == 0,
                  "pico.sd_write: app_dir外への書き込みは拒否時に実際のファイルを作らない");
            check(HostSd::files.count("/other/outside_canvas.pimg") == 0,
                  "pico.canvas_save: app_dir外への保存は拒否時に実際のファイルを作らない");
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

    // ---- 設定ファイル(pico.config_read/get/write)と、app.cfgの書き込み制限 ----
    {
        LuaEngine cfg_engine(64 * 1024, LuaPermissions{}, "/lua/apps/demo");
        check(cfg_engine.valid(), "設定ファイルテスト用にLuaEngineを構築(app_dir=/lua/apps/demo)");
        if (cfg_engine.valid()) {
            lua_pushcfunction(cfg_engine.raw(), l_check);
            lua_setglobal(cfg_engine.raw(), "check");

            HostSd::files["/lua/apps/demo/app.cfg"] =
                "name=デモ\npermission_network=false\n";
            HostSd::files["/lua/apps/demo/settings.cfg"] =
                "# コメント\nvolume = 30\nname=a\nname=b\n";
            HostSd::files["/lua/apps/other/app.cfg"] = "permission_network=false\n";
            HostSd::files["/other/x.cfg"] = "k=v\n";

            const bool ok = cfg_engine.Run(R"LUA(
                local t = pico.config_read('/lua/apps/demo/settings.cfg')
                check(t ~= nil and t.volume == '30', 'config_read: 前後の空白を除いた値を文字列で返す')
                check(t.name == 'b', 'config_read: 同じキーは後勝ち')
                check(pico.config_read('/lua/apps/demo/none.cfg') == nil, 'config_read: 無いファイルはnil')
                check(pico.config_read('/other/x.cfg') == nil, 'config_read: app_dir外はnil')
                check(pico.config_get('/lua/apps/demo/settings.cfg', 'name') == 'b', 'config_get: 後勝ち')
                check(pico.config_get('/lua/apps/demo/settings.cfg', 'nokey') == nil, 'config_get: 無いキーはnil')
                check(pico.config_get('/lua/apps/demo/app.cfg', 'name') == 'デモ', 'config_get: app.cfgは読める')

                check(pico.config_write('/lua/apps/demo/settings.cfg', 'volume', 55) == true, 'config_write: 整数')
                check(pico.config_write('/lua/apps/demo/settings.cfg', 'ratio', 0.25) == true, 'config_write: 小数')
                check(pico.config_write('/lua/apps/demo/settings.cfg', 'on', true) == true, 'config_write: 真偽値')
                check(pico.config_write('/lua/apps/demo/new.cfg', 'k', 'v v') == true, 'config_write: 新しいファイル')
                local t2 = pico.config_read('/lua/apps/demo/settings.cfg')
                check(t2.volume == '55' and t2.ratio == '0.25' and t2.on == 'true' and t2.name == 'b',
                      'config_write→config_read: 書いた値が読み返せる')
                check(pico.config_get('/lua/apps/demo/new.cfg', 'k') == 'v v', 'config_write: 新規作成した値')
                check(pico.config_write('/lua/apps/demo/settings.cfg', 'x', 'a\npermission_network=true') == false,
                      'config_write: 改行を含む値は拒否')
                check(not pcall(pico.config_write, '/lua/apps/demo/settings.cfg', 'a=b', '1'),
                      'config_write: =を含むキーはエラー')
                check(not pcall(pico.config_write, '/lua/apps/demo/settings.cfg', 'k', {}),
                      'config_write: テーブルの値はエラー')
                check(pico.config_write('/other/x.cfg', 'k', 'w') == false, 'config_write: app_dir外はfalse')

                -- app.cfg(権限を持つ)はどの経路でも書き換えられない
                check(pico.config_write('/lua/apps/demo/app.cfg', 'permission_network', true) == false,
                      'config_write: 自分のapp.cfgは拒否')
                check(pico.sd_write('/lua/apps/demo/app.cfg', 'permission_network=true') == false,
                      'sd_write: 自分のapp.cfgは拒否')
                check(pico.sd_write('/lua/apps/demo/APP.CFG', 'x') == false, 'sd_write: 大文字でも拒否')
                check(pico.sd_write('/lua/apps/demo/app.cfg.', 'x') == false, 'sd_write: 末尾の.でも拒否')
                check(pico.sd_write('/lua/apps/demo/sub/../app.cfg', 'x') == false, 'sd_write: ..を挟んでも拒否')
                check(pico.sd_write('/lua/apps/demo/app.cfg', 'x', true) == false, 'sd_write: 追記も拒否')
                check(pico.sd_remove('/lua/apps/demo/app.cfg') == false, 'sd_remove: app.cfgの削除は拒否')
                check(pico.sd_remove('/lua/apps/demo') == false, 'sd_remove: app_dirごとの削除も拒否')
                check(pico.sd_mkdir('/lua/apps/demo/app.cfg') == false, 'sd_mkdir: app.cfgという名前は拒否')
                local cv = pico.create('CanvasRaster')
                check(pico.canvas_save(cv, '/lua/apps/demo/app.cfg') == false, 'canvas_save: app.cfgは拒否')
                check(pico.sd_write('/lua/apps/demo/sub/app.cfg', 'x') == true,
                      'sd_write: アプリとして読まれない場所のapp.cfgは書ける')
                check(pico.sd_remove('/lua/apps/demo/settings.cfg') == true, 'sd_remove: 普通のファイルは消せる')
            )LUA", "config_test");
            check(ok, "設定ファイルテストの実行が成功する");
            check(HostSd::files["/lua/apps/demo/app.cfg"] == "name=デモ\npermission_network=false\n",
                  "app.cfgの中身が変わっていない");
            check(HostSd::files.count("/lua/apps/demo/settings.cfg.tmp") == 0,
                  "config_write: 一時ファイルが残らない");
        }

        // sd_outside_app_dir=trueでも、他のアプリ(スキャン対象)のapp.cfgへは書けない
        LuaPermissions outside_perm;
        outside_perm.sd_outside_app_dir = true;
        LuaEngine outside_engine(64 * 1024, outside_perm, "/lua/apps/demo");
        check(outside_engine.valid(), "設定ファイルテスト用にLuaEngineを構築(sd_outside_app_dir=true)");
        if (outside_engine.valid()) {
            lua_pushcfunction(outside_engine.raw(), l_check);
            lua_setglobal(outside_engine.raw(), "check");
            const bool ok = outside_engine.Run(R"LUA(
                check(pico.config_write('/lua/apps/other/app.cfg', 'permission_network', true) == false,
                      'config_write: 他のアプリのapp.cfgは拒否')
                check(pico.sd_write('/lua/apps/newapp/app.cfg', 'permission_network=true') == false,
                      'sd_write: 新しいアプリのapp.cfgを作るのも拒否')
                check(pico.sd_remove('/lua/apps/other') == false, 'sd_remove: 他のアプリのディレクトリは拒否')
                check(pico.sd_remove('/lua/apps') == false, 'sd_remove: /lua/appsは拒否')
                check(pico.sd_remove('/') == false, 'sd_remove: ルートは拒否')
                check(pico.config_write('/other/x.cfg', 'k', 'w') == true, 'config_write: 権限があればapp_dir外にも書ける')
                check(pico.config_get('/other/x.cfg', 'k') == 'w', 'config_get: 書いた値')
            )LUA", "config_outside_test");
            check(ok, "sd_outside_app_dir=trueの設定ファイルテストの実行が成功する");
            check(HostSd::files.count("/lua/apps/newapp/app.cfg") == 0, "新しいアプリのapp.cfgが作られていない");
        }
    }

    // ---- 通知(pico.notify / notify_cancel / notify_list / launch_reason) ----
    {
        NotificationFunctions::SetupAt(0);
        //権限が無ければ nil, 理由(引数の書き間違いは権限より先にエラー)
        {
            LuaEngine no_perm(64 * 1024, LuaPermissions{}, "/lua/apps/demo");
            lua_pushcfunction(no_perm.raw(), l_check);
            lua_setglobal(no_perm.raw(), "check");
            const bool ok = no_perm.Run(R"LUA(
                local id, err = pico.notify{ title = 'x' }
                check(id == nil and type(err) == 'string', 'notify: 権限が無ければnilと理由')
                check(not pcall(pico.notify, { body = 'タイトル無し' }), 'notify: titleが無ければ(権限より先に)エラー')
            )LUA", "notify_noperm");
            check(ok, "通知: 権限無しのテストの実行が成功する");
            check(NotificationFunctions::HistoryCount() == 0, "通知: 権限が無ければ何も出ない");
        }

        LuaPermissions perm;
        perm.notify = true;
        LuaEngine ne(64 * 1024, perm, "/lua/apps/demo");
        check(ne.valid(), "通知テスト用にLuaEngineを構築");
        if (ne.valid()) {
            lua_pushcfunction(ne.raw(), l_check);
            lua_setglobal(ne.raw(), "check");
            const bool ok = ne.Run(R"LUA(
                check(pico.launch_reason() == nil, 'launch_reason: 通知から起動されていなければnil')
                check(pico.notify{ title = 'すぐ', body = '本文' } == 0, 'notify: すぐ出すと0')
                local a = pico.notify{ title = '後で', delay_ms = 60000, tag = 'later', data = 'x1' }
                check(type(a) == 'number' and a > 0, 'notify: 予約するとid')
                local b = pico.notify{ title = '毎朝', daily = '07:30' }
                local c = pico.notify{ title = '電池', when = 'battery_low', below = 10 }
                local d = pico.notify{ title = '日時', at = { year = 2027, month = 1, day = 2, hour = 3, min = 4 } }
                check(b and c and d, 'notify: daily / when / at(テーブル)で予約できる')
                check(pico.notify{ title = 'もう1件', every_ms = 60000 } == nil, 'notify: 送り主ごとの上限でnil')
                local l = pico.notify_list()
                check(#l == 4 and l[1].tag == 'later' and l[1].kind == 'delay' and l[2].kind == 'daily' and
                      l[3].kind == 'battery_low' and l[4].kind == 'at', 'notify_list: 自分の予約を返す')
                check(pico.notify_cancel('later') == 1, 'notify_cancel: tagで取り消す')
                check(pico.notify_cancel(b) == 1, 'notify_cancel: idで取り消す')
                check(pico.notify_cancel() == 2, 'notify_cancel: 引数無しで全部')
                check(#pico.notify_list() == 0, 'notify_list: 取り消した後は空')

                check(not pcall(pico.notify, { title = 'x', delay_ms = 10, every_ms = 20000 }), 'notify: いつを2つ指定するとエラー')
                check(not pcall(pico.notify, { title = 'x', every_ms = 100 }), 'notify: every_msが短すぎるとエラー')
                check(not pcall(pico.notify, { title = 'x', daily = '25:00' }), 'notify: dailyの書式違いはエラー')
                check(not pcall(pico.notify, { title = 'x', when = 'rain' }), 'notify: 知らないwhenはエラー')
                check(not pcall(pico.notify, { title = 'x', at = { year = 2027 } }), 'notify: atの足りないフィールドはエラー')
            )LUA", "notify");
            check(ok, "通知: Luaテストの実行が成功する");
            check(NotificationFunctions::HistoryCount() == 1 &&
                  NotificationFunctions::HistoryAt(0)->content.owner == "/lua/apps/demo" &&
                  NotificationFunctions::HistoryAt(0)->content.body == "本文",
                  "通知: 送り主はアプリのディレクトリ");
        }
        //他のアプリの予約は取り消せない/見えない
        {
            NotificationFunctions::When w;
            w.trigger = NotificationFunctions::Trigger::Delay;
            w.delay_ms = 1000;
            NotificationFunctions::Content oc;
            oc.title.assign("他");
            oc.owner.assign("/lua/apps/other");
            oc.tag.assign("t");
            NotificationFunctions::Schedule(oc, w);
            const bool ok = ne.Run(R"LUA(
                check(#pico.notify_list() == 0, 'notify_list: 他のアプリの予約は見えない')
                check(pico.notify_cancel('t') == 0 and pico.notify_cancel() == 0, 'notify_cancel: 他のアプリの予約は消せない')
            )LUA", "notify_other");
            check(ok && NotificationFunctions::RuleCount() == 1, "通知: 他のアプリの予約は残る");
        }
        //起動理由
        {
            LuaEngine re(64 * 1024, perm, "/lua/apps/demo");
            re.SetLaunchReason("tg", "42");
            lua_pushcfunction(re.raw(), l_check);
            lua_setglobal(re.raw(), "check");
            const bool ok = re.Run(R"LUA(
                local tag, data = pico.launch_reason()
                check(tag == 'tg' and data == '42', 'launch_reason: 通知のtagとdataを返す')
            )LUA", "launch_reason");
            check(ok, "通知: launch_reasonのテストの実行が成功する");
        }
        NotificationFunctions::SetupAt(0);
    }

    // ---- 拡張API(Love2Dとの比較で足したもの): 図形・画像の変形・文字幅・WAV・システム・ファイル・キー ----
    {
        LuaEngine ex(256 * 1024);
        check(ex.valid(), "拡張API: エンジンを作れる");
        lua_pushcfunction(ex.raw(), l_check);
        lua_setglobal(ex.raw(), "check");

        if (OSData::frame->sp_w_ == 0) OSData::frame->createSprite(SCREEN_WIDTH, SCREEN_HEIGHT);
        OSData::frame->setClipRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

        // 線の太さ・楕円・三角形・多角形・円弧: dirty矩形と引数の検証
        bool ok = ex.Run("pico.draw_line(10, 10, 20, 10, 1)", "ex_line_thin");
        check(ok && g_last_dirty.x == 10 && g_last_dirty.w == 11 && g_last_dirty.h == 1, "draw_line: 太さ省略は従来どおり");
        ok = ex.Run("pico.draw_line(10, 10, 20, 10, 1, 5)", "ex_line_wide");
        check(ok && g_last_dirty.x < 10 && g_last_dirty.w > 11 && g_last_dirty.h > 1, "draw_line: 太さを指定すると線の周りまでdirtyになる");

        ok = ex.Run("pico.draw_ellipse(50, 60, 5, 3, 1)", "ex_ellipse");
        check(ok && g_last_dirty.x == 45 && g_last_dirty.y == 57 && g_last_dirty.w == 11 && g_last_dirty.h == 7, "draw_ellipse: 外接矩形がdirty");
        ok = ex.Run("pico.fill_ellipse(50, 60, 5, 3, 1)", "ex_fill_ellipse");
        check(ok && g_last_dirty.x == 45 && g_last_dirty.w == 11, "fill_ellipse: 外接矩形がdirty");
        ok = ex.Run("check(not pcall(pico.draw_ellipse, 0, 0, -1, 2, 1), 'draw_ellipse: 負の半径はエラー')", "ex_ellipse_err");
        check(ok, "draw_ellipse: 負の半径テストの実行");

        ok = ex.Run("pico.fill_triangle(10, 10, 30, 12, 20, 40, 1)", "ex_tri");
        check(ok && g_last_dirty.x == 10 && g_last_dirty.y == 10 && g_last_dirty.w == 21 && g_last_dirty.h == 31, "fill_triangle: 外接矩形がdirty");
        ok = ex.Run("pico.draw_triangle(10, 10, 30, 12, 20, 40, 1, 3)", "ex_tri_outline");
        check(ok && g_last_dirty.x < 10 && g_last_dirty.w > 21, "draw_triangle: 太い輪郭は外へ広がる");

        ok = ex.Run(R"LUA(
            check(not pcall(pico.draw_polygon, {1, 2, 3, 4}, 1), 'draw_polygon: 点が2つはエラー')
            check(not pcall(pico.draw_polygon, {1, 2, 3, 4, 5}, 1), 'draw_polygon: 奇数個はエラー')
            check(not pcall(pico.draw_polygon, {1, 2, 3, 4, 5, 'x'}, 1), 'draw_polygon: 数値以外はエラー')
            local many = {}
            for i = 1, 66 do many[i] = i end
            check(not pcall(pico.fill_polygon, many, 1), 'fill_polygon: 33点以上はエラー')
            check(pcall(pico.draw_polygon, {0, 0, 10, 0, 10, 10}, 1, 2), 'draw_polygon: 正常')
        )LUA", "ex_poly_err");
        check(ok, "多角形: 引数の検証");

        // 塗りつぶしは実際にピクセルが入る(スタブのdrawFastHLineは書き込む)
        ok = ex.Run("pico.fill_polygon({10, 10, 20, 10, 20, 20, 10, 20}, 7)", "ex_fill_square");
        check(ok && OSData::frame->readPixelValue(10, 10) == 7 && OSData::frame->readPixelValue(19, 19) == 7
              && OSData::frame->readPixelValue(20, 20) == 0 && OSData::frame->readPixelValue(9, 10) == 0,
              "fill_polygon: 正方形の内側だけが塗られる");
        ok = ex.Run("pico.fill_polygon({100,100, 130,100, 130,110, 110,110, 110,130, 100,130}, 9)", "ex_fill_concave");
        check(ok && OSData::frame->readPixelValue(105, 120) == 9 && OSData::frame->readPixelValue(120, 120) == 0
              && OSData::frame->readPixelValue(120, 105) == 9, "fill_polygon: 凹んだL字も正しく塗れる");

        ok = ex.Run("pico.draw_arc(100, 100, 20, 0, 3.14159, 1, 2)", "ex_arc");
        check(ok && g_last_dirty.x <= 80 && g_last_dirty.w >= 41, "draw_arc: 円全体を覆うdirty");
        ok = ex.Run("pico.fill_arc(150, 150, 20, 0, 1.5708, 6)", "ex_fill_arc");
        check(ok && OSData::frame->readPixelValue(158, 158) == 6 && OSData::frame->readPixelValue(140, 140) == 0,
              "fill_arc: 右下の1/4だけが塗られる(角度は時計回り)");

        // 文字幅
        ok = ex.Run(R"LUA(
            check(pico.text_width('') == 0, 'text_width: 空文字列は0')
            check(pico.text_width('abcd') > pico.text_width('ab'), 'text_width: 長いほど広い')
        )LUA", "ex_text_width");
        check(ok, "text_width: 実行");

        // 画像の拡大縮小・回転・反転
        OSData::SD_usable = true;
        HostSd::files["/img/ex.pimg"] = MakePimgBytes(2, 2, false, 5);
        HostSd::files["/img/ext.pimg"] = MakePimgBytes(2, 2, true, 0); // 全部透過
        ok = ex.Run(R"LUA(
            img = pico.image_load('/img/ex.pimg')
            imt = pico.image_load('/img/ext.pimg')
            check(img and imt, 'draw_image_ex: 画像を読める')
            pico.draw_image_ex(img, 200, 200)
            pico.draw_image_ex(img, 50, 250, 0, 4)
        )LUA", "ex_image_load");
        check(ok, "draw_image_ex: 実行");
        check(OSData::frame->readPixelValue(200, 200) == 5 && OSData::frame->readPixelValue(201, 201) == 5
              && OSData::frame->readPixelValue(202, 200) == 0, "draw_image_ex: 等倍");
        check(OSData::frame->readPixelValue(50, 250) == 5 && OSData::frame->readPixelValue(57, 257) == 5
              && OSData::frame->readPixelValue(58, 250) == 0 && OSData::frame->readPixelValue(50, 258) == 0,
              "draw_image_ex: 4倍で8x8になる");
        ok = ex.Run("pico.draw_image_ex(img, 100, 280, 0, -3, 3, 0, 0)", "ex_flip");
        check(ok && OSData::frame->readPixelValue(99, 280) == 5 && OSData::frame->readPixelValue(94, 285) == 5
              && OSData::frame->readPixelValue(100, 280) == 0, "draw_image_ex: 負のsxで左右反転(xから左へ伸びる)");
        ok = ex.Run("pico.draw_image_ex(img, 150, 20, 1.5707963, 1, 1, 0, 0)", "ex_rot");
        check(ok && OSData::frame->readPixelValue(149, 20) == 5 && OSData::frame->readPixelValue(148, 21) == 5
              && OSData::frame->readPixelValue(151, 20) == 0, "draw_image_ex: 90度回転(時計回り)");
        ok = ex.Run("pico.draw_image_ex(img, 20, 100, 0, 4, 4, 1, 1)", "ex_origin");
        check(ok && OSData::frame->readPixelValue(16, 96) == 5 && OSData::frame->readPixelValue(15, 96) == 0,
              "draw_image_ex: 原点(ox,oy)を(x,y)に合わせる");
        ok = ex.Run("pico.draw_image_ex(imt, 30, 150, 0, 3, 3)", "ex_transparent");
        check(ok && OSData::frame->readPixelValue(30, 150) == 0, "draw_image_ex: 透過画像は何も塗らない");
        ok = ex.Run(R"LUA(
            check(not pcall(pico.draw_image_ex, 999999, 0, 0), 'draw_image_ex: 無効なハンドルはエラー')
            check(not pcall(pico.draw_image_ex, img, 0, 0, 0, 100), 'draw_image_ex: 倍率の上限')
            pico.draw_image_ex(img, 0, 0, 0, 0) -- 倍率0は何もしない
        )LUA", "ex_image_err");
        check(ok, "draw_image_ex: エラー系");

        // WAV(何も鳴らしていないときの答え)
        ok = ex.Run(R"LUA(
            check(pico.wav_paused() == false, 'wav_paused: 鳴らしていなければfalse')
            check(pico.wav_pause(true) == false, 'wav_pause: 鳴らしていなければfalse')
            check(math.type(pico.wav_position()) == 'integer' and math.type(pico.wav_duration()) == 'integer',
                  'wav_position/duration: 整数(ms)')
            check(pico.wav_position() <= pico.wav_duration() or pico.wav_duration() == 0, 'wav_position: 長さを越えない')
            check(pico.wav_seek(-5) == false, 'wav_seek: 負はfalse')
        )LUA", "ex_wav");
        check(ok, "wav: 一時停止・位置・シーク");

        // システム
        ok = ex.Run(R"LUA(
            local a = pico.millis()
            check(type(a) == 'number' and a >= 0, 'millis: 数値')
            check(pico.battery() == nil, 'battery: まだ読めていなければnil')
        )LUA", "ex_system");
        check(ok, "system: millis/battery");
        BatteryFunctions::Setup();
        ok = ex.Run(R"LUA(
            local p, v, ext = pico.battery()
            check(p >= 0 and p <= 100 and v >= 3.0 and v <= 4.2 and type(ext) == 'boolean', 'battery: 残量・電圧・給電')
        )LUA", "ex_battery");
        check(ok, "battery: 読めたあと");

        // ファイルの情報・部分読み
        HostSd::files["/ex/data.bin"] = std::string("0123456789ABCDEF");
        ok = ex.Run(R"LUA(
            local st = pico.sd_stat('/ex/data.bin')
            check(st and st.size == 16 and st.is_dir == false, 'sd_stat: サイズとis_dir')
            check(pico.sd_stat('/ex/nothing') == nil, 'sd_stat: 無ければnil')
            check(pico.sd_read_part('/ex/data.bin', 4, 4) == '4567', 'sd_read_part: 途中から読める')
            check(pico.sd_read_part('/ex/data.bin', 12, 100) == 'CDEF', 'sd_read_part: 終わりで短く返る')
            check(pico.sd_read_part('/ex/data.bin', 99, 4) == '', 'sd_read_part: 終わりより先は空')
            check(pico.sd_read_part('/ex/nothing', 0, 4) == nil, 'sd_read_part: 無ければnil')
            check(not pcall(pico.sd_read_part, '/ex/data.bin', -1, 4), 'sd_read_part: 負のoffsetはエラー')
        )LUA", "ex_sd");
        check(ok, "sd_stat/sd_read_part");

        // 物理キーボード: on_key
        {
            using KeyInputFunctions::Event;
            using KeyInputFunctions::Key;
            Event a; a.key = Key::Char; a.cp = 0x3042; // あ
            check(!ex.DispatchKey(a), "on_key: 登録が無ければ取らない");
            ok = ex.Run(R"LUA(
                log = {}
                pico.on_key(function(key, mods)
                    log[#log + 1] = key .. (mods.ctrl and '+C' or '') .. (mods.shift and '+S' or '')
                    return key ~= 'tab'
                end)
            )LUA", "ex_on_key");
            check(ok, "on_key: 登録できる");
            Event c; c.key = Key::Char; c.cp = 'x'; c.mods = KeyInputFunctions::Ctrl;
            Event l; l.key = Key::Left; l.mods = KeyInputFunctions::Shift;
            Event t; t.key = Key::Tab;
            check(ex.DispatchKey(a), "on_key: 真を返せば取った扱い");
            check(ex.DispatchKey(c), "on_key: Ctrl付きの文字");
            check(ex.DispatchKey(l), "on_key: 特殊キー");
            check(!ex.DispatchKey(t), "on_key: 偽を返せば取らない(キー盤へ回る)");
            ok = ex.Run(R"LUA(
                check(log[1] == 'あ' and log[2] == 'x+C' and log[3] == 'left+S' and log[4] == 'tab',
                      'on_key: 文字はUTF-8・特殊キーは名前・修飾キーが渡る')
                check(not pcall(pico.on_key, 5), 'on_key: 関数以外はエラー')
                pico.on_key(nil)
            )LUA", "ex_on_key_check");
            check(ok, "on_key: 引数の確認");
            check(!ex.DispatchKey(a), "on_key: nilで解除できる");
            // コールバックがエラーなら1回だけダイアログを出して以降は呼ばない
            ok = ex.Run("pico.on_key(function() error('boom') end)", "ex_on_key_err");
            check(ok, "on_key: エラーになる関数を登録できる");
            check(ex.DispatchKey(a), "on_key: エラーのときは打鍵を消費したことにする");
            check(!ex.DispatchKey(a), "on_key: エラーの後は呼ばれない");
        }
        OSData::frame->clearClipRect();
        OSData::SD_usable = false;
    }

    // =====================================================================
    // 2026-10-05: ウィジェットアプリ向けの5つの追加機能
    // =====================================================================

    // ---- JSON(pico.json_decode / json_encode / json_null) ----
    {
        LuaEngine je(256 * 1024);
        check(je.valid(), "JSONテスト用にLuaEngineを構築");
        lua_pushcfunction(je.raw(), l_check);
        lua_setglobal(je.raw(), "check");
        const bool ok = je.Run(R"LUA(
            local t = pico.json_decode('{"a":1,"b":[1,2.5,"x",true,false],"c":{"d":"e"},"n":null}')
            check(t.a == 1 and math.type(t.a) == "integer", "json_decode: 整数は整数のまま")
            check(t.b[2] == 2.5 and math.type(t.b[2]) == "float", "json_decode: 小数は小数")
            check(t.b[3] == "x" and t.b[4] == true and t.b[5] == false, "json_decode: 文字列と真偽")
            check(t.c.d == "e", "json_decode: 入れ子")
            check(t.n == nil, "json_decode: nullはnil(キーが消える)")
            local k = pico.json_decode('{"n":null,"l":[1,null,3]}', true)
            check(k.n == pico.json_null and k.l[2] == pico.json_null, "json_decode: keep_nullで目印になる")
            check(pico.json_decode('"a\\n\\t\\"\\\\\\/b"') == 'a\n\t"\\/b', "json_decode: エスケープ")
            check(pico.json_decode('"\\u3042\\u00e9"') == "あé", "json_decode: \\uをUTF-8へ")
            check(pico.json_decode('"\\ud83d\\ude00"') == "\u{1F600}", "json_decode: サロゲートペア")
            check(pico.json_decode('"\\ud83d"') == "\u{FFFD}", "json_decode: 対にならないサロゲートはU+FFFD")
            check(pico.json_decode("  [ ]  ")[1] == nil, "json_decode: 空の配列と前後の空白")
            check(pico.json_decode("-12") == -12 and pico.json_decode("1e3") == 1000.0, "json_decode: 数値(負・指数)")
            check(pico.json_decode("123456789012345678901234567890") > 1e29, "json_decode: 範囲外の整数は小数へ")
            check(pico.json_decode("true") == true and pico.json_decode("null") == nil, "json_decode: 単独の値")

            local function bad(s)
                local v, e = pico.json_decode(s)
                return v == nil and type(e) == "string"
            end
            check(bad("{"), "json_decode: 閉じていないオブジェクト")
            check(bad("[1,]"), "json_decode: 末尾のカンマ")
            check(bad("{'a':1}"), "json_decode: 単引用符")
            check(bad("01"), "json_decode: 先頭の0")
            check(bad("1 2"), "json_decode: 余分な文字")
            check(bad('"abc'), "json_decode: 閉じていない文字列")
            check(bad('"\\x"'), "json_decode: 不正なエスケープ")
            check(bad(""), "json_decode: 空")
            check(bad(string.rep("[", 40) .. string.rep("]", 40)), "json_decode: 入れ子が深すぎる")
            check(bad(string.rep("1", 65537)), "json_decode: 入力が大きすぎる")
            local _, e = pico.json_decode('{"a":}')
            check(e:find("位置") ~= nil, "json_decode: 誤りの位置が分かる")

            local function rt(v) return pico.json_decode(pico.json_encode(v)) end
            local r = rt({name = "たろう", list = {1, 2, 3}, nested = {x = {y = {z = true}}}, f = 1.5})
            check(r.name == "たろう" and r.list[3] == 3 and r.nested.x.y.z == true and r.f == 1.5, "json_encode: 往復")
            check(pico.json_encode({1, 2, 3}) == "[1,2,3]", "json_encode: 配列")
            check(pico.json_encode({a = 1}) == '{"a":1}', "json_encode: オブジェクト")
            check(pico.json_encode({}) == "[]", "json_encode: 空のテーブルは[]")
            check(pico.json_encode("a\"b\\c\n\1") == '"a\\"b\\\\c\\n\\u0001"', "json_encode: 文字列のエスケープ")
            check(pico.json_encode(nil) == "null" and pico.json_encode(true) == "true", "json_encode: nil/真偽")
            check(pico.json_encode(pico.json_null) == "null", "json_encode: json_null")
            check(pico.json_encode(3) == "3" and pico.json_encode(2.5) == "2.5", "json_encode: 数値")
            check(pico.json_encode({1, nil, 3}) == "[1,null,3]", "json_encode: 穴のある配列はnullで埋める")
            check(pico.json_encode({[1000] = 1}) == nil, "json_encode: まばらな整数キーは配列にしない(キーが文字列でないので失敗)")

            local v, e1 = pico.json_encode(0/0)
            check(v == nil and e1 ~= nil, "json_encode: NaNは書けない")
            v, e1 = pico.json_encode(function() end)
            check(v == nil and e1 ~= nil, "json_encode: 関数は書けない")
            v, e1 = pico.json_encode({[1.5] = 1, [true] = 2})
            check(v == nil and e1 ~= nil, "json_encode: 文字列以外のキーは書けない")
            local cyc = {}
            cyc.self = cyc
            v, e1 = pico.json_encode(cyc)
            check(v == nil and e1 ~= nil, "json_encode: 循環参照は失敗する(無限再帰しない)")
        )LUA", "json_test");
        check(ok, "JSON: テストスクリプトの実行が成功する");
    }

    // ---- タイマー(pico.after / every / cancel) ----
    {
        LuaEngine te(64 * 1024);
        check(te.valid(), "タイマーテスト用にLuaEngineを構築");
        lua_pushcfunction(te.raw(), l_check);
        lua_setglobal(te.raw(), "check");
        bool ok = te.Run(R"LUA(
            log = {}
            h_after = pico.after(100, function(h) log[#log + 1] = "after" end)
            h_every = pico.every(30, function(h) log[#log + 1] = "every" end)
            h_cancel = pico.after(50, function() log[#log + 1] = "cancelled" end)
            check(type(h_after) == "number" and h_after ~= h_every, "pico.after/every: ハンドルが返る")
            check(pico.cancel(h_cancel) == true, "pico.cancel: 取り消せる")
            check(pico.cancel(h_cancel) == false, "pico.cancel: 二重に取り消すとfalse")
            check(not pcall(pico.after, -1, function() end), "pico.after: 負はエラー")
            check(not pcall(pico.every, 0, function() end), "pico.every: 0はエラー")
            check(not pcall(pico.after, 10, 5), "pico.after: 関数以外はエラー")
        )LUA", "timer_setup");
        check(ok, "タイマー: 準備スクリプトの実行が成功する");

        auto log_str = [&]() {
            lua_getglobal(te.raw(), "log");
            std::string r;
            const int n = (int)lua_rawlen(te.raw(), -1);
            for (int i = 1; i <= n; i++) {
                lua_rawgeti(te.raw(), -1, i);
                r += lua_tostring(te.raw(), -1);
                r += ",";
                lua_pop(te.raw(), 1);
            }
            lua_pop(te.raw(), 1);
            return r;
        };
        te.UpdateTimers(29);
        check(log_str() == "", "タイマー: 時間になるまで鳴らない");
        te.UpdateTimers(1);
        check(log_str() == "every,", "タイマー: every(30)が30msで鳴る");
        te.UpdateTimers(70);   // 100ms: afterが鳴る。everyは遅れても1回だけ(溜めない)
        check(log_str() == "every,after,every,", "タイマー: 遅れても1回だけ・afterは1度だけ(スロット順)");
        te.UpdateTimers(500);
        check(log_str() == "every,after,every,every,", "タイマー: 大きく遅れても溜めて鳴らさない");
        te.UpdateTimers(30);
        check(log_str() == "every,after,every,every,every,", "タイマー: afterは再び鳴らない・everyは続く");

        ok = te.Run(R"LUA(
            check(pico.cancel(h_after) == false, "pico.cancel: 鳴り終わったafterはfalse")
            check(pico.cancel(h_every) == true, "pico.cancel: everyを止められる")
            -- コールバックの中から自分を止める・新しいタイマーを作る
            n_self = 0
            h_self = pico.every(10, function(h)
                n_self = n_self + 1
                pico.cancel(h)
                pico.after(10, function() n_chain = true end)
            end)
        )LUA", "timer_cancel");
        check(ok, "タイマー: 取り消しのスクリプトが成功する");
        te.UpdateTimers(10);
        te.UpdateTimers(10);
        te.UpdateTimers(10);
        ok = te.Run(R"LUA(
            check(n_self == 1, "タイマー: コールバックの中で自分をcancelすると1回で止まる")
            check(n_chain == true, "タイマー: コールバックの中で作ったタイマーも動く")
            -- 上限(16個)
            local hs = {}
            for i = 1, 16 do hs[i] = pico.after(1000000, function() end) end
            check(hs[16] ~= nil, "pico.after: 16個まで作れる")
            check(pico.after(1000, function() end) == nil, "pico.after: 上限を超えるとnil")
            pico.cancel(hs[1])
            local again = pico.after(1000, function() end)
            check(again ~= nil and again ~= hs[1], "pico.after: 空いたスロットは別のハンドルで再利用される")
            check(pico.cancel(hs[1]) == false, "pico.cancel: 解放済みハンドルは別のタイマーを巻き込まない")
            check(pico.cancel(again) == true, "pico.cancel: 再利用したタイマーは取り消せる")
            -- エラーになるタイマーは止まり、ダイアログは1回だけ
            err_count = 0
            pico.every(5, function() err_count = err_count + 1; error("boom") end)
        )LUA", "timer_limit");
        check(ok, "タイマー: 上限のスクリプトが成功する");
        te.UpdateTimers(5);
        te.UpdateTimers(5);
        te.UpdateTimers(5);
        ok = te.Run("check(err_count == 1, 'タイマー: エラーになった繰り返しタイマーはそれ以降鳴らない')", "timer_err");
        check(ok, "タイマー: エラー後の確認が成功する");
    }

    // ---- タッチのイベント引数(id, x, y, lx, ly, dx, dy) ----
    {
        const bool setup_ok = engine.Run(R"LUA(
            ev_btn = pico.create("Button")
            pico.set(ev_btn, "x", 10)
            pico.set(ev_btn, "y", 20)
            ev_log = {}
            for _, name in ipairs({"press_start", "press_move", "press_end", "press_out"}) do
                pico.on(ev_btn, name, function(id, x, y, lx, ly, dx, dy)
                    ev_log[#ev_log + 1] = {name = name, id = id, x = x, y = y, lx = lx, ly = ly, dx = dx, dy = dy}
                end)
            end
        )LUA", "touch_args_setup");
        check(setup_ok, "タッチのイベント引数: 準備スクリプトの実行が成功する");
        lua_State* L2 = engine.raw();
        lua_getglobal(L2, "ev_btn");
        const WidgetId ev_id = (WidgetId)lua_tointeger(L2, -1);
        lua_pop(L2, 1);
        Widget* w = WidgetRegistry::Resolve(ev_id);
        check(w != nullptr, "タッチのイベント引数: ボタンが解決できる");
        if (w) {
            const Rect r = w->getScreenRect();
            OSData::touchX = (int16_t)(r.x + 7);
            OSData::touchY = (int16_t)(r.y + 3);
            w->causeOnPressStart();
            OSData::touchX = (int16_t)(r.x + 12);
            OSData::touchY = (int16_t)(r.y + 1);
            w->causeOnPressMove();
            w->causeOnPressEnd();
            const bool ok = engine.Run(R"LUA(
                local a, b, c = ev_log[1], ev_log[2], ev_log[3]
                check(a.name == "press_start" and a.id == ev_btn, "press_start: 第1引数はid")
                check(a.lx == 7 and a.ly == 3, "press_start: ウィジェット内の座標(lx, ly)")
                check(a.x == a.lx + (a.x - a.lx) and a.x - a.lx == b.x - b.lx, "press_start: 画面座標x,yはlx,lyと整合する")
                check(a.dx == 0 and a.dy == 0, "press_start: 移動量は0")
                check(b.name == "press_move" and b.lx == 12 and b.ly == 1, "press_move: 座標")
                check(b.dx == 5 and b.dy == -2, "press_move: 前のイベントからの移動量")
                check(c.name == "press_end" and c.dx == 0 and c.dy == 0, "press_end: 動いていなければ移動量は0")
            )LUA", "touch_args_check");
            check(ok, "タッチのイベント引数: 検証スクリプトの実行が成功する");
        }
    }

    // ---- 画面をまたぐ受け渡し(pico.args / pico.pop(result) / on_suspend・on_resume / store) ----
    {
        HostSd::files.clear();
        OSData::SD_usable = true;
        LuaEngine se(128 * 1024, LuaPermissions{}, "/app");
        se.SetScriptPath("/app/main.lua");
        se.SetSceneArgs("{\"level\":3,\"name\":\"x\"}", "/parent/main.lua");
        lua_pushcfunction(se.raw(), l_check);
        lua_setglobal(se.raw(), "check");
        bool ok = se.Run(R"LUA(
            local a = pico.args()
            check(a.level == 3 and a.name == "x", "pico.args: 渡された引数が読める")

            -- 引数・結果の誤り
            check(not pcall(pico.push_scene, "/app/sub.lua", function() end), "push_scene: 関数は渡せない")
            check(not pcall(pico.push_scene, "/app/sub.lua", {big = string.rep("x", 2000)}), "push_scene: 大きすぎる引数はエラー")
            check(not pcall(pico.pop, {big = string.rep("x", 2000)}), "pop: 大きすぎる結果はエラー")

            -- store
            check(pico.store_load() == nil, "store_load: まだ無ければnil")
            check(pico.store_save({score = 10, names = {"a", "b"}}) == true, "store_save: 保存できる")
            local s = pico.store_load()
            check(s.score == 10 and s.names[2] == "b", "store_load: 保存した値が読める")
            check(pico.sd_exists("/app/store.json"), "store: アプリのフォルダのstore.jsonに置かれる")
            check(not pcall(pico.store_save, function() end), "store_save: JSONにできない値はエラー")
            check(not pico.sd_exists("/app/store.json.tmp"), "store_save: 一時ファイルが残らない")

            suspended = 0
            function on_suspend() suspended = suspended + 1; return {pos = 5, tags = {"k"}} end
            got_state, got_result = nil, nil
            function on_resume(st) got_state = st end
            function on_result(r) got_result = r end
        )LUA", "scene_args_test");
        check(ok, "画面の受け渡し: テストスクリプトの実行が成功する");

        // pico.pop(result) は親宛ての待ち箱に積む(Pop自体は要求を登録するだけ)
        ok = se.Run("pico.pop({picked = 'red', n = 2})", "pop_result");
        check(ok, "pico.pop(result): 呼べる");
        FixedString<PICO_STR_1KiB> res;
        check(!LuaScene::TakeResult("/other/main.lua", res), "TakeResult: 別の画面宛てには渡さない");
        check(LuaScene::TakeResult("/parent/main.lua", res), "TakeResult: 親宛ての結果を受け取れる");
        check(strstr(res.c_str(), "\"picked\":\"red\"") != nullptr, "TakeResult: 結果がJSONで入っている");
        check(!LuaScene::TakeResult("/parent/main.lua", res), "TakeResult: 受け取ったら空になる(1回きり)");

        // on_suspend / on_resume / on_result
        FixedString<PICO_STR_2KiB> state;
        check(se.CallSuspend(state), "CallSuspend: on_suspendの戻り値を保存する");
        check(strstr(state.c_str(), "\"pos\":5") != nullptr, "CallSuspend: JSONで返る");
        se.CallWithJson("on_resume", state.c_str());
        se.CallWithJson("on_result", "{\"ok\":true}");
        ok = se.Run(R"LUA(
            check(suspended == 1, "on_suspend: 1回呼ばれる")
            check(got_state.pos == 5 and got_state.tags[1] == "k", "on_resume: 保存した状態が渡る")
            check(got_result.ok == true, "on_result: 結果が渡る")
            function on_suspend() return nil end
        )LUA", "scene_args_check");
        check(ok, "画面の受け渡し: 確認スクリプトが成功する");
        check(!se.CallSuspend(state), "CallSuspend: nilを返せば保存しない");
        se.CallWithJson("no_such_function", "{}");   // 無い関数は何もしない
        check(true, "CallWithJson: 無い関数は無視される");

        // 別のアプリフォルダの引数が無い場合
        LuaEngine plain(64 * 1024);
        lua_pushcfunction(plain.raw(), l_check);
        lua_setglobal(plain.raw(), "check");
        ok = plain.Run("check(pico.args() == nil, 'pico.args: 引数が無ければnil')", "no_args");
        check(ok, "pico.args: 引数なし");
        HostSd::files.clear();
    }

    // ---- require ----
    {
        HostSd::files.clear();
        OSData::SD_usable = true;
        HostSd::files["/rq/util.lua"] = "local M = {}\nfunction M.double(x) return x * 2 end\nM.loaded_count = (LOADED or 0) + 1\nLOADED = M.loaded_count\nreturn M\n";
        HostSd::files["/rq/a/b.lua"] = "return { name = 'ab', util = require('util') }\n";
        HostSd::files["/rq/pkg/init.lua"] = "return 'pkg-init'\n";
        HostSd::files["/rq/noret.lua"] = "NORET_RAN = (NORET_RAN or 0) + 1\n";
        HostSd::files["/rq/cyc1.lua"] = "return require('cyc2')\n";
        HostSd::files["/rq/cyc2.lua"] = "return require('cyc1')\n";
        HostSd::files["/rq/syntax.lua"] = "return = =\n";
        HostSd::files["/rq/boom.lua"] = "error('module failed')\n";
        HostSd::files["/rq/dyn1.lua"] = "return 'dyn1'\n";
        HostSd::files["/outside/secret.lua"] = "return 'secret'\n";

        LuaEngine re(256 * 1024, LuaPermissions{}, "/rq");
        lua_pushcfunction(re.raw(), l_check);
        lua_setglobal(re.raw(), "check");
        const bool ok = re.Run(R"LUA(
            local util = require("util")
            check(util.double(21) == 42, "require: モジュールを読んで使える")
            check(require("util") == util, "require: 2回目は同じ値(キャッシュ)")
            check(LOADED == 1, "require: 実行は1回だけ")
            check(pico.require("util") == util, "pico.require: requireと同じ")
            local ab = require("a.b")
            check(ab.name == "ab" and ab.util == util, "require: ドット区切りはディレクトリ・入れ子のrequireも先読みされる")
            check(require "pkg" == "pkg-init", "require: <名前>/init.lua")
            check(require("noret") == true and NORET_RAN == 1, "require: 戻り値が無ければtrue")
            require('noret')
            check(NORET_RAN == 1, "require: 戻り値が無くても2回実行しない")

            local ok, err = pcall(require, "cyc1")
            check(not ok and tostring(err):find("循環"), "require: 循環はエラー")
            ok, err = pcall(require, "syntax")
            check(not ok, "require: 構文エラーはエラー")
            ok, err = pcall(require, "boom")
            check(not ok and tostring(err):find("module failed"), "require: モジュールの実行時エラーが伝わる")
            ok, err = pcall(require, "boom")
            check(not ok and not tostring(err):find("循環"), "require: 失敗した後に再度呼んでも循環扱いにならない")
            ok, err = pcall(require, "no.such.mod")
            check(not ok and tostring(err):find("見つかりません"), "require: 無いモジュール")
            ok = pcall(require, "../outside/secret")
            check(not ok, "require: ..は使えない")
            ok = pcall(require, "/outside/secret")
            check(not ok, "require: 絶対パスは使えない")
            ok = pcall(require, "")
            check(not ok, "require: 空の名前")
            ok = pcall(require, "a b")
            check(not ok, "require: 使えない文字")

            -- 組み立てた名前: トップレベル近くなら実行中に読める
            local name = "dyn" .. 1
            check(require(name) == "dyn1", "require: 組み立てた名前もトップレベルなら読める")
            -- 深い所から組み立てた名前で呼ぶと断る(先読みされていないもの)
            HOSTILE = "dyn1"
        )LUA", "/rq/main.lua");
        check(ok, "require: テストスクリプトの実行が成功する");

        // app_dir外は読めない(sd_outside_app_dirなし)
        LuaEngine ce(64 * 1024, LuaPermissions{}, "/rq/a");
        lua_pushcfunction(ce.raw(), l_check);
        lua_setglobal(ce.raw(), "check");
        const bool ok2 = ce.Run("check(not pcall(require, 'util'), 'require: app_dirの外のモジュールは読めない')", "/rq/a/main.lua");
        check(ok2, "require: app_dir外の確認");

        // 深い所からの組み立てた名前
        LuaEngine de(128 * 1024, LuaPermissions{}, "/rq");
        lua_pushcfunction(de.raw(), l_check);
        lua_setglobal(de.raw(), "check");
        const bool ok3 = de.Run(R"LUA(
            local function d1(n)
                if n == 0 then return (pcall(require, "dy" .. "n1")) end
                local r = d1(n - 1)   -- 末尾呼び出しにしない(深さを稼ぐ)
                return r
            end
            local ok = d1(10)
            check(not ok, "require: 深い呼び出しの奥で、先読みされていない名前を読もうとすると断る")
            local nm = "dy" .. "n1"
            check(require(nm) == "dyn1", "require: 浅くなれば(先読みされていなくても)読める")
        )LUA", "/rq/main.lua");
        check(ok3, "require: 深い所の確認");
        HostSd::files.clear();
    }

    // ---- HTTP(実ソケット: 127.0.0.1に立てた小さなサーバ相手) ----
    {
        struct Srv {
            int lfd = -1;
            int port = 0;
            std::atomic<bool> stop{false};
            std::mutex mu;
            std::vector<std::string> requests;
            std::thread th;
        } srv;
        srv.lfd = socket(AF_INET, SOCK_STREAM, 0);
        int one = 1;
        setsockopt(srv.lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        bool listening = bind(srv.lfd, (sockaddr*)&addr, sizeof(addr)) == 0 && listen(srv.lfd, 8) == 0;
        socklen_t alen = sizeof(addr);
        if (listening) {
            getsockname(srv.lfd, (sockaddr*)&addr, &alen);
            srv.port = ntohs(addr.sin_port);
        }
        check(listening, "HTTPテスト用のローカルサーバを立てる");
        if (listening) {
            srv.th = std::thread([&srv]() {
                while (!srv.stop) {
                    fd_set fds;
                    FD_ZERO(&fds);
                    FD_SET(srv.lfd, &fds);
                    timeval tv{0, 50000};
                    if (select(srv.lfd + 1, &fds, nullptr, nullptr, &tv) <= 0) continue;
                    int c = accept(srv.lfd, nullptr, nullptr);
                    if (c < 0) continue;
                    std::string req;
                    char buf[2048];
                    size_t body_need = 0;
                    size_t head_end = std::string::npos;
                    while (true) {
                        ssize_t n = recv(c, buf, sizeof(buf), 0);
                        if (n <= 0) break;
                        req.append(buf, (size_t)n);
                        if (head_end == std::string::npos) {
                            head_end = req.find("\r\n\r\n");
                            if (head_end != std::string::npos) {
                                size_t p = req.find("Content-Length: ");
                                if (p != std::string::npos && p < head_end) body_need = (size_t)atoi(req.c_str() + p + 16);
                            }
                        }
                        if (head_end != std::string::npos && req.size() >= head_end + 4 + body_need) break;
                    }
                    { std::lock_guard<std::mutex> lk(srv.mu); srv.requests.push_back(req); }
                    std::string body, status = "200 OK", ctype = "text/plain";
                    if (req.compare(0, 10, "GET /hello") == 0) {
                        body = "hello";
                    } else if (req.compare(0, 9, "GET /json") == 0) {
                        body = "{\"ok\":true,\"n\":[1,2,3]}";
                        ctype = "application/json";
                    } else if (req.compare(0, 8, "GET /big") == 0) {
                        body.assign(40000, 'x');
                    } else if (req.compare(0, 10, "POST /echo") == 0) {
                        body = req.substr(head_end + 4);
                    } else {
                        status = "500 Internal Server Error";
                        body = "oops";
                    }
                    std::string resp = "HTTP/1.1 " + status + "\r\nContent-Type: " + ctype +
                        "\r\nETag: \"v1\"\r\nContent-Length: " + std::to_string(body.size()) +
                        "\r\nConnection: close\r\n\r\n" + body;
                    size_t off = 0;
                    while (off < resp.size()) {
                        ssize_t n = send(c, resp.data() + off, resp.size() - off, 0);
                        if (n <= 0) break;
                        off += (size_t)n;
                    }
                    close(c);
                }
            });

            LuaPermissions np;
            np.network = true;
            LuaEngine he(256 * 1024, np);
            check(he.valid(), "HTTP結合テスト用にLuaEngineを構築");
            lua_pushcfunction(he.raw(), l_check);
            lua_setglobal(he.raw(), "check");
            lua_pushfstring(he.raw(), "http://127.0.0.1:%d", srv.port);
            lua_setglobal(he.raw(), "BASE");
            HostSd::files.clear();
            OSData::SD_usable = true;

            bool ok = he.Run(R"LUA(
                order = {}
                R = {}
                id1 = pico.http_request("GET", BASE .. "/hello", nil, nil, function(ok, st, body, err, hdr, info)
                    order[#order + 1] = 1
                    R.hello = {ok = ok, st = st, body = body, err = err, ct = hdr["content-type"], etag = hdr["etag"],
                               cl = hdr["content-length"], size = info.size}
                end, {headers = {["X-Test"] = "abc", Authorization = "Bearer tok", ["X-Num"] = 42}})
                id2 = pico.http_request("POST", BASE .. "/echo", '{"a":1}', "application/json", function(ok, st, body)
                    order[#order + 1] = 2
                    R.echo = {ok = ok, st = st, body = body}
                end)
                id3 = pico.http_request("GET", BASE .. "/json", nil, nil, function(ok, st, body)
                    order[#order + 1] = 3
                    R.json = pico.json_decode(body)
                end)
                id4 = pico.http_request("GET", BASE .. "/nope", nil, nil, function(ok, st, body)
                    order[#order + 1] = 4
                    R.fail = {ok = ok, st = st, body = body}
                end)
                id5 = pico.http_request("GET", BASE .. "/big", nil, nil, function(ok, st, body, err)
                    order[#order + 1] = 5
                    R.big = {ok = ok, body = body, err = err}
                end)
                check(id1 and id2 and id3 and id4 and id5, "HTTP: 5本とも受け付ける(1本走って4本待つ)")
                check(pico.http_request("GET", BASE .. "/hello", nil, nil, function() end) == false, "HTTP: 6本目は断る")
            )LUA", "http_net_start");
            check(ok, "HTTP結合: 開始スクリプトが成功する");

            for (int i = 0; i < 4000 && he.HttpBusy(); i++) {
                he.UpdateHttp();
                usleep(1000);
            }
            check(!he.HttpBusy(), "HTTP結合: 全部終わる");

            ok = he.Run(R"LUA(
                check(#order == 5 and order[1] == 1 and order[2] == 2 and order[3] == 3 and order[4] == 4 and order[5] == 5,
                      "HTTP: 順番どおりに1本ずつ走る")
                check(R.hello.ok and R.hello.st == 200 and R.hello.body == "hello", "HTTP: GETの結果")
                check(R.hello.ct == "text/plain" and R.hello.etag == "v1" and R.hello.cl == 5, "HTTP: 応答ヘッダが読める")
                check(R.hello.size == 5, "HTTP: info.size")
                check(R.echo.ok and R.echo.body == '{"a":1}', "HTTP: POSTのボディが届く")
                check(R.json.ok == true and R.json.n[3] == 3, "HTTP: 応答をjson_decodeできる")
                check(R.fail.ok and R.fail.st == 500 and R.fail.body == "oops", "HTTP: 500でも本文が読める")
                check(R.big.ok == false and R.big.body == nil and R.big.err ~= nil, "HTTP: 16KiBを超える応答はメモリでは受けない")
            )LUA", "http_net_check");
            check(ok, "HTTP結合: 結果の確認が成功する");

            {
                std::lock_guard<std::mutex> lk(srv.mu);
                const std::string& r = srv.requests.empty() ? std::string() : srv.requests[0];
                check(r.find("X-Test: abc\r\n") != std::string::npos, "HTTP: 足したヘッダが送られる");
                check(r.find("Authorization: Bearer tok\r\n") != std::string::npos, "HTTP: Authorizationが送られる");
                check(r.find("X-Num: 42\r\n") != std::string::npos, "HTTP: 数値のヘッダ値");
                check(r.find("Host: 127.0.0.1") != std::string::npos && r.find("User-Agent: pico-os/1") != std::string::npos,
                      "HTTP: 標準のヘッダも付く");
                const std::string& r2 = srv.requests.size() > 1 ? srv.requests[1] : std::string();
                check(r2.find("Content-Type: application/json\r\n") != std::string::npos
                      && r2.find("Content-Length: 7\r\n") != std::string::npos, "HTTP: POSTのContent-Type/Length");
                check(r2.find("X-Test") == std::string::npos, "HTTP: ヘッダは次のリクエストへ引き継がれない");
            }

            // ファイルへ直接保存(16KiBを超えるもの)
            ok = he.Run(R"LUA(
                dl = nil
                pico.http_request("GET", BASE .. "/big", nil, nil, function(ok, st, body, err, hdr, info)
                    dl = {ok = ok, st = st, body = body, err = err, size = info.size, saved = info.saved}
                end, {save_to = "/dl/big.bin"})
                dl_bad = nil
                pico.http_request("GET", BASE .. "/nope", nil, nil, function(ok, st, body, err, hdr, info)
                    dl_bad = {ok = ok, st = st, saved = info.saved}
                end, {save_to = "/dl/err.bin"})
                dl_cancel = pico.http_request("GET", BASE .. "/big", nil, nil, function() CANCEL_CALLED = true end, {save_to = "/dl/cancel.bin"})
            )LUA", "http_dl_start");
            check(ok, "HTTP保存: 開始スクリプトが成功する");
            for (int i = 0; i < 4000 && he.HttpBusy(); i++) {
                he.UpdateHttp();
                usleep(1000);
                if (i == 3) {
                    // 3本目(取り消し用)は待ち行列にいるうちに取り消す
                    he.Run("pico.http_cancel(dl_cancel)", "http_dl_cancel");
                }
            }
            ok = he.Run(R"LUA(
                check(dl and dl.ok and dl.st == 200 and dl.body == nil, "HTTP保存: 成功すると本文はnil")
                check(dl.size == 40000 and dl.saved == "/dl/big.bin", "HTTP保存: info.size/saved")
                check(dl_bad and dl_bad.ok and dl_bad.st == 500 and dl_bad.saved == "/dl/err.bin", "HTTP保存: 500でも受け取れた分は保存される")
                check(CANCEL_CALLED == nil, "HTTP保存: 取り消したコールバックは呼ばれない")
                check(pico.sd_exists("/dl/big.bin") and not pico.sd_exists("/dl/big.bin.part"), "HTTP保存: .partは残らない")
                check(not pico.sd_exists("/dl/cancel.bin"), "HTTP保存: 取り消した分は作られない")
                check(pico.http_request("GET", BASE .. "/hello", nil, nil, function() end, {save_to = "/sys/x.bin"}) ~= nil, "HTTP保存: パス指定は通る")
                pico.http_cancel()
            )LUA", "http_dl_check");
            check(ok, "HTTP保存: 結果の確認が成功する");
            check(HostSd::files["/dl/big.bin"].size() == 40000, "HTTP保存: 40000バイト全部が書かれている");

            // 保存先の権限
            LuaEngine ce(64 * 1024, np, "/app");
            lua_pushcfunction(ce.raw(), l_check);
            lua_setglobal(ce.raw(), "check");
            lua_pushfstring(ce.raw(), "http://127.0.0.1:%d", srv.port);
            lua_setglobal(ce.raw(), "BASE");
            HostSd::files["/app/app.cfg"] = "permission_network=true\n";
            ok = ce.Run(R"LUA(
                check(pico.http_request("GET", BASE .. "/hello", nil, nil, function() end, {save_to = "/other/x.bin"}) == false,
                      "HTTP保存: app_dirの外には保存できない")
                check(pico.http_request("GET", BASE .. "/hello", nil, nil, function() end, {save_to = "/app/app.cfg"}) == false,
                      "HTTP保存: app.cfgは上書きできない")
                check(type(pico.http_request("GET", BASE .. "/hello", nil, nil, function() end, {save_to = "/app/x.bin"})) == "number",
                      "HTTP保存: app_dirの中なら保存できる")
                pico.http_cancel()
            )LUA", "http_dl_perm");
            check(ok, "HTTP保存: 権限の確認が成功する");

            srv.stop = true;
            srv.th.join();
        }
        if (srv.lfd >= 0) close(srv.lfd);
        HostSd::files.clear();
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
