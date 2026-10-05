// Luaのサンドボックス・打ち切り(握り潰せないこと)・スタックトレース・デバッガのテスト。
//
//   サンドボックス  … debug/io/package/標準のrequire/dofile/loadfile/os.exit/string.dumpが無いこと、
//                     loadがバイトコードを断ること(テキストとenvは今までどおり)、printがエラーにならないこと
//   打ち切り        … pcall/xpcall/coroutine.resume/coroutine.wrapで包んで繰り返す終わらないループ、
//                     メッセージハンドラの中の終わらないループ、pico.setから入れ子で鳴るコールバックで
//                     予算を積み直そうとする抜け道が全部止まること。__gc(フック無しで動くので止められない)は使えないこと。
//                     yieldをまたぐpcallは今までどおり動くこと。打ち切りの後も次のRun()は普通に動くこと
//   スタックトレース … 捕まえられなかったエラーのトレース(関数名と行)・pico.traceback()・
//                     SDのパスのチャンク名が"main.lua:12:"の形になること・エラーを残す係(ErrorReporter)へ届くこと
//   デバッガ        … 偽物のFrontendで: ブレークポイント(pico.set_breakpoint / シリアルのdbg b)・
//                     ステップ実行(1行/次へ/抜ける)・pico.breakpoint()・エラーで止まる・停止(Abort)・
//                     ローカル変数と上位値・作った後のコルーチンにも行フックが効くこと・
//                     止まっている間のシリアルのコマンド・ソースの行の読み出し・無効のときは止まらないこと
#include "lua/LuaEngine.hpp"
#include "lua/LuaDebugger.hpp"
#include "gui/widgets/dialogs/MsgDialog.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/Keyboard_Functions.hpp"
#include "OS_Data.hpp"
#include "SdFat.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ---- モック(lua_engine_test.cppと同じ) ----
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
void KeyboardFunctions::Show(ITextInputTarget*, KeyboardFunctions::Layout, bool){}
void KeyboardFunctions::OnPanelShown(KeyboardPanel*){}
void KeyboardFunctions::OnPanelHidden(KeyboardPanel*){}
void KeyboardFunctions::OnPanelResized(KeyboardPanel*){}
void KeyboardFunctions::OnPanelChanged(KeyboardPanel*, bool){}
void KeyboardFunctions::HideAll(){}

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

static void InstallCheck(LuaEngine& e) {
    lua_pushcfunction(e.raw(), l_check);
    lua_setglobal(e.raw(), "check");
}

// 出たエラーのダイアログを数えて閉じる
static int CloseDialogs() {
    int n = 0;
    while (!WidgetFunctions::dialog_roots.empty()) {
        MsgDialog* d = static_cast<MsgDialog*>(WidgetFunctions::dialog_roots.back());
        d->causeOnClosed(true);
        WidgetFunctions::ProcessPendingDeletes();
        n++;
        if (n > 20) break;
    }
    return n;
}

static std::string LastDialogText() {
    if (WidgetFunctions::dialog_roots.empty()) return "";
    MsgDialog* d = static_cast<MsgDialog*>(WidgetFunctions::dialog_roots.back());
    return d->getMessage();
}

// ---- エラーを残す係の偽物 ----
static std::string g_rep_app, g_rep_msg, g_rep_trace;
static int g_rep_count = 0;
static bool FakeReporter(const char* app, const char* msg, const char* trace) {
    g_rep_app = app ? app : "";
    g_rep_msg = msg ? msg : "";
    g_rep_trace = trace ? trace : "";
    g_rep_count++;
    return true;
}

// ---- デバッガの偽物の画面 ----
struct Stop {
    LuaDebugger::Reason reason;
    int line;
    std::string where;
    std::string name;
    std::string message;
    std::string source;
    std::vector<std::string> vars;
    int frames;
};

class FakeFrontend : public LuaDebugger::Frontend {
    public:
        std::vector<LuaDebugger::Command> script; // 止まるたびに先頭から返す(尽きたらContinue)
        std::vector<Stop> stops;
        bool poll_serial = false;                 // trueならシリアルの列から続行の指示を読む

