// Lua の実行の土台: 状態の作成とメモリ予算・サンドボックス・実行時間の安全網・エラーの見せ方・pico.* の登録・
// コールバックの中継・デバッガ・タイマー・画面をまたぐ受け渡し。pico.* の関数本体は LuaEngine_<分野>.cpp
#include "lua/LuaEngine.hpp"
#include "lua/LuaEngine_Internal.hpp"
#include "lua/LuaConstants.hpp"
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
#include "gui/icons/Jpeg_Codec.hpp"
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

// ---------------- メモリ予算 ----------------

void* LuaEngine::Alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    LuaEngine* self = static_cast<LuaEngine*>(ud);
    const size_t old = ptr ? osize : 0;

    if (nsize == 0) {
        if (ptr) {
            self->used_ -= old;
            free(ptr);
        }
        return nullptr;
    }

    // 打ち切り中は増える確保を全部断る(RaiseAbort()がこれでLUA_ERRMEMを起こす)
    if (self->aborting_ && nsize > old) return nullptr;

    if (self->used_ - old + nsize > self->budget_) {
        return nullptr; // 予算超過。呼び出し元(Lua本体)はLUA_ERRMEMとして扱う
    }

    // ヒープの残りを守る(LuaEngine.hppのkHeapReserveBytes参照)。末尾の未使用の領域が
    // 「確保しても残りを割らない」だけあるうちは何も見ない(sbrk(0)を読むだけで軽い)。
    // 割りそうなときは、空きブロックから取れたのか末尾を伸ばしたのかを確保の前後で比べ、
    // 伸ばして残りを割ったときだけ返して断る。reallocだと断るときに元へ戻せないので、
    // 増やすときはmalloc+コピーにする(縮めるのはreallocのまま。末尾を伸ばさない)
    if (nsize > old) {
        const size_t before = MemFunctions::HeapTopRoom();
        if (before != MemFunctions::kRoomUnknown && before < kHeapReserveBytes + nsize) {
            void* np = malloc(nsize);
            if (!np) {
                self->heap_refusals_++;
                return nullptr;
            }
            const size_t after = MemFunctions::HeapTopRoom();
            if (after < before && after < kHeapReserveBytes) {
                free(np);
                self->heap_refusals_++;
                return nullptr; // Lua本体はごみを集めてもう一度だけ頼み直す(lmem.cのtryagain)
            }
            if (ptr) {
                memcpy(np, ptr, old);
                free(ptr);
            }
            self->used_ = self->used_ - old + nsize;
            return np;
        }
    }

    void* np = realloc(ptr, nsize);
    if (!np) return nullptr;

    self->used_ = self->used_ - old + nsize;
    return np;
}

int LuaEngine::InitTrampoline(lua_State* L) {
    LuaEngine* self = static_cast<LuaEngine*>(lua_touserdata(L, 1));
    self->openSandboxedLibs();
    self->registerApi();
    return 0;
}

// ---------------- サンドボックス ----------------
// クラスコメント「サンドボックス」参照。luaL_openlibs()は使わず、要るものだけを開いて危ないものを外す

void LuaEngine::openSandboxedLibs() {
    static const luaL_Reg kLibs[] = {
        {LUA_GNAME, luaopen_base},
        {LUA_COLIBNAME, luaopen_coroutine},
        {LUA_TABLIBNAME, luaopen_table},
        {LUA_STRLIBNAME, luaopen_string},
        {LUA_MATHLIBNAME, luaopen_math},
        {LUA_UTF8LIBNAME, luaopen_utf8},
        {LUA_OSLIBNAME, luaopen_os},
    };
    for (const luaL_Reg& lib : kLibs) {
        luaL_requiref(L, lib.name, lib.func, 1);
        lua_pop(L, 1);
    }

    // 基本ライブラリ: SDを素通しで読むdofile/loadfileは外す(pico.sd_readを使う。権限の確認が掛かる)
    lua_pushnil(L); lua_setglobal(L, "dofile");
    lua_pushnil(L); lua_setglobal(L, "loadfile");

    // osは時刻だけ残す(exit/execute/remove/rename/getenv/tmpname/setlocaleは外す)
    lua_getglobal(L, LUA_OSLIBNAME);
    static const char* const kOsRemove[] = {"exit", "execute", "remove", "rename", "getenv", "tmpname", "setlocale"};
    for (const char* name : kOsRemove) {
        lua_pushnil(L);
        lua_setfield(L, -2, name);
    }
    lua_pop(L, 1);

    // string.dump(バイトコードを作る)は外す。loadはテキストだけ受け付ける(下)
    lua_getglobal(L, LUA_STRLIBNAME);
    lua_pushnil(L);
    lua_setfield(L, -2, "dump");
    lua_pop(L, 1);

    // load: 書き換えたバイトコードはVMを壊せるので、モードを"t"(テキストだけ)に固定する
    lua_getglobal(L, "load");
    lua_pushlightuserdata(L, this);
    lua_insert(L, -2);
    lua_pushcclosure(L, l_safe_load, 2);
    lua_setglobal(L, "load");

    // setmetatable: __gc(ファイナライザ)を持つメタテーブルは断る。LuaはGCのメタメソッドを
    // フック無し(allowhook=0)で動かすので、__gcの中の終わらないループは命令数の安全網でも止められず、
    // GCが走ったところ(どこでも起こる)でOSごと固まる。__gcが効くのはsetmetatableの時点で
    // メタテーブルに__gcがあった場合だけ(Lua 5.4の仕様)なので、ここで見れば足りる
    lua_getglobal(L, "setmetatable");
    lua_pushlightuserdata(L, this);
    lua_insert(L, -2);
    lua_pushcclosure(L, l_safe_setmetatable, 2);
    lua_setglobal(L, "setmetatable");

    // print: 標準出力は実機ではどこにも出ないので、pico.logと同じくログへ
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, l_print, 1);
    lua_setglobal(L, "print");

    // エラーを捕まえる関数(pcall/xpcall/coroutine.resume/coroutine.close)は、打ち切り中なら
    // 捕まえた結果を捨てて、もう一度投げ直す(打ち切りを握り潰せないように)
    auto guard = [this](const char* table, const char* name) {
        if (table) lua_getglobal(L, table); else lua_pushglobaltable(L);
        lua_getfield(L, -1, name);             // 元の関数
        lua_pushlightuserdata(L, this);
        lua_insert(L, -2);                     // upvalue1=this, upvalue2=元の関数
        lua_pushcclosure(L, l_guarded_call, 2);
        lua_setfield(L, -2, name);
        lua_pop(L, 1);
    };
    // coroutine.wrapは自前のもの(l_wrap)に置き換える
    lua_getglobal(L, LUA_COLIBNAME);
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, l_wrap, 1);
    lua_setfield(L, -2, "wrap");
    lua_pop(L, 1);

    guard(nullptr, "pcall");
    guard(nullptr, "xpcall");
    guard(LUA_COLIBNAME, "resume");
    guard(LUA_COLIBNAME, "close");
}

int LuaEngine::l_safe_load(lua_State* L) {
    // load(chunk [, chunkname [, mode [, env]]]) のmodeを"t"に差し替えて元のloadへ渡す。
    // envは「渡されなかった」と「nilを渡した」で意味が違うので、渡されたときだけ残す
    luaL_checkany(L, 1);
    const bool has_env = lua_gettop(L) >= 4;
    lua_settop(L, 4);
    lua_pushstring(L, "t");
    lua_replace(L, 3);
    if (!has_env) lua_settop(L, 3);
    lua_pushvalue(L, lua_upvalueindex(2));
    lua_insert(L, 1);
    lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
    return lua_gettop(L);
}

