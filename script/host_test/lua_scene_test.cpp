// LuaScene(SD上のLuaスクリプトを1本読んで実行する画面)を、実際のシーン遷移
// (Scene_Functions.cpp)と組み合わせて動かす結合テスト。
//
// LuaEngine自体の動作はlua_engine_test.cppで検証済みなので、ここではLuaScene固有の
// 配線(SDからの読み込み、シーンのライフサイクルに合わせたLuaEngineの生成/破棄、
// pico.pop()で実際にランチャへ戻れること)に絞って確認する。
//
// pico.push_scene/change_scene/launch_app(シーン制御。2026-09-21追加)も、
// SceneFunctions::Push/Change/AppFunctions::LaunchByName経由で実際のシーン遷移まで
// 起こすため、ここで検証する(push_scene/change_scene: 別のLuaスクリプトへ実際に
// 遷移すること・スタック深さの増減・要求がフレーム境界まで保留されること。
// launch_app: 登録簿のC++製アプリへ実際に遷移すること・未登録名はfalseを返すこと)。
#include "gui/scenes/LuaScene.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/App_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/Keyboard_Functions.hpp"
#include "functions/Mem_Functions.hpp"
#include "gui/widgets/WidgetRegistry.hpp"
#include "gui/widgets/WidgetProperty.hpp"
#include "gui/widgets/dialogs/MsgDialog.hpp"
#include "OS_Data.hpp"
#include <cstdio>
#include <vector>
#include <string>

// ---- モック ----
void PICO_GFX::MarkDirty(const Rect&){}
void PICO_GFX::Setup(){}
void PICO_GFX::FlushDirty(){}
void PICO_GFX::DrawDialogBackground(){}
void KeyboardFunctions::HideAll(){}
void KeyboardFunctions::Setup(){}
void KeyboardFunctions::RegisterInputTarget(ITextInputTarget*){}
void KeyboardFunctions::UnregisterInputTarget(ITextInputTarget*){}

static std::vector<std::string> logs;
void LogFunctions::Log(LogType type, const char* fmt, ...){
    char buf[512];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    logs.push_back(std::string(GetPrefix(type)) + buf);
}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}

static bool logsContain(const char* needle){
    for(const auto& l : logs) if(l.find(needle) != std::string::npos) return true;
    return false;
}

// ---- ランチャ役の最小限のシーン(Pushの戻り先) ----
class FakeLauncherScene : public Scene {
    public:
        int enter_count = 0;
        const char* getName() const override { return "FakeLauncher"; }
        void onEnter() override { enter_count++; }
};

// ---- pico.launch_app()の飛び先役(C++製アプリを模した最小限のシーン) ----
static int other_app_enter_count = 0;
class OtherAppScene : public Scene {
    public:
        const char* getName() const override { return "OtherApp"; }
        void onEnter() override { other_app_enter_count++; }
};

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

static const char* kTestScript = R"LUA(
local x, y, w, h = pico.content_rect()
check_w = w
check_h = h

count = 0
count_label = pico.create("Label")
pico.set(count_label, "text", "0")

inc_button = pico.create("Button")
pico.set(inc_button, "text", "+1")
pico.on(inc_button, "press_start", function()
    count = count + 1
    pico.set(count_label, "text", tostring(count))
end)

back_button = pico.create("Button")
pico.set(back_button, "text", "back")
pico.on(back_button, "press_start", function()
    pico.pop()
end)

pico.log("lua_scene_test script ran")
)LUA";

