#pragma once

#include <stdint.h>
#include "lua.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

// Luaデバッガ(ブレークポイント・ステップ実行・スタックトレースと変数の表示)。
//
// LuaEngineが1つ持つ(/sys/debug.cfg の lua-debugger = true のときだけ。SetGlobalEnabled())。
// 止まる仕組みはLuaのフック(lua_sethook)の LUA_MASKLINE で、ブレークポイントがあるか
// ステップ実行中のときだけ行フックを入れる(行フックは1行ごとに呼ばれて重いため)。
//
// 「止まる」= フックの中で Frontend::onPause() を呼んで、それが戻るまで待つこと。OSは1本のloop()で
// 動いているので、その間は他の画面も止まる(2コア目の音は鳴り続ける)。実機の Frontend は
// LuaDebugScreen(液晶へ直接描いてタッチ・キー・シリアルを読む小さなループ)。テストは偽物を差す。
// Frontendが無い(Webビルド等)と、止まる代わりにシリアルへ場所を出して続ける。
//
// 止まるきっかけ(Reason):
//   - Breakpoint: 行のブレークポイント(ファイル名:行)。pico.set_breakpoint() / シリアルの dbg b
//   - Step:       ステップ実行(1行ずつ/関数を飛ばして/関数を抜けるまで)
//   - Pause:      シリアルの dbg pause(次に実行する行で止まる)
//   - Error:      捕まえられなかったエラー(LuaEngineのメッセージハンドラから。止まって変数を見られるが、
//                 続けてもエラーとしてそのまま進む)
//   - Api:        スクリプトの pico.breakpoint()
//
// シリアルのコマンド(USBシリアル/PCの標準入力の1行。"dbg "で始まる行はPadFunctionsからここへ回る):
//   dbg b main.lua:12   ブレークポイントを置く(ファイル名はアプリのフォルダからの相対でも、名前だけでもよい)
//   dbg d main.lua:12   外す / dbg d   全部外す / dbg l   一覧
//   dbg pause           次の行で止める
//   止まっている間だけ: dbg c(続行) / dbg s(1行) / dbg n(次の行・関数は飛ばす) / dbg o(関数を抜ける) /
//                      dbg q(スクリプトを止める) / dbg bt(スタック) / dbg locals [段]
class LuaDebugger {
    public:
        static constexpr int kMaxBreakpoints = 16;
        static constexpr int kMaxFrames = 8;
        static constexpr int kMaxVars = 16;

        enum class Command : uint8_t { None, Continue, StepInto, StepOver, StepOut, Abort };
        enum class Reason : uint8_t { Breakpoint, Step, Pause, Error, Api };

        struct Breakpoint {
            FixedString<PICO_STR_M> file;
            int line = 0;
        };

        struct Frame {
            int level = -1;                   // lua_getstackの段(locals()へ渡す)
            int line = -1;                    // -1 = Cの関数
            FixedString<PICO_STR_M> where;    // "main.lua:12"
            FixedString<PICO_STR_S> name;     // 関数名("?"=不明)
        };

        struct Var {
            FixedString<PICO_STR_S> name;
            FixedString<PICO_STR_M> value;
        };

        struct PauseInfo {
            Reason reason = Reason::Pause;
            FixedString<PICO_STR_L> message;   // Error/Apiのときのメッセージ
            Frame frames[kMaxFrames];
            int frame_count = 0;
            int top = -1;                      // 最初のLuaの段(frames[]の添字)。-1 = 無い
            FixedString<PICO_PATH_LEN> source; // 最初のLuaの段のファイル(SD上のパス。'@'を除いたもの。無ければ空)
            int line = -1;
            Var vars[kMaxVars];
            int var_count = 0;
            int vars_frame = -1;               // varsがどの段(frames[]の添字)のものか
        };

        class Frontend {
            public:
                virtual ~Frontend() {}
                // 止まった。戻り値の指示で続ける(Noneは続行と同じ)。
                // 別の段の変数を見たいときは dbg.loadVars(L, info, frames[]の添字) を呼んでよい
                virtual Command onPause(LuaDebugger& dbg, lua_State* L, PauseInfo& info) = 0;
        };