int LuaEngine::l_safe_setmetatable(lua_State* L) {
    if (lua_type(L, 2) == LUA_TTABLE) {
        lua_pushliteral(L, "__gc");
        const int t = lua_rawget(L, 2);
        lua_pop(L, 1);
        if (t != LUA_TNIL) return luaL_error(L, "__gc(ファイナライザ)は使えません(サンドボックス)");
    }
    lua_pushvalue(L, lua_upvalueindex(2));
    lua_insert(L, 1);
    lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
    return lua_gettop(L);
}

int LuaEngine::l_print(lua_State* L) {
    FixedString<PICO_STR_256B> line;
    const int n = lua_gettop(L);
    for (int i = 1; i <= n; i++) {
        if (i > 1) line.append("\t");
        if (lua_type(L, i) == LUA_TSTRING || lua_type(L, i) == LUA_TNUMBER) {
            lua_pushvalue(L, i);
            line.append(lua_tostring(L, -1));
            lua_pop(L, 1);
        } else {
            char buf[PICO_STR_M];
            LuaDebugger::FormatValue(L, i, buf, sizeof(buf));
            line.append(buf);
        }
    }
    LOG_APP_MSG("%s", line.c_str());
    return 0;
}

int LuaEngine::GuardedFinish(lua_State* L, int, lua_KContext) {
    LuaEngine* self = static_cast<LuaEngine*>(lua_touserdata(L, lua_upvalueindex(1)));
    if (self->aborting_) return RaiseAbort(L);
    return lua_gettop(L);
}

int LuaEngine::l_guarded_call(lua_State* L) {
    // coroutine.resume(co, ...): 作った後に変わったフックの設定(ブレークポイント等)をそのスレッドへ合わせる
    if (lua_type(L, 1) == LUA_TTHREAD) {
        LuaEngine* self = static_cast<LuaEngine*>(lua_touserdata(L, lua_upvalueindex(1)));
        self->ApplyHook(lua_tothread(L, 1));
    }
    lua_pushvalue(L, lua_upvalueindex(2));
    lua_insert(L, 1);
    // yieldをまたいでも動くよう、継続関数つきで呼ぶ(元のpcallがyieldに対応しているため)
    lua_callk(L, lua_gettop(L) - 1, LUA_MULTRET, 0, GuardedFinish);
    return GuardedFinish(L, LUA_OK, 0);
}

// coroutine.wrap(f): 標準のものと同じ振る舞いだが、呼ぶたびにスレッドのフックを合わせ、
// 打ち切り中ならメッセージハンドラを呼ばずに巻き戻す(lcorolib.cのauxwrap/auxresumeと同じ手順)
int LuaEngine::l_wrap(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_State* co = lua_newthread(L);
    lua_pushvalue(L, 1);
    lua_xmove(L, co, 1);
    lua_pushvalue(L, lua_upvalueindex(1)); // this
    lua_insert(L, -2);                     // upvalue1=this, upvalue2=スレッド
    lua_pushcclosure(L, l_wrap_aux, 2);
    return 1;
}

int LuaEngine::l_wrap_aux(lua_State* L) {
    LuaEngine* self = static_cast<LuaEngine*>(lua_touserdata(L, lua_upvalueindex(1)));
    lua_State* co = lua_tothread(L, lua_upvalueindex(2));
    self->ApplyHook(co);
    const int narg = lua_gettop(L);
    if (!lua_checkstack(co, narg)) return luaL_error(L, "too many arguments to resume");
    int nres = 0;
    int status;
    if (lua_status(co) == LUA_OK && lua_gettop(co) == 0) {
        lua_pushliteral(L, "cannot resume dead coroutine");
        status = LUA_ERRRUN;
    } else {
        lua_xmove(L, co, narg);
        status = lua_resume(co, L, narg, &nres);
        if (status == LUA_OK || status == LUA_YIELD) {
            if (!lua_checkstack(L, nres + 1)) {
                lua_pop(co, nres);
                return luaL_error(L, "too many results to resume");
            }
            lua_xmove(co, L, nres);
            if (self->aborting_) return RaiseAbort(L);
            return nres;
        }
        lua_xmove(co, L, 1); // エラーの値
        lua_closethread(co, L);
    }
    if (self->aborting_) return RaiseAbort(L);
    if (status != LUA_ERRMEM && lua_type(L, -1) == LUA_TSTRING) {
        luaL_where(L, 1);
        lua_insert(L, -2);
        lua_concat(L, 2);
    }
    return lua_error(L);
}

int LuaEngine::RaiseAbort(lua_State* L) {
    // lua_error()では投げない。lua_error()はxpcallのメッセージハンドラ(Luaの関数)を呼ぶが、
    // フックの中から投げるとそのハンドラはフック無しで動くので、ハンドラの中の終わらないループを
    // 止められなくなる。メモリ不足(LUA_ERRMEM)はハンドラを呼ばずに巻き戻すので、打ち切り中は
    // 確保を断る(Alloc())ことにして、わざと確保してLUA_ERRMEMを起こす。
    // メッセージは"not enough memory"になるので、表示はReportError()がabort_msg_へ差し替える
    lua_newuserdatauv(L, 16, 0);
    return lua_error(L); // 確保が通ってしまった場合の保険(打ち切り中は届かない)
}

void LuaEngine::BeginAbort(const char* msg) {
    if (!aborting_) {
        aborting_ = true;
        abort_msg_.assign(msg ? msg : "スクリプトを打ち切りました");
    }
    ApplyHook(L);
}

void LuaEngine::ApplyHook(lua_State* th) {
    if (!th) return;
    int mask = LUA_MASKCOUNT;
    if (debugger_ && debugger_->wantsLineHook()) mask |= LUA_MASKLINE;
    // 打ち切り中は1命令ごとにフックへ来る(すぐに投げ直せるように)
    const int count = aborting_ ? 1 : kHookInstructionInterval;
    if (lua_gethook(th) == InstructionHook && lua_gethookmask(th) == mask && lua_gethookcount(th) == count) return;
    lua_sethook(th, InstructionHook, mask, count);
}

// ---------------- 実行時間の安全網(暴走防止) ----------------
// クラスコメント「実行時間の安全網」参照。

void LuaEngine::InstructionHook(lua_State* L, lua_Debug* ar) {
    // Alloc()へlua_newstate(Alloc, this)で渡したudをlua_getallocf()経由で取り戻す。
    // フック専用の状態をLuaEngine以外に持たずに済む
    void* ud = nullptr;
    lua_getallocf(L, &ud);
    LuaEngine* self = static_cast<LuaEngine*>(ud);

    // コルーチンはスレッドごとにフックを持つので、作られた後に設定が変わった分をここで合わせる
    self->ApplyHook(L);

    // 打ち切り中: 何をしていても(pcallで捕まえた後でも)すぐに投げ直す
    if (self->aborting_) {
        RaiseAbort(L);
        return;
    }

    if (ar->event == LUA_HOOKLINE) {
        if (self->debugger_) {
            const LuaDebugger::Command c = self->debugger_->onLine(L, ar);
            if (c == LuaDebugger::Command::Abort) {
                self->BeginAbort("デバッガで停止しました");
                self->loop_broken_ = true;
                RaiseAbort(L);
                return;
            }
            // ステップ実行の切り替え等で行フックの要否が変わる
            self->ApplyHook(L);
        }
        return;
    }

    if (self->instructions_remaining_ <= (uint32_t)kHookInstructionInterval) {
        // 打ち切りに入る。以降はpcall等で捕まえても投げ直される(l_guarded_call)ので、
        // ProtectedCall()(C++側の一番外のlua_pcall)まで必ず戻る
        char msg[PICO_STR_256B];
        snprintf(msg, sizeof(msg), "スクリプトの実行が命令数の上限(%u)を超えたため打ち切りました"
                 "(無限ループの可能性があります)", (unsigned)kMaxInstructionsPerCall);
        // luaL_whereで場所(main.lua:12:)を付ける
        luaL_where(L, 0); // フックの中では段0が実行中の関数
        lua_pushstring(L, msg);
        lua_concat(L, 2);
        self->BeginAbort(lua_tostring(L, -1));
        lua_pop(L, 1);
        self->ApplyHook(L); // 今のスレッド(コルーチンの中かもしれない)も1命令ごとに
        // メッセージハンドラは呼ばれない(RaiseAbort()参照)ので、トレースはここで作る
        LuaDebugger::BuildTrace(L, 0, self->last_trace_, sizeof(self->last_trace_), 10);
        RaiseAbort(L);
        return; // 到達しないが、"呼んだら戻らない"ことを読み手へ明示するため書いておく
    }
    self->instructions_remaining_ -= kHookInstructionInterval;
}