int main(){
    // check_w/check_hを読むためのグローバル参照はLuaEngine::raw()経由で行うので、
    // このテストでは"check"というLua関数は使わない(LuaScene::onEnter()が
    // engineの生成とRun()を1つの呼び出しの中で済ませてしまうため、外から
    // check()を注入する隙が無い。LuaEngine単体のテストはlua_engine_test.cpp側の担当)

    FakeLauncherScene* launcher = new FakeLauncherScene();
    SceneFunctions::Setup(launcher);
    check(SceneFunctions::Current() == launcher, "前提: ランチャ役のシーンが最初に入る");

    // ---- スクリプトをSDに見立てて登録し、LuaSceneへPush ----
    HostSd::files["/lua/test.lua"] = kTestScript;
    SceneFunctions::Push(new LuaScene("/lua/test.lua"));
    SceneFunctions::Update(); // 保留中のPushを適用する

    LuaScene* lua_scene = static_cast<LuaScene*>(SceneFunctions::Current());
    check(lua_scene != nullptr && lua_scene != (Scene*)launcher, "Push: LuaSceneへ遷移する");
    check(lua_scene->getEngine() != nullptr && lua_scene->getEngine()->valid(),
          "onEnter(): LuaEngineが生成され有効になっている");
    check(logsContain("lua_scene_test script ran"), "onEnter(): スクリプトの末尾まで実行される");

    lua_State* L = lua_scene->getEngine()->raw();

    lua_getglobal(L, "check_w");
    lua_getglobal(L, "check_h");
    check(lua_tointeger(L, -2) > 0 && lua_tointeger(L, -1) > 0,
          "pico.content_rect(): 有効な幅・高さがスクリプトへ渡る");
    lua_pop(L, 2);

    // ---- ボタンを実際にタップして、Luaコールバック経由でラベルが更新されること ----
    lua_getglobal(L, "inc_button");
    const WidgetId inc_id = (WidgetId)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getglobal(L, "count_label");
    const WidgetId label_id = (WidgetId)lua_tointeger(L, -1);
    lua_pop(L, 1);

    Widget* inc_button = WidgetRegistry::Resolve(inc_id);
    Widget* count_label = WidgetRegistry::Resolve(label_id);
    check(inc_button != nullptr && count_label != nullptr,
          "スクリプトが生成したウィジェットがWidgetRegistryから引ける");

    if(inc_button) inc_button->causeOnPressStart();
    if(inc_button) inc_button->causeOnPressStart();

    lua_getglobal(L, "count");
    check(lua_tointeger(L, -1) == 2, "タップ2回でLua側のcountが2になる");
    lua_pop(L, 1);

    WidgetProperty::Value v;
    check(WidgetProperty::Get(count_label, WidgetProperty::Id::Text, v) && v.s == "2",
          "タップに応じてpico.set()経由でラベルの表示も更新される");

    // setup()/loop()を定義していないスクリプトでも、onUpdate()は安全にno-opであること
    lua_scene->onUpdate();
    check(true, "onUpdate(): setup/loop未定義でもクラッシュしない(no-op)");

    // ---- 戻るボタン: pico.pop()で実際にランチャへ戻れること ----
    lua_getglobal(L, "back_button");
    const WidgetId back_id = (WidgetId)lua_tointeger(L, -1);
    lua_pop(L, 1);
    Widget* back_button = WidgetRegistry::Resolve(back_id);
    check(back_button != nullptr, "戻るボタンも生成されている");

    if(back_button) back_button->causeOnPressStart();
    check(SceneFunctions::pending_type == SceneFunctions::RequestType::Pop,
          "pico.pop(): Pop要求が登録される(即時には遷移しない)");

    SceneFunctions::Update(); // 保留中のPopを適用する
    check(SceneFunctions::Current() == launcher, "Pop適用後: ランチャへ戻っている");
    // Push/Popの仕組み上、戻ってきたシーンもonEnter()から作り直される
    // (Scene.hppのコメント通り。シーンオブジェクト自体は最初のものが使い回される)
    check(launcher->enter_count == 2, "ランチャへ戻るとonEnter()が再度呼ばれる(1回目のSetup分+復帰分)");

    // ---- 異常系: ファイルが無い ----
    {
        const size_t dialogs_before = WidgetFunctions::dialog_roots.size();
        SceneFunctions::Push(new LuaScene("/lua/does_not_exist.lua"));
        SceneFunctions::Update();
        check(WidgetFunctions::dialog_roots.size() == dialogs_before + 1,
              "存在しないスクリプトはErrorFunctions経由でダイアログが出る");
        if(WidgetFunctions::dialog_roots.size() > dialogs_before){
            static_cast<MsgDialog*>(WidgetFunctions::dialog_roots.back())->causeOnClosed(true);
        }
        WidgetFunctions::ProcessPendingDeletes();
        SceneFunctions::Pop();
        SceneFunctions::Update();
    }

    // ---- 異常系: 上限を超える大きさのスクリプトは打ち切って警告する ----
    {
        std::string huge = "-- padding\n";
        // kMaxScriptBytes(16KiB)を確実に超える大きさにする。有効なLuaとして
        // 実行し切れる保証は要らない(警告が出ること自体を見る)
        while(huge.size() < 20 * 1024) huge += "-- 0123456789012345678901234567890123456789\n";
        HostSd::files["/lua/huge.lua"] = huge;

        logs.clear();
        SceneFunctions::Push(new LuaScene("/lua/huge.lua"));
        SceneFunctions::Update();
        check(logsContain("上限"), "大きすぎるスクリプトは警告ログを出して打ち切る");
        SceneFunctions::Pop();
        SceneFunctions::Update();
    }

    check(SceneFunctions::Current() == launcher, "最終的にランチャへ戻っている");

    // ---- Arduino風 setup()/loop(): onEnter()後にsetup()が1回、onUpdate()毎にloop()が呼ばれる ----
    {
        static const char* kSetupLoopScript = R"LUA(
            setup_calls = 0
            loop_calls = 0
            function setup()
                setup_calls = setup_calls + 1
            end
            function loop(dt)
                loop_calls = loop_calls + 1
            end
        )LUA";
        HostSd::files["/lua/setup_loop.lua"] = kSetupLoopScript;
        SceneFunctions::Push(new LuaScene("/lua/setup_loop.lua"));
        SceneFunctions::Update();

        LuaScene* sl_scene = static_cast<LuaScene*>(SceneFunctions::Current());
        check(sl_scene != nullptr && sl_scene->getEngine() != nullptr,
              "setup/loop: LuaSceneへ遷移しengineが生成される");
        lua_State* sl_L = sl_scene->getEngine()->raw();

        lua_getglobal(sl_L, "setup_calls");
        check(lua_tointeger(sl_L, -1) == 1, "onEnter(): setup()が1回呼ばれる");
        lua_pop(sl_L, 1);

        // SceneFunctions::Update()は保留中の遷移適用に続けて、その場でcurrent->onUpdate()も
        // 呼ぶ(Push直後のUpdate()内で既にloop()が1回呼ばれている点に注意)。
        // 実機のmain.cpp本流と同じ経路(SceneFunctions::Update()を毎フレーム呼ぶ)で
        // 追加の3フレーム分を進める
        lua_getglobal(sl_L, "loop_calls");
        check(lua_tointeger(sl_L, -1) == 1,
              "Push直後のUpdate(): 遷移適用と同じ呼び出しでloop()も1回実行される");
        lua_pop(sl_L, 1);

        SceneFunctions::Update();
        SceneFunctions::Update();
        SceneFunctions::Update();
        lua_getglobal(sl_L, "loop_calls");
        check(lua_tointeger(sl_L, -1) == 4, "Update(): 毎フレーム呼ぶたびにloop()が実行される");
        lua_pop(sl_L, 1);

        SceneFunctions::Pop();
        SceneFunctions::Update();
        check(SceneFunctions::Current() == launcher, "setup/loopテスト後: ランチャへ戻っている");
    }

    // ---- シーン制御: pico.push_scene() / pico.change_scene() ----
    {
        static const char* kPushSourceScript = R"LUA(
            push_trigger = pico.create("Button")
            pico.on(push_trigger, "press_start", function()
                pico.push_scene("/lua/sub_a.lua")
            end)
        )LUA";
        static const char* kSubAScript = R"LUA(
            sub_a_marker = "sub_a_ran"
            change_trigger = pico.create("Button")
            pico.on(change_trigger, "press_start", function()
                pico.change_scene("/lua/sub_b.lua")
            end)
        )LUA";
        static const char* kSubBScript = R"LUA(
            sub_b_marker = "sub_b_ran"
        )LUA";
        HostSd::files["/lua/push_source.lua"] = kPushSourceScript;
        HostSd::files["/lua/sub_a.lua"] = kSubAScript;
        HostSd::files["/lua/sub_b.lua"] = kSubBScript;

        SceneFunctions::Push(new LuaScene("/lua/push_source.lua"));
        SceneFunctions::Update();
        LuaScene* push_source_scene = static_cast<LuaScene*>(SceneFunctions::Current());
        check(push_source_scene != nullptr && SceneFunctions::Depth() == 1,
              "シーン制御準備: push_source.luaへ遷移");

        lua_State* ps_L = push_source_scene->getEngine()->raw();
        lua_getglobal(ps_L, "push_trigger");
        const WidgetId push_trigger_id = (WidgetId)lua_tointeger(ps_L, -1);
        lua_pop(ps_L, 1);
        Widget* push_trigger = WidgetRegistry::Resolve(push_trigger_id);
        check(push_trigger != nullptr, "push_scene準備: トリガーボタンが生成されている");

        if(push_trigger) push_trigger->causeOnPressStart();
        check(SceneFunctions::pending_type == SceneFunctions::RequestType::Push,
              "pico.push_scene(): Push要求が登録される(即時には遷移しない)");

        SceneFunctions::Update();
        LuaScene* sub_a_scene = static_cast<LuaScene*>(SceneFunctions::Current());
        check(sub_a_scene != nullptr && sub_a_scene != push_source_scene && SceneFunctions::Depth() == 2,
              "pico.push_scene(): 別のLuaスクリプトへPushで遷移する(スタックが1段伸びる)");

        lua_State* a_L = sub_a_scene->getEngine()->raw();
        lua_getglobal(a_L, "sub_a_marker");
        check(std::string(lua_tostring(a_L, -1)) == "sub_a_ran",
              "pico.push_scene(): 指定したスクリプトが実際に実行される");
        lua_pop(a_L, 1);

        lua_getglobal(a_L, "change_trigger");
        const WidgetId change_trigger_id = (WidgetId)lua_tointeger(a_L, -1);
        lua_pop(a_L, 1);
        Widget* change_trigger = WidgetRegistry::Resolve(change_trigger_id);
        check(change_trigger != nullptr, "change_scene準備: トリガーボタンが生成されている");

        if(change_trigger) change_trigger->causeOnPressStart();
        check(SceneFunctions::pending_type == SceneFunctions::RequestType::Change,
              "pico.change_scene(): Change要求が登録される(即時には遷移しない)");

        SceneFunctions::Update();
        LuaScene* sub_b_scene = static_cast<LuaScene*>(SceneFunctions::Current());
        check(sub_b_scene != nullptr && SceneFunctions::Depth() == 2,
              "pico.change_scene(): スタックを消費せずに置き換わる(depthは変わらない)");

        lua_State* b_L = sub_b_scene->getEngine()->raw();
        lua_getglobal(b_L, "sub_b_marker");
        check(std::string(lua_tostring(b_L, -1)) == "sub_b_ran",
              "pico.change_scene(): 指定したスクリプトが実際に実行される(前のsub_a側の状態は残らない)");
        lua_pop(b_L, 1);

        // 積んだ分(push_source, sub_b)だけPopしてランチャへ戻る
        SceneFunctions::Pop();
        SceneFunctions::Update();
        SceneFunctions::Pop();
        SceneFunctions::Update();
        check(SceneFunctions::Current() == launcher, "シーン制御テスト後: ランチャへ戻っている");
    }

    // ---- シーン制御: pico.launch_app() ----
    {
        check(AppFunctions::Register("Other App", IconID::AppBox, &AppFunctions::MakeScene<OtherAppScene>),
              "launch_app準備: 別アプリ(C++製)を登録簿へ登録");

        // ボタン押下で発火する形にする(トップレベルで直接呼ぶと、Pop()で戻ってきた際に
        // スクリプトが最初から実行し直されるたびpico.launch_app()も再発火してしまい、
        // 後片付けのPopと競合するため。push_scene/change_scene側も同じ理由でボタン経由にしてある)
        static const char* kLaunchAppScript = R"LUA(
            launch_trigger = pico.create("Button")
            pico.on(launch_trigger, "press_start", function()
                ok_result = pico.launch_app("Other App")
                bad_result = pico.launch_app("No Such App")
            end)
        )LUA";
        HostSd::files["/lua/launch_app.lua"] = kLaunchAppScript;

        SceneFunctions::Push(new LuaScene("/lua/launch_app.lua"));
        SceneFunctions::Update();
        LuaScene* launch_app_scene = static_cast<LuaScene*>(SceneFunctions::Current());
        check(launch_app_scene != nullptr, "launch_app準備: launch_app.luaへ遷移");

        lua_State* la_L = launch_app_scene->getEngine()->raw();
        lua_getglobal(la_L, "launch_trigger");
        const WidgetId launch_trigger_id = (WidgetId)lua_tointeger(la_L, -1);
        lua_pop(la_L, 1);
        Widget* launch_trigger = WidgetRegistry::Resolve(launch_trigger_id);
        check(launch_trigger != nullptr, "launch_app準備: トリガーボタンが生成されている");
        if(launch_trigger) launch_trigger->causeOnPressStart();

        lua_getglobal(la_L, "ok_result");
        check(lua_toboolean(la_L, -1), "pico.launch_app(): 登録済みの名前ならtrueを返す");
        lua_pop(la_L, 1);
        lua_getglobal(la_L, "bad_result");
        check(!lua_toboolean(la_L, -1), "pico.launch_app(): 未登録の名前はfalseを返す");
        lua_pop(la_L, 1);

        check(SceneFunctions::pending_type == SceneFunctions::RequestType::Push,
              "pico.launch_app(): 成功時はPush要求が登録される(即時には遷移しない)");

        const int before_enter = other_app_enter_count;
        SceneFunctions::Update();
        check(SceneFunctions::Current() != nullptr &&
                  std::string(SceneFunctions::Current()->getName()) == "OtherApp",
              "pico.launch_app(): 登録簿のC++製アプリへ実際に遷移する");
        check(other_app_enter_count == before_enter + 1,
              "pico.launch_app(): 飛び先のonEnter()が呼ばれる");

        // launcher -> launch_app.lua(LuaScene) -> OtherAppScene の2段積みなので2回Popする
        SceneFunctions::Pop();
        SceneFunctions::Update();
        SceneFunctions::Pop();
        SceneFunctions::Update();
        check(SceneFunctions::Current() == launcher, "launch_appテスト後: ランチャへ戻っている");
    }

    // ---- 後片付け ----
    WidgetFunctions::ClearSceneWidgets();
    delete launcher;

    printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