        LuaDebugger();
        ~LuaDebugger();
        LuaDebugger(const LuaDebugger&) = delete;
        LuaDebugger& operator=(const LuaDebugger&) = delete;

        // ---- 全体の設定(DevToolsFunctionsが決める) ----
        static void SetGlobalEnabled(bool on);
        static bool GlobalEnabled();
        static void SetFrontend(Frontend* f);
        static Frontend* GetFrontend();
        // いま生きている(=LuaEngineが持っている)デバッガ。シリアルのコマンドの宛先
        static LuaDebugger* Active();

        // Luaのエラーを残す係(CrashDumpFunctions::SaveLuaError)。nullptrなら何もしない
        using ErrorReporter = bool (*)(const char* app, const char* message, const char* trace);
        static void SetErrorReporter(ErrorReporter r);
        static void ReportError(const char* app, const char* message, const char* trace);
        // Luaの実行に入る/出るのを知らせる係(CrashDumpFunctions::SetLua)
        using ActivityHook = void (*)(bool in_lua, const char* app);
        static void SetActivityHook(ActivityHook h);
        static void NotifyActivity(bool in_lua, const char* app);

        // 止めていた間に液晶へ直接描いたので、画面全体を描き直してほしい(LuaSceneが次のフレームで見る)
        static void RequestRedraw();
        static bool TakeRedrawRequest();

        // シリアルの1行。"dbg"で始まらなければfalse(他へ回す)
        static bool FeedSerialLine(const char* line);

        // ---- ブレークポイント ----
        bool addBreakpoint(const char* file, int line);
        bool removeBreakpoint(const char* file, int line);
        void clearBreakpoints();
        int breakpointCount() const { return bp_count_; }
        const Breakpoint& breakpoint(int i) const { return bps_[i]; }
        void requestPause() { pause_requested_ = true; }

        // 行フックが要るか(ブレークポイントがある・ステップ実行中・止める予約がある)
        bool wantsLineHook() const;

        // LuaEngineのフックから(LUA_HOOKLINE)。止まらなければ Command::None
        Command onLine(lua_State* L, lua_Debug* ar);
        // その場で止まる。level_startはlua_getstackの段(フックの中なら0、Cの関数の中なら1)
        Command pause(lua_State* L, Reason reason, int level_start, const char* message);

        // 溜まっているシリアルのコマンドを処理する。paused=trueなら続行系のコマンドを返す
        // (止まっていないときは続行系を無視する)。hook_changedはブレークポイント等が変わったらtrue
        Command pollSerial(lua_State* L, bool paused, bool* hook_changed);

        // 止まっている間の情報を作る/段を変えて変数を読み直す
        void fillPauseInfo(lua_State* L, Reason reason, int level_start, const char* message);
        void loadVars(lua_State* L, PauseInfo& info, int frame_index);
        const PauseInfo& info() const { return info_; }

        // ---- 調べる道具(静的。どのlua_Stateにも使える) ----
        static int StackDepth(lua_State* L);
        // スタックトレースを "main.lua:12 in function 'foo'" の行で組み立てる(最大max_frames段)
        static void BuildTrace(lua_State* L, int level_start, char* out, size_t size, int max_frames);
        // 値を1行の文字列にする(メタメソッドは呼ばない=Luaのコードを動かさない)
        static void FormatValue(lua_State* L, int idx, char* out, size_t size);
        // "@/lua/apps/x/main.lua"のようなチャンク名がbpのファイル指定に当たるか
        static bool MatchFile(const char* bp_file, const char* source);
        // SD上のファイルの first 行目からcount行を読む(1始まり)。読めた行数を返す
        static int ReadSourceLines(const char* path, int first, int count,
                                   FixedString<PICO_STR_L>* out);

    private:
        enum class Step : uint8_t { None, Into, Over, Out };

        Breakpoint bps_[kMaxBreakpoints];
        int bp_count_ = 0;
        Step step_ = Step::None;
        int step_depth_ = 0;
        bool pause_requested_ = false;
        bool paused_ = false;   // 止まっている最中(入れ子で止まらないように)
        PauseInfo info_;

        void applyCommand(lua_State* L, Command c, int level_start);
        void printBreakpoints() const;
        void printTrace() const;
        void printVars() const;
};