int LuaEngine::MessageHandler(lua_State* L) {
    void* ud = nullptr;
    lua_getallocf(L, &ud);
    LuaEngine* self = static_cast<LuaEngine*>(ud);

    // エラーの値を文字列にそろえる(error({...})等。__tostringはLuaのコードなので呼ばない)
    if (lua_type(L, 1) != LUA_TSTRING && lua_type(L, 1) != LUA_TNUMBER) {
        if (lua_isnil(L, 1)) lua_pushstring(L, "エラー(nil)");
        else lua_pushfstring(L, "エラー(値の種類: %s)", luaL_typename(L, 1));
        lua_replace(L, 1);
    }
    const char* msg = lua_tostring(L, 1);

    // スタックトレース(段1 = エラーを起こした関数)
    // 深い再帰(Cのスタックあふれ)のエラーでも呼ばれるので、スタックに大きな配列を置かずメンバへ直接書く
    LuaDebugger::BuildTrace(L, 1, self->last_trace_, sizeof(self->last_trace_), 10);

    // デバッガ: 捕まえられなかったエラーで止まる(続けてもエラーとして進む)
    if (self->debugger_ && !self->aborting_) {
        const LuaDebugger::Command c = self->debugger_->pause(L, LuaDebugger::Reason::Error, 1, msg);
        if (c == LuaDebugger::Command::Abort) self->loop_broken_ = true;
    }
    lua_settop(L, 1);
    return 1;
}

int LuaEngine::ProtectedCall(int nargs, int nresults) {
    const int func = lua_gettop(L) - nargs;
    const bool outer = (call_depth_ == 0);
    if (outer) {
        // 予算と打ち切りは一番外の呼び出しでだけ積み直す。pico.set等から入れ子でLuaへ戻る
        // 呼び出し(Dispatch)で積み直すと、それを繰り返して上限を逃れられてしまうため
        instructions_remaining_ = kMaxInstructionsPerCall;
        aborting_ = false;
        last_trace_[0] = '\0';
        ApplyHook(L);
        LuaDebugger::NotifyActivity(true, app_dir_.c_str());
    }

    lua_pushcfunction(L, MessageHandler);
    lua_insert(L, func);

    call_depth_++;
    const uint32_t t0 = outer ? (uint32_t)micros() : 0;
    const int status = lua_pcall(L, nargs, nresults, func);
    call_depth_--;
    lua_remove(L, func); // メッセージハンドラ(エラーならその上にメッセージが残る)

    if (outer) {
        ProfilerFunctions::AddLuaMicros((uint32_t)micros() - t0);
        LuaDebugger::NotifyActivity(false, nullptr);
        // pico.image_target()で描き先を画像へ向けたまま呼び出しを抜けたら、画面へ戻す
        // (OSData::frameは全ウィジェットの共有なので、残すと他の描画が画像へ吸い込まれる)
        EndImageTarget();
        last_aborted_ = (status != LUA_OK) && aborting_;
        if (aborting_) {
            aborting_ = false;
            ApplyHook(L);
        }
    }
    return status;
}

void LuaEngine::ReportError(const char* fallback) {
    const char* msg = lua_tostring(L, -1);
    if (!msg) msg = fallback;
    // 打ち切りはLUA_ERRMEM("not enough memory")として戻ってくるので、本当の理由に差し替える
    if (call_depth_ == 0 && last_aborted_) {
        msg = abort_msg_.c_str();
        last_aborted_ = false;
    }

    // 入れ子の呼び出し(pico.set等から鳴ったコールバック)が打ち切りで失敗した場合は、
    // 外側の呼び出しも同じ打ち切りで必ず失敗するので、そちらで1回だけ出す
    if (call_depth_ > 0 && aborting_) {
        lua_pop(L, 1);
        return;
    }

    // バックグラウンドスクリプトは画面を持たないので、ダイアログは出さない(今の画面の上に出ても困る)。
    // ログ・クラッシュダンプ(/crash/lua_NNNN.txt)・lastError()(バックグラウンドの一覧に出す)へ
    if (background_) {
        last_error_.assign(msg);
        LOG_APP_FAIL("Luaのバックグラウンド(%s): %s", app_dir_.c_str(), msg);
        if (last_trace_[0]) LOG_APP_FAIL("Luaのスタックトレース:\n%s", last_trace_);
        LuaDebugger::ReportError(app_dir_.c_str(), msg, last_trace_);
        lua_pop(L, 1);
        return;
    }

    FixedString<PICO_STR_512B> shown;
    shown.assign(msg);
    if (last_trace_[0]) {
        // ダイアログには先頭の数段だけ(全部はログとクラッシュダンプへ)
        shown.append("\n");
        int lines = 0;
        const char* p = last_trace_;
        while (*p && lines < 4) {
            const char* nl = strchr(p, '\n');
            const size_t n = nl ? (size_t)(nl - p) : strlen(p);
            if (lines > 0) shown.append("\n");
            shown.append(p, n);
            lines++;
            p = nl ? nl + 1 : p + n;
        }
        LOG_APP_FAIL("Luaのスタックトレース:\n%s", last_trace_);
    }
    LuaDebugger::ReportError(app_dir_.c_str(), msg, last_trace_);
    ErrorFunctions::ShowFatal(shown.c_str());
    lua_pop(L, 1);
}

LuaEngine::LuaEngine(size_t budget_bytes, const LuaPermissions& permissions, const char* app_dir, bool background)
    : budget_(budget_bytes), permissions_(permissions), background_(background) {
    if (!PICO_IO::normalize(app_dir_, app_dir)) app_dir_.assign("/");
    CreateExt();

    L = lua_newstate(Alloc, this);
    if (!L) {
        LOG_APP_FAIL("LuaEngine: lua_newstateに失敗しました(予算%zuB)", budget_bytes);
        return;
    }
    *static_cast<LuaEngine**>(lua_getextraspace(L)) = this;   // pico.* の Self() が読む

    // バックグラウンドスクリプトには付けない(見ていない画面の裏で止まると、OSごと止まって見えるため)
    if (LuaDebugger::GlobalEnabled() && !background_) debugger_ = new LuaDebugger();

    // 以降の全てのLua実行(ライブラリを開くところも含む)に効かせるため、pcallより前に設定する
    ApplyHook(L);

    // luaL_openlibs()やregisterApi()の途中でOOMになった場合、pcallで保護せずに
    // 直接呼ぶとLuaは(保護フレームが無いため)abort()してしまう
    // (script/host_test/lua_alloc_budget_test.cppで確認済み)。
    // 必ずpcall越しに呼ぶことでLUA_ERRMEMとして安全に失敗させる。
    lua_pushcfunction(L, InitTrampoline);
    lua_pushlightuserdata(L, this);
    if (ProtectedCall(1) != LUA_OK) {
        LOG_APP_FAIL("LuaEngine: 初期化に失敗しました(予算%zuB): %s",
                     budget_bytes, lua_tostring(L, -1));
        lua_close(L);
        L = nullptr;
        return;
    }
}