        LuaDebugger::Command onPause(LuaDebugger& dbg, lua_State* L, LuaDebugger::PauseInfo& info) override {
            Stop s;
            s.reason = info.reason;
            s.line = info.line;
            s.message = info.message.c_str();
            s.source = info.source.c_str();
            s.frames = info.frame_count;
            if (info.top >= 0) {
                s.where = info.frames[info.top].where.c_str();
                s.name = info.frames[info.top].name.c_str();
            }
            for (int i = 0; i < info.var_count; i++) {
                s.vars.push_back(std::string(info.vars[i].name.c_str()) + "=" + info.vars[i].value.c_str());
            }
            stops.push_back(s);
            if (poll_serial) {
                const LuaDebugger::Command c = dbg.pollSerial(L, true, nullptr);
                if (c != LuaDebugger::Command::None) return c;
            }
            if (script.empty()) return LuaDebugger::Command::Continue;
            const LuaDebugger::Command c = script.front();
            script.erase(script.begin());
            return c;
        }
};

static bool HasVar(const Stop& s, const char* nv) {
    for (const auto& v : s.vars) if (v == nv) return true;
    return false;
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    OSData::SD_usable = true;

    // =================== サンドボックス ===================
    {
        LuaEngine e(128 * 1024);
        check(e.valid(), "サンドボックス: 構築できる");
        InstallCheck(e);
        const bool ok = e.Run(R"LUA(
            check(debug == nil, "debugライブラリは無い(debug.sethookでフックを外せないように)")
            check(io == nil, "ioライブラリは無い")
            check(package == nil, "packageライブラリは無い")
            -- requireはある(アプリのフォルダの中のモジュールだけを読む自前のもの。標準のrequireではない)
            check(type(require) == "function" and require == pico.require, "requireはpico.requireと同じ(標準のpackage.pathは使わない)")
            check(not pcall(require, "../etc/passwd"), "requireで..は使えない")
            check(dofile == nil and loadfile == nil, "dofile/loadfileは無い")
            check(os.exit == nil and os.execute == nil and os.remove == nil and os.rename == nil
                  and os.getenv == nil, "osの危ない関数は無い")
            check(type(os.time) == "function" and type(os.clock) == "function" and type(os.date) == "function",
                  "os.time/clock/dateは残っている")
            check(string.dump == nil, "string.dumpは無い")
            check(type(coroutine.create) == "function" and type(table.concat) == "function"
                  and type(utf8.char) == "function" and type(math.floor) == "function", "他の標準ライブラリはある")

            local f, err = load(string.char(27) .. "Lua" .. string.rep("x", 40))
            check(f == nil and type(err) == "string", "loadはバイトコードを断る")
            local g = load("return 1 + 2")
            check(g ~= nil and g() == 3, "loadはテキストを読める")
            x_global = 7
            local h = load("return x_global", "c", "t")
            check(h() == 7, "loadのenv省略は_ENV(グローバル)")
            local k = load("return x_global", "c", "b", {x_global = 5})
            check(k ~= nil and k() == 5, "loadのenv指定は効く(modeに'b'を渡してもテキストとして読む)")
            local m = load(function() return nil end)
            check(m ~= nil, "loadは読み込み関数も受け付ける")
            print("print", 1, true, nil, {})
            check(true, "printはエラーにならない")
        )LUA", "sandbox");
        check(ok, "サンドボックス: スクリプトが最後まで動く");
        CloseDialogs();
    }

    // =================== 打ち切りは握り潰せない ===================
    {
        struct Case { const char* name; const char* src; };
        const Case cases[] = {
            {"pcallで包んで繰り返す",
             "while true do pcall(function() while true do end end) end"},
            {"xpcallで包んで繰り返す",
             "while true do xpcall(function() while true do end end, function(e) return e end) end"},
            {"メッセージハンドラの中で終わらない",
             "while true do xpcall(function() error('x') end, function(e) while true do end end) end"},
            {"coroutine.resumeで包んで繰り返す",
             "while true do local co = coroutine.create(function() while true do end end); coroutine.resume(co) end"},
            {"coroutine.wrapとpcallを重ねる",
             "while true do pcall(coroutine.wrap(function() while true do end end)) end"},
            {"pcallの中のコルーチンの中のpcall",
             "while true do pcall(function() local co = coroutine.create(function() "
             "while true do pcall(function() while true do end end) end end); "
             "while true do coroutine.resume(co) end end) end"},
            {"打ち切りのエラーを文字列で捕まえて投げ直さない",
             "local n = 0 while true do local ok, e = pcall(function() while true do end end); n = n + 1 end"},
        };
        for (const Case& c : cases) {
            LuaEngine e(128 * 1024);
            InstallCheck(e);
            const bool ok = e.Run(c.src, "abort_case");
            char label[160];
            snprintf(label, sizeof(label), "打ち切り: %s → 止まる(Run()がfalse)", c.name);
            check(!ok, label);
            const int dialogs = CloseDialogs();
            snprintf(label, sizeof(label), "打ち切り: %s → ダイアログは1枚だけ", c.name);
            check(dialogs == 1, label);

            // 打ち切りの後も同じエンジンで普通に動く(打ち切りは次の一番外の呼び出しで解ける)
            const bool ok2 = e.Run(R"LUA(
                local ok, err = pcall(error, "ふつうのエラー")
                check(ok == false and err == "ふつうのエラー", "打ち切りの後: pcallは普通のエラーを今までどおり捕まえる")
            )LUA", "after_abort");
            check(ok2, "打ち切りの後: 次のRun()は普通に動く");
            CloseDialogs();
        }

        // pico.setから入れ子で鳴るコールバック(Dispatch)で予算を積み直して上限を逃れる抜け道
        {
            LuaEngine e(128 * 1024);
            const bool ok = e.Run(R"LUA(
                local s = pico.create("NumberSlider")
                pico.set(s, "max_value", 10)
                pico.on(s, "value_changed", function() end)
                local v = 0
                while true do v = (v + 1) % 10; pico.set(s, "value", v) end
            )LUA", "nested_dispatch");
            check(!ok, "打ち切り: 入れ子のコールバックで予算を積み直して逃れられない");
            check(CloseDialogs() == 1, "打ち切り: 入れ子のコールバックがあってもダイアログは1枚");
            WidgetFunctions::ClearSceneWidgets();
        }

        // 入れ子のコールバックの中で終わらない → 外側も含めて止まる
        {
            LuaEngine e(128 * 1024);
            InstallCheck(e);
            const bool ok = e.Run(R"LUA(
                local s = pico.create("NumberSlider")
                pico.set(s, "max_value", 10)
                pico.on(s, "value_changed", function() while true do end end)
                pcall(pico.set, s, "value", 3)
                check(false, "入れ子の打ち切りの後の行は動かない")
            )LUA", "nested_dispatch2");
            check(!ok, "打ち切り: コールバックの中の終わらないループは外側のpcallでも止められない");
            check(CloseDialogs() == 1, "打ち切り: 入れ子の打ち切りでもダイアログは1枚");
            WidgetFunctions::ClearSceneWidgets();
        }

        // yieldをまたぐpcallは今までどおり動く(包んだpcallが継続関数に対応している)
        {
            LuaEngine e(128 * 1024);
            InstallCheck(e);
            const bool ok = e.Run(R"LUA(
                local co = coroutine.wrap(function()
                    local ok, v = pcall(function() return coroutine.yield(1) + 1 end)
                    return ok and v
                end)
                check(co() == 1, "yieldをまたぐpcall: 1回目のyield")
                check(co(41) == 42, "yieldをまたぐpcall: 再開して値が戻る")
                local ok2, err = pcall(function() error({code = 1}) end)
                check(ok2 == false and type(err) == "table" and err.code == 1, "pcallはテーブルのエラーもそのまま返す")
                check(select("#", pcall(function() return 1, 2, 3 end)) == 4, "pcallの戻り値の数は変わらない")
            )LUA", "yield_pcall");
            check(ok, "yieldをまたぐpcall: スクリプトが最後まで動く");
            CloseDialogs();
        }

        // __gc(GCのメタメソッドはフック無しで動くので止められない)は使えない
        {
            LuaEngine* e = new LuaEngine(128 * 1024);
            InstallCheck(*e);
            const bool ok = e->Run(R"LUA(
                local ok, err = pcall(setmetatable, {}, {__gc = function() while true do end end})
                check(ok == false and tostring(err):find("__gc", 1, true) ~= nil, "__gcを持つメタテーブルはsetmetatableが断る")
                local mt = {}
                local t = setmetatable({}, mt)
                mt.__gc = function() while true do end end  -- 後から足しても効かない(Lua 5.4の仕様)
                check(getmetatable(t) == mt, "__gcの無いメタテーブルは今までどおり付けられる")
                check(setmetatable({}, nil) ~= nil, "setmetatable(t, nil)も使える")
                t = nil
                collectgarbage("collect")
                check(true, "後から__gcを足したオブジェクトを回収しても固まらない")
            )LUA", "gc_loop");
            check(ok, "サンドボックス: __gcのスクリプトが最後まで動く");
            delete e;
            check(true, "サンドボックス: 閉じても固まらない");
            CloseDialogs();
        }
    }

    // =================== スタックトレース ===================
    {
        LuaDebugger::SetErrorReporter(&FakeReporter);
        LuaEngine e(128 * 1024, LuaPermissions{}, "/lua/apps/demo");
        const bool ok = e.Run(
            "local function inner(t)\n"
            "  return t.x.y\n"
            "end\n"
            "local function outer()\n"
            "  local v = inner({})\n"
            "  return v\n"
            "end\n"
            "outer()\n", "/lua/apps/demo/main.lua");
        check(!ok, "トレース: 実行時エラーでRun()がfalse");
        const std::string trace = e.lastTrace();
        printf("       トレース:\n%s\n", trace.c_str());
        check(trace.find("main.lua:2 inner") != std::string::npos, "トレース: エラーの行と関数名(main.lua:2 inner)");
        check(trace.find("main.lua:5 outer") != std::string::npos, "トレース: 呼び出し元(main.lua:5 outer)");
        check(trace.find("main.lua:8") != std::string::npos, "トレース: 一番外(main.lua:8)");
        const std::string shown = LastDialogText();
        check(shown.find("main.lua:2:") != std::string::npos && shown.find("[string") == std::string::npos,
              "トレース: SDのパスのチャンク名は\"main.lua:2:\"の形(\"[string ...\"ではない)");
        check(shown.find("inner") != std::string::npos, "トレース: ダイアログにもトレースの先頭が出る");
        check(g_rep_count == 1 && g_rep_app == "/lua/apps/demo" && g_rep_trace == trace &&
              g_rep_msg.find("attempt to index") != std::string::npos,
              "トレース: エラーを残す係(クラッシュダンプ)へアプリ・メッセージ・トレースが届く");
        CloseDialogs();

        InstallCheck(e);
        const bool ok2 = e.Run(R"LUA(
            local function f() return pico.traceback("ここ") end
            local t = f()
            check(t:find("ここ", 1, true) == 1 and t:find(" f", 1, true) ~= nil, "pico.traceback: メッセージと関数名")
            local ok, err = pcall(error, {1})
            check(ok == false, "テーブルのエラーもpcallで捕まえられる")
        )LUA", "tb");
        check(ok2, "pico.traceback: スクリプトが動く");

        // エラーの値がテーブルでもダイアログが出る
        const bool ok3 = e.Run("error({})", "tblerr");
        check(!ok3 && LastDialogText().find("table") != std::string::npos, "トレース: テーブルのエラーは種類を出す");
        CloseDialogs();
        LuaDebugger::SetErrorReporter(nullptr);
    }

    // =================== デバッガ ===================
    {
        // 無効のときは止まらない
        {
            LuaDebugger::SetGlobalEnabled(false);
            FakeFrontend fe;
            LuaDebugger::SetFrontend(&fe);
            LuaEngine e(128 * 1024);
            InstallCheck(e);
            check(e.debugger() == nullptr, "デバッガ: 無効ならデバッガを持たない");
            const bool ok = e.Run(R"LUA(
                check(pico.debugger_enabled() == false, "デバッガ: 無効ならdebugger_enabled()はfalse")
                check(pico.breakpoint() == false, "デバッガ: 無効ならbreakpoint()は止まらずfalse")
                check(pico.set_breakpoint("x.lua", 1) == false, "デバッガ: 無効ならset_breakpoint()はfalse")
            )LUA", "disabled");
            check(ok && fe.stops.empty(), "デバッガ: 無効なら一度も止まらない");
            LuaDebugger::SetFrontend(nullptr);
        }

        LuaDebugger::SetGlobalEnabled(true);

        // ブレークポイント・ローカル変数・上位値・ステップ実行
        {
            FakeFrontend fe;
            LuaDebugger::SetFrontend(&fe);
            LuaEngine e(128 * 1024);
            InstallCheck(e);
            check(e.debugger() != nullptr, "デバッガ: 有効ならデバッガを持つ");
            check(LuaDebugger::Active() == e.debugger(), "デバッガ: Active()が今のエンジンのもの");

            // 1: local count = 10
            // 2: local function add(a, b)
            // 3:   local s = a + b
            // 4:   return s + count
            // 5: end
            // 6: function run()
            // 7:   local x = add(1, 2)
            // 8:   local y = x * 2
            // 9:   return y
            //10: end
            const char* src =
                "local count = 10\n"
                "local function add(a, b)\n"
                "  local s = a + b\n"
                "  return s + count\n"
                "end\n"
                "function run()\n"
                "  local x = add(1, 2)\n"
                "  local y = x * 2\n"
                "  return y\n"
                "end\n";
            check(e.Run(src, "/lua/dbg.lua"), "デバッガ: スクリプトを読む");

            // ブレークポイントで止まって、ローカル変数と上位値が見える
            check(e.debugger()->addBreakpoint("dbg.lua", 3), "デバッガ: ブレークポイントを置ける");
            fe.script = {LuaDebugger::Command::Continue};
            check(e.Run("result = run()", "call1"), "デバッガ: 止まって続けると最後まで動く");
            check(fe.stops.size() == 1, "デバッガ: ブレークポイントで1回止まる");
            if (fe.stops.size() == 1) {
                const Stop& s = fe.stops[0];
                check(s.reason == LuaDebugger::Reason::Breakpoint && s.line == 3, "デバッガ: 理由と行(3行目)");
                check(s.where == "dbg.lua:3" && s.name == "add", "デバッガ: 場所(dbg.lua:3)と関数名(add)");
                check(s.source == "/lua/dbg.lua", "デバッガ: ソースのパス(SDから読む用)");
                check(HasVar(s, "a=1") && HasVar(s, "b=2"), "デバッガ: 引数が見える");
                check(HasVar(s, "^count=10"), "デバッガ: 上位値(^count)が見える");
                check(s.frames >= 3, "デバッガ: スタックの段(add/run/メイン)");
            }

            // 1行ずつ(StepInto)→次へ(StepOver)→抜ける(StepOut)
            fe.stops.clear();
            e.debugger()->clearBreakpoints();
            e.debugger()->addBreakpoint("dbg.lua", 7);
            fe.script = {
                LuaDebugger::Command::StepInto,  // 7で止まる → 1行(addの中の3へ)
                LuaDebugger::Command::StepOut,   // 3 → 抜ける(runの7か8へ)
                LuaDebugger::Command::StepOver,  // → 次へ
                LuaDebugger::Command::Continue,
            };
            check(e.Run("result = run()", "call2"), "デバッガ: ステップ実行で最後まで動く");
            for (const auto& s : fe.stops) printf("       止まった: %s %s 理由%d\n", s.where.c_str(), s.name.c_str(), (int)s.reason);
            check(fe.stops.size() == 4, "デバッガ: ステップ実行で4回止まる");
            if (fe.stops.size() == 4) {
                check(fe.stops[0].line == 7 && fe.stops[0].reason == LuaDebugger::Reason::Breakpoint, "ステップ: 7行目のブレークポイント");
                check(fe.stops[1].line == 3 && fe.stops[1].name == "add" && fe.stops[1].reason == LuaDebugger::Reason::Step,
                      "ステップ: 1行で関数の中(3行目、add)へ入る");
                check(fe.stops[2].name == "run" && (fe.stops[2].line == 7 || fe.stops[2].line == 8),
                      "ステップ: 抜けるでrunへ戻る");
                check(fe.stops[3].name == "run" && fe.stops[3].line == fe.stops[2].line + 1,
                      "ステップ: 次へで同じ関数の次の行");
            }
            lua_getglobal(e.raw(), "result");
            check(lua_tointeger(e.raw(), -1) == 26, "デバッガ: 止まりながらでも結果は正しい((1+2+10)*2)");
            lua_pop(e.raw(), 1);

            // 次へ(StepOver)は関数の中に入らない
            fe.stops.clear();
            fe.script = {LuaDebugger::Command::StepOver, LuaDebugger::Command::Continue};
            check(e.Run("result = run()", "call3"), "デバッガ: 次へで最後まで動く");
            check(fe.stops.size() == 2 && fe.stops[1].name == "run" && fe.stops[1].line == 8,
                  "ステップ: 次へはaddの中に入らず8行目で止まる");
            e.debugger()->clearBreakpoints();

            // pico.breakpoint()とpico.set_breakpoint()
            fe.stops.clear();
            fe.script = {};
            const bool ok = e.Run(R"LUA(
                local v = 5
                check(pico.debugger_enabled(), "pico.debugger_enabled(): 有効ならtrue")
                check(pico.breakpoint("見て") == true, "pico.breakpoint(): 止まってtrue")
                check(pico.set_breakpoint("dbg.lua", 9), "pico.set_breakpoint(): 置ける")
                local r = run()
                pico.clear_breakpoint()
                r = run()
            )LUA", "/lua/api.lua");
            check(ok, "デバッガ: pico.breakpoint()のスクリプトが動く");
            check(fe.stops.size() == 2, "デバッガ: pico.breakpoint()で1回+set_breakpointで1回止まる(clear後は止まらない)");
            if (fe.stops.size() == 2) {
                check(fe.stops[0].reason == LuaDebugger::Reason::Api && fe.stops[0].message == "見て" &&
                      fe.stops[0].where == "api.lua:4" && HasVar(fe.stops[0], "v=5"),
                      "pico.breakpoint(): 理由・メッセージ・呼んだ場所・変数");
                check(fe.stops[1].line == 9 && fe.stops[1].reason == LuaDebugger::Reason::Breakpoint,
                      "pico.set_breakpoint(): 9行目で止まる");
            }

            // エラーで止まる(変数を見られる。続けてもエラーとして進む)
            fe.stops.clear();
            const bool okerr = e.Run("local where = 'ここ'\nlocal t = nil\nreturn t.x\n", "/lua/err.lua");
            check(!okerr, "デバッガ: エラーはそのままエラーになる");
            check(fe.stops.size() == 1 && fe.stops[0].reason == LuaDebugger::Reason::Error &&
                  fe.stops[0].line == 3 && HasVar(fe.stops[0], "where=\"ここ\"") &&
                  fe.stops[0].message.find("err.lua:3:") != std::string::npos,
                  "デバッガ: 捕まえられなかったエラーの場所で止まって変数が見える");
            CloseDialogs();

            // pcallで捕まえるエラーでは止まらない
            fe.stops.clear();
            check(e.Run("pcall(error, 'x')", "pcall_err") && fe.stops.empty(), "デバッガ: pcallで捕まえるエラーでは止まらない");

            // 停止(Abort): スクリプトを止める。pcallでも握り潰せない
            fe.stops.clear();
            e.debugger()->addBreakpoint("abort.lua", 2);
            fe.script = {LuaDebugger::Command::Abort};
            const bool okab = e.Run("pcall(function()\nlocal z = 1\nend)\ncheck(false, '停止の後は動かない')\n", "/lua/abort.lua");
            check(!okab && fe.stops.size() == 1, "デバッガ: 停止でスクリプトが止まる(pcallの中でも)");
            check(LastDialogText().find("デバッガで停止") != std::string::npos, "デバッガ: 停止の理由がダイアログに出る");
            CloseDialogs();
            e.debugger()->clearBreakpoints();

            // 作った後のコルーチンにも行フックが効く
            fe.stops.clear();
            check(e.Run("co = coroutine.create(function()\nlocal q = 1\nreturn q\nend)", "/lua/co.lua"), "コルーチン: 作る");
            e.debugger()->addBreakpoint("co.lua", 3);
            check(e.Run("coroutine.resume(co)", "resume"), "コルーチン: 再開する");
            check(fe.stops.size() == 1 && fe.stops[0].line == 3 && HasVar(fe.stops[0], "q=1"),
                  "コルーチン: ブレークポイントを置く前に作ったコルーチンの中でも止まる");
            e.debugger()->clearBreakpoints();

            // シリアルのコマンド: dbg b で置き、止まっている間に dbg c で続ける
            fe.stops.clear();
            fe.poll_serial = true;
            check(LuaDebugger::FeedSerialLine("dbg b dbg.lua:8"), "シリアル: dbg の行を受け取る");
            check(!LuaDebugger::FeedSerialLine("pad 0010") && !LuaDebugger::FeedSerialLine("dbgx"), "シリアル: dbg 以外の行は受け取らない");
            e.UpdateDebugger();
            check(e.debugger()->breakpointCount() == 1, "シリアル: dbg b でブレークポイントが置かれる");
            LuaDebugger::FeedSerialLine("dbg l");
            LuaDebugger::FeedSerialLine("dbg c");
            fe.script = {LuaDebugger::Command::Abort}; // シリアルのcが先に効くので使われない
            check(e.Run("result = run()", "serial"), "シリアル: 止まっている間の dbg c で続く");
            check(fe.stops.size() == 1 && fe.stops[0].line == 8, "シリアル: dbg b の行(8行目)で止まった");
            fe.script.clear();
            fe.poll_serial = false;
            LuaDebugger::FeedSerialLine("dbg d dbg.lua:8");
            e.UpdateDebugger();
            check(e.debugger()->breakpointCount() == 0, "シリアル: dbg d で外れる");
            LuaDebugger::FeedSerialLine("dbg pause");
            e.UpdateDebugger();
            fe.stops.clear();
            check(e.Run("local a = 1\nlocal b = 2\n", "/lua/p.lua") && fe.stops.size() == 1 &&
                  fe.stops[0].reason == LuaDebugger::Reason::Pause && fe.stops[0].line == 1,
                  "シリアル: dbg pause で次の行(1行目)に止まる");

            LuaDebugger::SetFrontend(nullptr);
        }
        check(LuaDebugger::Active() == nullptr, "デバッガ: エンジンを閉じたらActive()はnullptr");

        // 画面が無ければ止まらずに続ける
        {
            LuaEngine e(128 * 1024);
            e.debugger()->addBreakpoint("nofe.lua", 1);
            check(e.Run("local a = 1\n", "/lua/nofe.lua"), "デバッガ: Frontendが無ければ止まらずに続ける");
        }

        // ソースの行の読み出し(SDのファイル)
        {
            HostSd::files["/lua/src.lua"] = "line1\nline2\n\tline3\r\nline4";
            FixedString<PICO_STR_L> lines[5];
            const int n = LuaDebugger::ReadSourceLines("/lua/src.lua", 2, 5, lines);
            check(n == 3 && strcmp(lines[0].c_str(), "line2") == 0 && strcmp(lines[1].c_str(), "  line3") == 0 &&
                  strcmp(lines[2].c_str(), "line4") == 0, "ソース: 2行目から(タブは空白、CRは捨てる、最後の改行無しの行も)");
            check(LuaDebugger::ReadSourceLines("/lua/none.lua", 1, 3, lines) == 0, "ソース: 無いファイルは0行");
        }

        // ファイル名の当たり方
        check(LuaDebugger::MatchFile("main.lua", "@/lua/apps/x/main.lua"), "ファイル名: 名前だけで当たる");
        check(LuaDebugger::MatchFile("x/main.lua", "@/lua/apps/x/main.lua"), "ファイル名: フォルダ付きでも当たる");
        check(!LuaDebugger::MatchFile("ain.lua", "@/lua/apps/x/main.lua"), "ファイル名: 名前の途中では当たらない");
        check(LuaDebugger::MatchFile("/lua/apps/x/main.lua", "@/lua/apps/x/main.lua"), "ファイル名: 完全なパスでも当たる");

        LuaDebugger::SetGlobalEnabled(false);
    }

    WidgetFunctions::ClearSceneWidgets();
    printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