LuaEngine::~LuaEngine() {
    DestroyExt();
    DestroyIso();
    // 鳴らしっぱなし(長さ0)の音を残したままアプリを閉じると鳴り止まないので、
    // 音を使ったアプリは閉じるときに全部止める
    if (used_sound_) SoundFunctions::StopAll();
    if (used_music_) SoundFunctions::MusicStop();
    if (used_wav_) SoundFunctions::WavStop();
    ReleaseMic();
    // パレットを変えたアプリは閉じる/別の画面へ移るときに既定へ戻す
    if (used_palette_) PICO_GFX::ResetPalette();
    DestroyHttp(); // lua_close()より前でも後でも問題ない(HttpStateはLuaと無関係のC++側の状態)
    if (L) {
        // __gcはsetmetatableで断っているが、念のため打ち切り中にしてから閉じる
        // (閉じる途中でLuaのコードが動いても確保できずにすぐ終わる)
        BeginAbort("アプリを閉じています");
        lua_close(L);
    }
    delete debugger_;
}

bool LuaEngine::Run(const char* script, const char* chunkname) {
    if (!L) return false;

    // SD上のパス("/..."で始まる)は"@パス"というチャンク名にする。エラーやスタックトレースが
    // [string "..."]ではなく "main.lua:12:" の形になり、デバッガがソースの行をSDから読める
    FixedString<PICO_PATH_LEN> name;
    if (chunkname && chunkname[0] == '/') {
        name.assign("@");
        name.append(chunkname);
    } else {
        name.assign(chunkname ? chunkname : "script");
    }

    // require("名前") されるモジュールを、実行の外の浅い所で先に読み込んでおく
    // (実行中のコンパイルでコア0のスタックを溢れさせないため。クラスコメント参照)
    lua_pushcfunction(L, PreloadTrampoline);
    lua_pushlightuserdata(L, this);
    lua_pushlightuserdata(L, (void*)script);
    lua_pushinteger(L, (lua_Integer)strlen(script));
    if (ProtectedCall(3) != LUA_OK) {
        LOG_APP_WARN("LuaEngine: require先の先読みに失敗しました: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
    }

    // テキストだけ受け付ける(バイトコードはVMを壊せるため。サンドボックス参照)
    if (luaL_loadbufferx(L, script, strlen(script), name.c_str(), "t") != LUA_OK) {
        last_trace_[0] = '\0';
        ReportError("Luaスクリプトの構文エラー");
        return false;
    }

    if (ProtectedCall(0) != LUA_OK) {
        ReportError("Luaスクリプトの実行時エラー");
        return false;
    }

    return true;
}

void LuaEngine::callGlobalNoArgs(const char* name) {
    if (!L) return;

    lua_getglobal(L, name);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return;
    }

    if (ProtectedCall(0) != LUA_OK) {
        ReportError("Luaスクリプトの実行時エラー");
    }
}

void LuaEngine::CallSetup() {
    callGlobalNoArgs("setup");
}

void LuaEngine::CallLoop(uint32_t dt_ms) {
    if (!L || loop_broken_) return;

    lua_getglobal(L, "loop");
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return;
    }

    lua_pushinteger(L, (lua_Integer)dt_ms);
    if (ProtectedCall(1) != LUA_OK) {
        // 毎フレーム同じエラーダイアログが積まれ続けないよう、以降はloop()を呼ばない
        loop_broken_ = true;
        ReportError("loop()の実行時エラー");
    }
}

bool LuaEngine::DispatchKey(const KeyInputFunctions::Event& ev) {
    using KeyInputFunctions::Key;
    if (!L || key_callback_ref_ == LUA_NOREF) return false;

    static const char* const kNames[] = {
        nullptr, "enter", "backspace", "tab", "escape", "delete",
        "left", "right", "up", "down", "home", "end", "pageup", "pagedown", "zenhan",
        "playpause", "stop", "nexttrack", "prevtrack", "volumeup", "volumedown", "mute",
    };
    static_assert(sizeof(kNames) / sizeof(kNames[0]) == (size_t)Key::Mute + 1, "キーの名前の表がKeyと合っていない");

    lua_rawgeti(L, LUA_REGISTRYINDEX, key_callback_ref_);
    // 第1引数: 文字ならその文字(UTF-8)、特殊キーなら名前。第2引数: {ctrl=,alt=,shift=}
    if (ev.key == Key::Char) {
        lua_pushfstring(L, "%U", (long)ev.cp);
    } else {
        const size_t i = (size_t)ev.key;
        lua_pushstring(L, i < sizeof(kNames) / sizeof(kNames[0]) && kNames[i] ? kNames[i] : "unknown");
    }
    lua_createtable(L, 0, 3);
    lua_pushboolean(L, ev.ctrl());  lua_setfield(L, -2, "ctrl");
    lua_pushboolean(L, ev.alt());   lua_setfield(L, -2, "alt");
    lua_pushboolean(L, ev.shift()); lua_setfield(L, -2, "shift");

    if (ProtectedCall(2, 1) != LUA_OK) {
        // 毎打鍵同じエラーのダイアログが積まれないよう、以降は呼ばない(loop()と同じ安全弁)
        luaL_unref(L, LUA_REGISTRYINDEX, key_callback_ref_);
        key_callback_ref_ = LUA_NOREF;
        ReportError("on_key()の実行時エラー");
        return true; // 打鍵はここで消費した扱い(キー盤へ回さない)
    }
    const bool handled = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return handled;
}

// ---------------- pico.* API登録 ----------------

// pico.* は素のCの関数として置く(thisはLuaの状態ごとの予備の領域 lua_getextraspace() に入れてあり、
// Self() がそこから読む。作ったコルーチンへは lua_newthread() が写す)。以前は this を上位値に持つ
// Cのクロージャにしていたが、関数が約350個あり、クロージャだけで実機で約8KB(PCで約17KB)をLuaの予算から使っていた
void LuaEngine::registerFn(const char* name, lua_CFunction fn) {
    api_cur_->push_back({name, fn});   // テーブルへは finishApiTable() の __index が使うときに入れる
}

// 名前の順に並べ(同じ名前は後から足したもの = 差し替えを残す)、スタックのトップのテーブルの __index にする
void LuaEngine::finishApiTable(std::vector<luaL_Reg>& api, int constants) {
    std::stable_sort(api.begin(), api.end(),
                     [](const luaL_Reg& a, const luaL_Reg& b) { return strcmp(a.name, b.name) < 0; });
    size_t n = 0;
    for (size_t i = 0; i < api.size(); i++) {
        if (n > 0 && strcmp(api[n - 1].name, api[i].name) == 0) api[n - 1] = api[i];
        else api[n++] = api[i];
    }
    api.resize(n);
    api.shrink_to_fit();
    lua_createtable(L, 0, 1);
    lua_pushlightuserdata(L, &api);
    lua_pushinteger(L, constants);
    lua_pushcclosure(L, l_api_index, 2);
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, -2);
}

// pico.xxx が無かったとき: 表から引き、見つかったらテーブルへ入れて(次からは普通に引ける)返す
int LuaEngine::l_api_index(lua_State* L) {
    if (lua_type(L, 2) != LUA_TSTRING) return 0;
    const char* key = lua_tostring(L, 2);
    const auto* api = static_cast<const std::vector<luaL_Reg>*>(lua_touserdata(L, lua_upvalueindex(1)));
    const auto it = std::lower_bound(api->begin(), api->end(), key,
                                     [](const luaL_Reg& r, const char* k) { return strcmp(r.name, k) < 0; });
    if (it == api->end() || strcmp(it->name, key) != 0) {
        // 関数が無ければ数値定数(pico.COLOR_RED 等)。これも次からは普通に引けるようテーブルへ入れる
        int32_t v = 0;
        const lua_Integer kind = lua_tointeger(L, lua_upvalueindex(2));
        const bool found = kind == 1 ? LuaConstants::FindPico(key, v)
                         : kind == 2 ? LuaConstants::FindIso(key, v) : false;
        if (!found) return 0;
        lua_pushinteger(L, v);
        lua_pushvalue(L, 2);
        lua_pushvalue(L, -2);
        lua_rawset(L, 1);
        return 1;
    }
    lua_pushcfunction(L, it->func);
    lua_pushvalue(L, 2);
    lua_pushvalue(L, -2);
    lua_rawset(L, 1);
    return 1;
}

void LuaEngine::registerApi() {
    api_pico_.reserve(240);
    lua_newtable(L);
    registerFn("create", l_create);
    registerFn("destroy", l_destroy);
    registerFn("set", l_set);
    registerFn("get", l_get);
    registerFn("on", l_on);
    registerFn("add_child", l_add_child);
    registerFn("remove_child", l_remove_child);
    registerFn("list_add", l_list_add);
    registerFn("list_clear", l_list_clear);
    registerFn("tab_add", l_tab_add);
    registerFn("log", l_log);
    registerFn("show_error", l_show_error);
    registerFn("pop", l_pop);
    registerFn("push_scene", l_push_scene);
    registerFn("change_scene", l_change_scene);
    registerFn("launch_app", l_launch_app);
    registerFn("notify", l_notify);
    registerFn("notify_cancel", l_notify_cancel);
    registerFn("notify_list", l_notify_list);
    registerFn("launch_reason", l_launch_reason);
    registerFn("content_rect", l_content_rect);
    registerFn("get_time", l_get_time);
    registerFn("get_touch", l_get_touch);
    registerFn("millis", l_millis);
    registerFn("micros", l_micros);
    registerFn("battery", l_battery);
    registerFn("on_key", l_on_key);
    registerFn("pad_connected", l_pad_connected);
    registerFn("pad_down", l_pad_down);
    registerFn("pad_pressed", l_pad_pressed);
    registerFn("pad_released", l_pad_released);
    registerFn("sound_available", l_sound_available);
    registerFn("sound_rate", l_sound_rate);
    registerFn("sound_set_rate", l_sound_set_rate);
    registerFn("beep", l_beep);
    registerFn("sound_play", l_sound_play);
    registerFn("sound_stop", l_sound_stop);
    registerFn("sound_playing", l_sound_playing);
    registerFn("note_freq", l_note_freq);
    registerFn("music_play", l_music_play);
    registerFn("music_play_text", l_music_play_text);
    registerFn("music_stop", l_music_stop);
    registerFn("music_playing", l_music_playing);
    registerFn("wav_play", l_wav_play);
    registerFn("wav_stop", l_wav_stop);
    registerFn("wav_playing", l_wav_playing);
    registerFn("wav_pause", l_wav_pause);
    registerFn("wav_paused", l_wav_paused);
    registerFn("wav_position", l_wav_position);
    registerFn("wav_duration", l_wav_duration);
    registerFn("wav_seek", l_wav_seek);
    registerFn("invalidate", l_invalidate);
    registerFn("mark_dirty", l_mark_dirty);
    registerFn("draw_pixel", l_draw_pixel);
    registerFn("draw_line", l_draw_line);
    registerFn("draw_rect", l_draw_rect);
    registerFn("fill_rect", l_fill_rect);
    registerFn("draw_circle", l_draw_circle);
    registerFn("fill_circle", l_fill_circle);
    registerFn("draw_ellipse", l_draw_ellipse);
    registerFn("fill_ellipse", l_fill_ellipse);
    registerFn("draw_triangle", l_draw_triangle);
    registerFn("fill_triangle", l_fill_triangle);
    registerFn("draw_polygon", l_draw_polygon);
    registerFn("fill_polygon", l_fill_polygon);
    registerFn("draw_arc", l_draw_arc);
    registerFn("fill_arc", l_fill_arc);
    registerFn("text_width", l_text_width);
    registerFn("clear_rect", l_clear_rect);
    registerFn("draw_text", l_draw_text);
    registerFn("set_draw_area", l_set_draw_area);
    registerFn("clear_draw_area", l_clear_draw_area);
    registerFn("get_draw_area", l_get_draw_area);
    registerFn("draw_image", l_draw_image);
    registerFn("draw_image_part", l_draw_image_part);
    registerFn("draw_image_ex", l_draw_image_ex);
    registerFn("image_load", l_image_load);
    registerFn("image_size", l_image_size);
    registerFn("image_free", l_image_free);
    registerFn("canvas_clear", l_canvas_clear);
    registerFn("canvas_save", l_canvas_save);
    registerFn("canvas_load", l_canvas_load);
    registerFn("canvas_undo", l_canvas_undo);
    registerFn("sd_exists", l_sd_exists);
    registerFn("sd_read", l_sd_read);
    registerFn("sd_write", l_sd_write);
    registerFn("sd_remove", l_sd_remove);
    registerFn("sd_mkdir", l_sd_mkdir);
    registerFn("sd_list", l_sd_list);
    registerFn("sd_stat", l_sd_stat);
    registerFn("sd_read_part", l_sd_read_part);
    registerFn("config_read", l_config_read);
    registerFn("config_get", l_config_get);
    registerFn("config_write", l_config_write);
    registerFn("show_message", l_show_message);
    registerFn("show_input", l_show_input);
    registerFn("show_file_save", l_show_file_save);
    registerFn("show_file_select", l_show_file_select);
    registerFn("show_color", l_show_color);
    registerFn("http_request", l_http_request);
    registerFn("http_cancel", l_http_cancel);
    registerFn("http_close", l_http_close);
    registerFn("json_decode", l_json_decode);
    registerFn("json_encode", l_json_encode);
    lua_pushlightuserdata(L, LuaJson::NullValue());
    lua_setfield(L, -2, "json_null");
    registerFn("after", l_after);
    registerFn("every", l_every);
    registerFn("cancel", l_cancel);
    registerFn("args", l_args);
    registerFn("store_load", l_store_load);
    registerFn("store_save", l_store_save);
    registerFn("require", l_require);
    registerFn("traceback", l_traceback);
    registerFn("breakpoint", l_breakpoint);
    registerFn("set_breakpoint", l_set_breakpoint);
    registerFn("clear_breakpoint", l_clear_breakpoint);
    registerFn("debugger_enabled", l_debugger_enabled);
    registerFn("is_background", l_is_background);
    registerFn("finish", l_finish);
    RegisterExtApi();
    if (background_) filterBackgroundApi();
    finishApiTable(api_pico_, 1);
    // グローバルの require は pico.require と同じ関数
    lua_getfield(L, -1, "require");
    lua_setglobal(L, "require");
    lua_setglobal(L, "pico");
}

// ---------------- バックグラウンドスクリプト ----------------

namespace {
    // バックグラウンドスクリプトで使える pico.*。画面(ウィジェット・描画・ダイアログ・画面の遷移)・
    // 入力・音・デバッガは外す(画面を持たず、今前に出ている別のアプリの上で動くため)
    const char* const kBackgroundApi[] = {
        "log", "notify", "notify_cancel", "notify_list",
        "get_time", "time", "millis", "micros", "battery", "wifi_status",
        "sd_exists", "sd_read", "sd_write", "sd_remove", "sd_mkdir", "sd_list", "sd_stat", "sd_read_part",
        "config_read", "config_get", "config_write",
        "http_request", "http_cancel", "http_close",
        "json_decode", "json_encode", "json_pick", "sd_json_pick", "xml_pick", "sd_xml_pick", "form_encode",
        "after", "every", "cancel",
        "store_load", "store_save", "require", "traceback",
        "app_dir", "path_join", "url_encode", "url_decode", "base64_encode", "base64_decode",
        "settings_get", "settings_set", "settings_all", "memory_info",
        "encrypt", "decrypt", "is_encrypted", "hash", "random_bytes",
        "is_background", "finish",
    };
}

void LuaEngine::filterBackgroundApi() {
    size_t n = 0;
    for (size_t i = 0; i < api_pico_.size(); i++) {
        bool keep = false;
        for (const char* name : kBackgroundApi) {
            if (strcmp(api_pico_[i].name, name) == 0) { keep = true; break; }
        }
        if (keep) api_pico_[n++] = api_pico_[i];
    }
    api_pico_.resize(n);
}

int LuaEngine::l_is_background(lua_State* L) {
    lua_pushboolean(L, Self(L)->background_);
    return 1;
}

int LuaEngine::l_finish(lua_State* L) {
    LuaEngine* self = Self(L);
    if (self->background_) self->finish_requested_ = true;
    lua_pushboolean(L, self->background_);
    return 1;
}

bool LuaEngine::timersActive() const {
    for (const Timer& t : timers_) {
        if (t.used) return true;
    }
    return false;
}

// ---------------- コールバック中継 ----------------

bool LuaEngine::EventKindFromName(const char* name, EventKind& out) {
    static const struct { const char* name; EventKind kind; } kTable[] = {
        {"press_start", EventKind::PressStart},
        {"press_end", EventKind::PressEnd},
        {"press_move", EventKind::PressMove},
        {"press_out", EventKind::PressOut},
        {"render", EventKind::Render},
        {"closed", EventKind::Closed},
        {"checked_changed", EventKind::CheckedChanged},
        {"value_changed", EventKind::ValueChanged},
        {"select_item", EventKind::SelectItem},
        {"tab_changed", EventKind::TabChanged},
        {"dropdown_changed", EventKind::DropdownChanged},
        {"text_changed", EventKind::TextChanged},
        {"duration_changed", EventKind::DurationChanged},
        {"day_selected", EventKind::DaySelected},
        {"link_tap", EventKind::LinkTap},
        {"text_tap", EventKind::TextTap},
        {"text_input", EventKind::TextInput},
        {"scrolled", EventKind::Scrolled},
        {"long_press", EventKind::LongPress},
        {"double_tap", EventKind::DoubleTap},
        {"swipe", EventKind::Swipe},
    };
    for (const auto& e : kTable) {
        if (strcmp(e.name, name) == 0) { out = e.kind; return true; }
    }
    return false;
}

LuaEngine::CallbackBinding* LuaEngine::FindCallback(WidgetId id, EventKind kind) {
    // callbacks_はid昇順に保ってあるので、まずidの範囲だけをlower_bound()で絞る。
    // 同じidが持ちうるイベント種別はEventKindの総数ぶん(現状12種)しかないため、
    // そこから先の線形走査は実質定数時間で終わる
    auto it = std::lower_bound(callbacks_.begin(), callbacks_.end(), id,
        [](const CallbackBinding& e, WidgetId key) { return e.id < key; });
    for (; it != callbacks_.end() && it->id == id; ++it) {
        if (it->kind == kind) return &*it;
    }
    return nullptr;
}

void LuaEngine::BindCallback(Widget* w, WidgetId id, EventKind kind, int ref) {
    if (CallbackBinding* existing = FindCallback(id, kind)) {
        // 同じid+kindへ再度onした場合は古いrefを捨てて差し替える(リーク防止)。
        // Widget側のstd::functionは初回のBindCallbackで既に配線済みなので繋ぎ直し不要
        luaL_unref(L, LUA_REGISTRYINDEX, existing->ref);
        existing->ref = ref;
        return;
    }

    // 新規挿入はid昇順を保つ位置(lower_bound)へ差し込む。同じidの中での並びは
    // 探索(FindCallback)が線形走査するだけなので問わない
    auto pos = std::lower_bound(callbacks_.begin(), callbacks_.end(), id,
        [](const CallbackBinding& e, WidgetId key) { return e.id < key; });
    callbacks_.insert(pos, {id, kind, ref});

    // キャプチャするのはthis(LuaEngine*)とid(WidgetId=uint32_t)だけなので、
    // std::functionの小バッファに収まりヒープ確保は起きない(クラスコメント参照)
    switch (kind) {
        case EventKind::PressStart:
            w->setOnPressStart([this, id]() { this->Dispatch(id, EventKind::PressStart); });
            break;
        case EventKind::PressEnd:
            w->setOnPressEnd([this, id]() { this->Dispatch(id, EventKind::PressEnd); });
            break;
        case EventKind::PressMove:
            w->setOnPressMove([this, id]() { this->Dispatch(id, EventKind::PressMove); });
            break;
        case EventKind::PressOut:
            w->setOnPressOut([this, id]() { this->Dispatch(id, EventKind::PressOut); });
            break;
        case EventKind::Render:
            // l_on()側でLuaCanvasにしか許していないので安全にstatic_castできる
            static_cast<LuaCanvas*>(w)->setOnRender([this, id]() { this->Dispatch(id, EventKind::Render); });
            break;
        case EventKind::Closed:
            // ここでは何もしない: ダイアログのsetOnClosed/setOnClose配線自体は
            // 生成時点(pico.show_xxx() → WireDialogClosed())で既に済んでいる。
            // pico.on()はcallbacks_への登録(Dispatch()が引くref)だけを担う
            break;
        // ウィジェット固有イベント(クラスコメント「ウィジェット固有イベント」参照)。
        // l_on()側で対応するWidgetTypeであることを確認済みなので安全にstatic_castできる。
        // CheckedChanged/ValueChanged/TabChangedは変わった後の値そのものを渡さず、
        // 既存の共通Dispatch(id, kind)(idのみ)に乗せる(値はpico.get()で読む)
        case EventKind::CheckedChanged:
            static_cast<Checkbox*>(w)->setOnChangeChecked(
                [this, id]() { this->Dispatch(id, EventKind::CheckedChanged); });
            break;
        case EventKind::ValueChanged:
            static_cast<NumberSlider*>(w)->setOnValueChanged(
                [this, id]() { this->Dispatch(id, EventKind::ValueChanged); });
            break;
        case EventKind::TabChanged:
            static_cast<TabBar*>(w)->setOnChanged(
                [this, id](int) { this->OnTabChanged(id); });
            break;
        case EventKind::DropdownChanged:
            static_cast<DropdownMenu*>(w)->setOnChanged(
                [this, id]() { this->Dispatch(id, EventKind::DropdownChanged); });
            break;
        case EventKind::TextChanged:
            static_cast<TextboxT*>(w)->setOnTextChanged(
                [this, id]() { this->Dispatch(id, EventKind::TextChanged); });
            break;
        case EventKind::DurationChanged:
        case EventKind::DaySelected:
        case EventKind::LinkTap:
        case EventKind::TextTap:
        case EventKind::TextInput:
        case EventKind::Scrolled:
        case EventKind::LongPress:
        case EventKind::DoubleTap:
        case EventKind::Swipe:
            this->BindExtCallback(w, id, kind);
            break;
        case EventKind::SelectItem:
            // already_selectedは永続プロパティとして持てない一時的な値なので、
            // DispatchClosedと同じ形の専用Dispatchで2引数目として渡す
            // (indexは"selected_index"プロパティとして既に読めるので渡さない)
            static_cast<ScrollList*>(w)->setOnSelectItem(
                [this, id](int, bool already_selected) { this->DispatchSelectItem(id, already_selected); });
            break;
    }
}

void LuaEngine::PruneCallbacksFor(WidgetId id) {
    // callbacks_はid昇順なので、このidの区間はlower_bound()で先頭を絞ってから
    // 連続する分だけ前へ進めば良い(同じidは高々EventKindの種類数ぶんしか無い)
    auto first = std::lower_bound(callbacks_.begin(), callbacks_.end(), id,
        [](const CallbackBinding& e, WidgetId key) { return e.id < key; });
    auto last = first;
    while (last != callbacks_.end() && last->id == id) ++last;

    for (auto it = first; it != last; ++it) {
        luaL_unref(L, LUA_REGISTRYINDEX, it->ref);
    }
    callbacks_.erase(first, last);

    // 名前・タブの連動も、このウィジェットが消えたら一緒に片付ける
    ForgetExt(id);
}

void LuaEngine::Dispatch(WidgetId id, EventKind kind) {
    CallbackBinding* e = FindCallback(id, kind);
    if (!e) return; // pico.destroy()等で既に外れている

    // Lua呼び出しの中からpico.on/pico.destroyが起きるとcallbacks_が再確保・移動
    // されうるので、eを跨いで持ち越さずrefだけ値でコピーしておく
    const int ref = e->ref;

    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    lua_pushinteger(L, (lua_Integer)id);
    int nargs = 1;

    // タッチのイベントには座標を添える: fn(id, x, y, lx, ly, dx, dy)
    //   x,y   = 画面座標(pico.get_touch()と同じ)
    //   lx,ly = そのウィジェットの左上からの座標(盤面のマスを逆算するときに使う)
    //   dx,dy = 前のタッチのイベントからの移動量(press_startでは0。ドラッグ・スクロールに使う)
    if (kind == EventKind::PressStart || kind == EventKind::PressMove
        || kind == EventKind::PressEnd || kind == EventKind::PressOut) {
        const int x = (int)OSData::touchX;
        const int y = (int)OSData::touchY;
        int lx = x, ly = y;
        if (Widget* w = WidgetRegistry::Resolve(id)) {
            const Rect r = w->getScreenRect();
            lx = x - r.x;
            ly = y - r.y;
        }
        int dx = 0, dy = 0;
        if (kind != EventKind::PressStart) {
            dx = x - last_touch_x_;
            dy = y - last_touch_y_;
        }
        last_touch_x_ = x;
        last_touch_y_ = y;
        lua_pushinteger(L, x);
        lua_pushinteger(L, y);
        lua_pushinteger(L, lx);
        lua_pushinteger(L, ly);
        lua_pushinteger(L, dx);
        lua_pushinteger(L, dy);
        nargs = 7;
    }

    if (ProtectedCall(nargs) != LUA_OK) {
        ReportError("Luaコールバックでエラーが発生しました");
    }
}

void LuaEngine::DispatchClosed(WidgetId id, bool is_ok) {
    int ref = LUA_NOREF;
    if (CallbackBinding* e = FindCallback(id, EventKind::Closed)) ref = e->ref;

    if (ref != LUA_NOREF) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        lua_pushinteger(L, (lua_Integer)id);
        lua_pushboolean(L, is_ok);
        int nargs = 2;
        // 決定で閉じたときは、結果(入力した文字列/選んだパス/選んだ色)を3番目の引数にも渡す。
        // 閉じた後はDestroyLater()で消えるので、ここで読んでおけば順序を気にせず済む
        if (is_ok) {
            if (Widget* dw = WidgetRegistry::Resolve(id)) {
                WidgetProperty::Id pid = WidgetProperty::Id::Count;
                switch (dw->getWidgetType()) {
                    case WidgetType::InputDialog: pid = WidgetProperty::Id::Text; break;
                    case WidgetType::FileSaveDialog:
                    case WidgetType::FileSelectDialog: pid = WidgetProperty::Id::Path; break;
                    case WidgetType::ColorDialog: pid = WidgetProperty::Id::Value; break;
                    case WidgetType::PickerDialog:
                        // 選択肢は選んだ番号(0始まり)、日付/時刻/数字は結果の文字列、進捗は無し
                        switch (static_cast<PickerDialog*>(dw)->getMode()) {
                            case PickerDialog::Mode::Choice: pid = WidgetProperty::Id::SelectedIndex; break;
                            case PickerDialog::Mode::Progress: break;
                            default: pid = WidgetProperty::Id::Text; break;
                        }
                        break;
                    default: break;
                }
                WidgetProperty::Value v;
                if (pid != WidgetProperty::Id::Count && WidgetProperty::Get(dw, pid, v)) {
                    PushPropertyValue(L, v);
                    nargs = 3;
                }
            }
        }
        if (ProtectedCall(nargs) != LUA_OK) {
            ReportError("Luaコールバックでエラーが発生しました");
        }
    }
    // pico.on(id,"closed",fn)を呼んでいなくても、ダイアログは必ずここで片付ける
    // (呼び忘れがモーダルの居座りにならないようにするための保証。クラスコメント
    // 「ダイアログ」参照)
    Widget* w = WidgetRegistry::Resolve(id);
    if (w) WidgetFunctions::DestroyLater(w);
}

void LuaEngine::DispatchSelectItem(WidgetId id, bool already_selected) {
    CallbackBinding* e = FindCallback(id, EventKind::SelectItem);
    if (!e) return;
    const int ref = e->ref; // Dispatch()と同じ理由でrefだけ値コピーしておく

    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    lua_pushinteger(L, (lua_Integer)id);
    lua_pushboolean(L, already_selected);
    if (ProtectedCall(2) != LUA_OK) {
        ReportError("Luaコールバックでエラーが発生しました");
    }
}

void LuaEngine::WireDialogClosed(Widget* dialog, WidgetId id) {
    // キャプチャはthis(LuaEngine*)+id(WidgetId)だけなので、他のBindCallback同様
    // std::functionの小バッファに収まる
    auto handler = [this, id](bool is_ok) { this->DispatchClosed(id, is_ok); };
    switch (dialog->getWidgetType()) {
        case WidgetType::MsgDialog:
            static_cast<MsgDialog*>(dialog)->setOnClosed(handler);
            break;
        case WidgetType::InputDialog:
            static_cast<InputDialog*>(dialog)->setOnClosed(handler);
            break;
        case WidgetType::FileSaveDialog:
            static_cast<FileSaveDialog*>(dialog)->setOnClose(handler);
            break;
        case WidgetType::FileSelectDialog:
            static_cast<FileSelectDialog*>(dialog)->setOnClose(handler);
            break;
        case WidgetType::ColorDialog:
            static_cast<ColorDialog*>(dialog)->setOnClose(handler);
            break;
        case WidgetType::PickerDialog:
            static_cast<PickerDialog*>(dialog)->setOnClosed(handler);
            break;
        default:
            break; // pico.show_xxx()から渡される型は上の5種のみ
    }
}

// ---------------- デバッガ ----------------

int LuaEngine::l_traceback(lua_State* L) {
    LuaEngine* self = Self(L);
    const char* msg = luaL_optstring(L, 1, nullptr);
    // 段1 = pico.traceback()を呼んだLuaの関数。書く先はエラー用のバッファを借りる(スタックに1KBを置かない。
    // last_trace_を読むのはProtectedCall()が失敗した直後のReportError()だけなので、ここで上書きしてよい)
    LuaDebugger::BuildTrace(L, 1, self->last_trace_, sizeof(self->last_trace_), 10);
    if (msg) lua_pushfstring(L, "%s\n%s", msg, self->last_trace_);
    else lua_pushstring(L, self->last_trace_);
    self->last_trace_[0] = '\0';
    return 1;
}

int LuaEngine::l_breakpoint(lua_State* L) {
    LuaEngine* self = Self(L);
    const char* msg = luaL_optstring(L, 1, nullptr);
    if (!self->debugger_) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const LuaDebugger::Command c = self->debugger_->pause(L, LuaDebugger::Reason::Api, 1, msg);
    if (c == LuaDebugger::Command::Abort) {
        self->BeginAbort("デバッガで停止しました");
        self->loop_broken_ = true;
        return RaiseAbort(L);
    }
    self->ApplyHook(L);
    lua_pushboolean(L, 1);
    return 1;
}

int LuaEngine::l_set_breakpoint(lua_State* L) {
    LuaEngine* self = Self(L);
    const char* file = luaL_checkstring(L, 1);
    const lua_Integer line = luaL_checkinteger(L, 2);
    if (!self->debugger_) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const bool ok = line > 0 && line < 1000000 && self->debugger_->addBreakpoint(file, (int)line);
    self->ApplyHook(L);
    lua_pushboolean(L, ok);
    return 1;
}

int LuaEngine::l_clear_breakpoint(lua_State* L) {
    LuaEngine* self = Self(L);
    if (!self->debugger_) return 0;
    if (lua_isnoneornil(L, 1)) {
        self->debugger_->clearBreakpoints();
    } else {
        const char* file = luaL_checkstring(L, 1);
        const lua_Integer line = luaL_checkinteger(L, 2);
        self->debugger_->removeBreakpoint(file, (int)line);
    }
    self->ApplyHook(L);
    return 0;
}

int LuaEngine::l_debugger_enabled(lua_State* L) {
    lua_pushboolean(L, Self(L)->debugger_ != nullptr);
    return 1;
}

void LuaEngine::UpdateDebugger() {
    if (!L || !debugger_ || call_depth_ > 0) return;
    bool changed = false;
    debugger_->pollSerial(L, false, &changed);
    if (changed) ApplyHook(L);
}


// ---------------- タイマー ----------------

bool LuaEngine::ResolveTimerHandle(uint32_t handle, size_t& out_index) const {
    const uint32_t index1 = handle & 0xFF;
    if (index1 == 0 || index1 > kMaxTimers) return false;
    const Timer& t = timers_[index1 - 1];
    if (!t.used || t.generation != (handle >> 8)) return false;
    out_index = index1 - 1;
    return true;
}

void LuaEngine::ReleaseTimer(size_t index) {
    Timer& t = timers_[index];
    if (t.ref != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, t.ref);
    t.ref = LUA_NOREF;
    t.used = false;
    t.generation++; // 古いハンドルを無効にする
}

int LuaEngine::addTimer(lua_State* L, bool repeat) {
    LuaEngine* self = Self(L);
    const lua_Integer ms = luaL_checkinteger(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    if (ms < 0 || ms > 24LL * 60 * 60 * 1000) {
        return luaL_error(L, "pico.%s: ミリ秒は0〜86400000で指定してください", repeat ? "every" : "after");
    }
    if (repeat && ms < 1) return luaL_error(L, "pico.every: 間隔は1ミリ秒以上にしてください");

    for (size_t i = 0; i < kMaxTimers; i++) {
        Timer& t = self->timers_[i];
        if (t.used) continue;
        lua_pushvalue(L, 2);
        t.ref = luaL_ref(L, LUA_REGISTRYINDEX);
        t.used = true;
        t.repeat = repeat;
        t.interval_ms = (uint32_t)ms;
        t.due_ms = self->timer_clock_ms_ + (uint32_t)ms;
        lua_pushinteger(L, (lua_Integer)((t.generation << 8) | (uint32_t)(i + 1)));
        return 1;
    }
    LOG_APP_WARN("pico.%s: タイマーが上限(%u個)に達しています", repeat ? "every" : "after", (unsigned)kMaxTimers);
    lua_pushnil(L);
    return 1;
}

int LuaEngine::l_after(lua_State* L) { return addTimer(L, false); }
int LuaEngine::l_every(lua_State* L) { return addTimer(L, true); }

int LuaEngine::l_cancel(lua_State* L) {
    LuaEngine* self = Self(L);
    const uint32_t handle = (uint32_t)luaL_checkinteger(L, 1);
    size_t index;
    if (!self->ResolveTimerHandle(handle, index)) {
        lua_pushboolean(L, false); // 終わったタイマーや無効なハンドルは黙って無視(二重cancelを許す)
        return 1;
    }
    self->ReleaseTimer(index);
    lua_pushboolean(L, true);
    return 1;
}

void LuaEngine::UpdateTimers(uint32_t dt_ms) {
    if (!L) return;
    timer_clock_ms_ += dt_ms;

    for (size_t i = 0; i < kMaxTimers; i++) {
        Timer& t = timers_[i];
        if (!t.used) continue;
        if ((int32_t)(timer_clock_ms_ - t.due_ms) < 0) continue;

        const uint32_t gen = t.generation;
        const int ref = t.ref;
        const uint32_t handle = (gen << 8) | (uint32_t)(i + 1);
        bool one_shot_ref_owned = false;

        if (t.repeat) {
            // 遅れても溜めて何回も鳴らさない: 次は「今から」interval後
            t.due_ms += t.interval_ms;
            if ((int32_t)(timer_clock_ms_ - t.due_ms) >= 0) t.due_ms = timer_clock_ms_ + t.interval_ms;
        } else {
            // 呼ぶ間もrefを生かしておくため、スロットだけ先に空ける(refはこちらで外す)
            t.used = false;
            t.ref = LUA_NOREF;
            t.generation++;
            one_shot_ref_owned = true;
        }

        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        lua_pushinteger(L, (lua_Integer)handle);
        const bool failed = (ProtectedCall(1) != LUA_OK);
        if (failed) {
            // 同じエラーが毎回出ないよう、繰り返しのタイマーはここで止める
            if (!one_shot_ref_owned && timers_[i].used && timers_[i].generation == gen) ReleaseTimer(i);
            ReportError("タイマーのコールバックでエラーが発生しました");
        }
        if (one_shot_ref_owned) luaL_unref(L, LUA_REGISTRYINDEX, ref);
    }
}

// ---------------- 画面をまたぐ受け渡し ----------------

void LuaEngine::SetScriptPath(const char* path) {
    script_path_.assign(path ? path : "");
}

void LuaEngine::SetSceneArgs(const char* json, const char* parent_script) {
    scene_args_.assign(json ? json : "");
    parent_path_.assign(parent_script ? parent_script : "");
}

int LuaEngine::l_args(lua_State* L) {
    LuaEngine* self = Self(L);
    if (self->scene_args_.empty()) { lua_pushnil(L); return 1; }
    const char* err = nullptr;
    size_t pos = 0;
    if (!LuaJson::Decode(L, self->scene_args_.c_str(), self->scene_args_.length(), true, err, pos)) {
        lua_pushnil(L);
    }
    return 1;
}

void LuaEngine::CallWithJson(const char* name, const char* json) {
    if (!L || !json || !*json) return;
    lua_getglobal(L, name);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return; }

    // デコード(メモリ不足でlongjmpしうる)は保護された呼び出しの中でやる必要があるため、
    // 小さなC関数に包んで ProtectedCall する
    struct Ctx { LuaEngine* self; const char* json; };
    Ctx ctx{this, json};
    lua_pushcfunction(L, [](lua_State* L2) -> int {
        Ctx* c = (Ctx*)lua_touserdata(L2, 1);
        const char* err = nullptr;
        size_t pos = 0;
        if (!LuaJson::Decode(L2, c->json, strlen(c->json), true, err, pos)) lua_pushnil(L2);
        return 1;
    });
    lua_pushlightuserdata(L, &ctx);
    if (ProtectedCall(1, 1) != LUA_OK) {
        lua_pop(L, 1); // 関数(name)は下に残っているので外す
        lua_pop(L, 1);
        return;
    }
    // スタック: [fn, value]
    if (ProtectedCall(1) != LUA_OK) {
        ReportError("Luaスクリプトの実行時エラー");
    }
}

bool LuaEngine::CallSuspend(FixedString<PICO_STR_2KiB>& out) {
    if (!L) return false;
    out.clear();
    lua_getglobal(L, "on_suspend");
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return false; }

    if (ProtectedCall(0, 1) != LUA_OK) {
        // 画面を離れる途中なのでダイアログは出さずログだけ
        LOG_APP_FAIL("on_suspend()でエラー: %s", lua_tostring(L, -1) ? lua_tostring(L, -1) : "?");
        lua_pop(L, 1);
        return false;
    }
    bool saved = false;
    if (!lua_isnil(L, -1)) {
        const char* err = nullptr;
        std::string json;
        const bool ok = LuaJson::EncodeValue(L, -1, json, 0, err);
        if (!ok) {
            LOG_APP_WARN("on_suspend()の戻り値を保存できません: %s", err ? err : "?");
        } else if (json.size() >= PICO_STR_2KiB) {
            LOG_APP_WARN("on_suspend()の戻り値が大きすぎます(%uバイトまで)", (unsigned)PICO_STR_2KiB - 1);
        } else {
            out.assign(json.c_str());
            saved = true;
        }
    }
    lua_pop(L, 1);
    return saved;
}
