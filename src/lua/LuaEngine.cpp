#include "lua/LuaEngine.hpp"
#include "functions/Power_Functions.hpp"

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
#include "functions/Widget_Functions.hpp"
#include "functions/Error_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/App_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "gui/scenes/Scene.hpp"
#include "gui/scenes/LuaScene.hpp"
#include "storage/SD_IO.hpp"
#include "storage/SD_Path.hpp"
#include "functions/Config_Functions.hpp"
#include "task/Http_Request.hpp"
#include "util/Url.hpp"
#include "functions/Time_Functions.hpp"
#include "functions/Sound_Functions.hpp"
#include "functions/Pad_Functions.hpp"
#include "functions/Battery_Functions.hpp"
#include "functions/Notification_Functions.hpp"
#include "sound/Note_Name.hpp"
#include "sound/Mml_Compiler.hpp"
#include "lua/LuaDebugger.hpp"
#include "lua/LuaJson.hpp"
#include "lua/LuaBuiltinModules.hpp"
#include "util/Secret_Aead.hpp"
#include "functions/Profiler_Functions.hpp"
#include "OS_Data.hpp"
#include "consts.hpp"
#include <Arduino.h>

namespace {
    // WidgetFactory::Create()が実際に生成する特殊化と揃える(WidgetProperty.cppの
    // 同名エイリアスと同じ理由。食い違うと不正なstatic_castになる)
    using TextboxT = Textbox<WidgetFactory::kTextboxCapacity>;

    // WidgetIdは32bitで符号無しだが、Luaのlua_Integerは64bit符号付きなので
    // そのまま行き来させて問題ない(桁が全く足りている)。
    LuaEngine* Self(lua_State* L) {
        return static_cast<LuaEngine*>(lua_touserdata(L, lua_upvalueindex(1)));
    }

    // pico.draw_*が描いた範囲をdirtyにする。pico.image_target()で画像へ描いている間は
    // 画面に関係が無いので何もしない
    void LuaMarkDirty(const Rect& r) {
        if (!LuaOffscreen::active) PICO_GFX::MarkDirty(r);
    }

    // WidgetProperty::Value <-> Luaスタックの変換
    void PushPropertyValue(lua_State* L, const WidgetProperty::Value& v) {
        switch (v.type) {
            case WidgetProperty::Type::Int:   lua_pushinteger(L, v.i); break;
            case WidgetProperty::Type::Float: lua_pushnumber(L, v.f); break;
            case WidgetProperty::Type::Bool:  lua_pushboolean(L, v.b); break;
            case WidgetProperty::Type::Str:   lua_pushstring(L, v.s.c_str()); break;
        }
    }

    // pico.http_request()の送信ボディ/受信本文の上限。pico.sd_read等の
    // kMaxSdReadBytesと同じ考え方(Lua state全体の予算を1回のリクエストで
    // 食い潰さないための頭打ち)。HttpEngine::HttpState越しにしか使わないため
    // LuaEngineのメンバにはせずここへ置く
    constexpr size_t kMaxHttpBodyBytes = PICO_STR_16KiB;
    constexpr size_t kMaxHttpResponseBytes = PICO_STR_16KiB;

    bool HttpMethodFromName(const char* name, HttpRequest::Method& out) {
        if (!name) return false;
        if (strcmp(name, "GET") == 0)    { out = HttpRequest::Method::GET;    return true; }
        if (strcmp(name, "POST") == 0)   { out = HttpRequest::Method::POST;   return true; }
        if (strcmp(name, "PUT") == 0)    { out = HttpRequest::Method::PUT;    return true; }
        if (strcmp(name, "PATCH") == 0)  { out = HttpRequest::Method::PATCH;  return true; }
        if (strcmp(name, "DELETE") == 0) { out = HttpRequest::Method::Delete; return true; }
        return false;
    }

    // pico.http_request()の受信本文の行き先。上限を超える分は書き込みを拒否して
    // 応答全体を失敗させる(pico.sd_readと同じく黙って切り詰めない方針)
    struct LuaHttpSink : IHttpSink {
        FixedString<kMaxHttpResponseBytes> body;
        bool write(const void* data, size_t len) override {
            if (body.length() + len > kMaxHttpResponseBytes) return false;
            return body.append((const char*)data, len);
        }
    };

    // opts.save_to を指定したときの本文の行き先。メモリに溜めずSDの <path>.part へ書き、
    // 最後まで受け取れたときだけ本来の名前へ差し替える(途中で切れた半端なファイルを残さない)
    constexpr size_t kMaxHttpDownloadBytes = 8u * 1024u * 1024u;
    struct LuaFileSink : IHttpSink {
        FsFile file;
        bool is_open = false;
        size_t total = 0;
        bool write(const void* data, size_t len) override {
            if (!is_open) return false;
            if (total + len > kMaxHttpDownloadBytes) return false;
            if (file.write(data, len) != len) return false;
            total += len;
            return true;
        }
        void closeFile() {
            if (is_open) { file.close(); is_open = false; }
        }
    };

    // 足せるリクエストヘッダの合計(HttpRequestの溜め場所512Bに、区切りを含めて収まる大きさ)
    constexpr size_t kMaxHttpHeaderBytes = 480;

    // Luaから足せないヘッダ(接続の仕組みや本文の長さは自分で決める。Hostを変えると別のサーバへ送れてしまう)
    bool HttpHeaderReserved(const char* name) {
        static const char* const kNames[] = {
            "host", "content-length", "connection", "transfer-encoding", "content-type",
            "upgrade", "te", "trailer", "keep-alive", "proxy-authorization", "proxy-connection",
        };
        for (const char* n : kNames) {
            if (strcasecmp(name, n) == 0) return true;
        }
        return false;
    }

    bool HttpHeaderValid(const char* name, const char* value) {
        if (!name || !*name || !value) return false;
        for (const char* p = name; *p; p++) {
            const unsigned char c = (unsigned char)*p;
            const bool tok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                || c == '-' || c == '_' || c == '.';
            if (!tok) return false;
        }
        for (const char* p = value; *p; p++) {
            const unsigned char c = (unsigned char)*p;
            if ((c < 0x20 && c != '\t') || c == 0x7f) return false;
        }
        return true;
    }
}

// pico.http_request()を待たせている間の1本分(走り出す前の控え)。本体は待つ間だけnewする
struct LuaEngine::PendingHttp {
    int id = 0;
    HttpRequest::Method method = HttpRequest::Method::GET;
    Url url;
    std::string body;
    FixedString<PICO_STR_M> content_type;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string save_to;
    int callback_ref = LUA_NOREF;
};

// pico.http_request()用の状態一式。ヘッダでは前方宣言のみにしてポインタで持ち、
// 使わないLuaアプリのメモリコストをゼロに保つ(クラスコメント「ネットワーク」参照)
struct LuaEngine::HttpState {
    HttpRequest request;
    LuaHttpSink sink;
    LuaFileSink file_sink;
    // HttpRequestは送信ボディ/Content-Typeを非所有ポインタで受け取る(IHttpSinkと
    // 同じ約束)ため、Luaスタック上の一時的な文字列をそのまま渡すのではなく、
    // リクエストが終わるまで生きているこのバッファへ一度コピーしてから渡す
    FixedString<kMaxHttpBodyBytes> body_buf;
    FixedString<PICO_STR_M> content_type_buf;
    // 進行中のリクエストが無ければLUA_NOREF。「走っているのは1本だけ」の判定にも使う
    int callback_ref = LUA_NOREF;
    int cur_id = 0;
    int next_id = 1;
    // 走っているリクエストの保存先(空ならメモリで受ける)
    FixedString<PICO_PATH_LEN> save_path;
    FixedString<PICO_PATH_LEN> save_part;
    bool saving() const { return !save_path.empty(); }
};

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

LuaEngine::LuaEngine(size_t budget_bytes, const LuaPermissions& permissions, const char* app_dir)
    : budget_(budget_bytes), permissions_(permissions) {
    if (!PICO_IO::normalize(app_dir_, app_dir)) app_dir_.assign("/");

    L = lua_newstate(Alloc, this);
    if (!L) {
        LOG_APP_FAIL("LuaEngine: lua_newstateに失敗しました(予算%zuB)", budget_bytes);
        return;
    }

    if (LuaDebugger::GlobalEnabled()) debugger_ = new LuaDebugger();

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
    EndImageTarget();
    // 鳴らしっぱなし(長さ0)の音を残したままアプリを閉じると鳴り止まないので、
    // 音を使ったアプリは閉じるときに全部止める
    if (used_sound_) SoundFunctions::StopAll();
    if (used_music_) SoundFunctions::MusicStop();
    if (used_wav_) SoundFunctions::WavStop();
    for (PendingHttp* r : http_queue_) delete r; // 順番待ちの控え(Luaのrefはlua_close()が捨てる)
    http_queue_.clear();
    delete http_; // lua_close()より前でも後でも問題ない(HttpStateはLuaと無関係のC++側の状態)
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
    };

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

void LuaEngine::registerFn(const char* name, lua_CFunction fn) {
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, fn, 1);
    lua_setfield(L, -2, name); // -2 = pushしてあるpicoテーブル
}

void LuaEngine::registerApi() {
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
    registerFn("battery", l_battery);
    registerFn("on_key", l_on_key);
    registerFn("pad_connected", l_pad_connected);
    registerFn("pad_down", l_pad_down);
    registerFn("pad_pressed", l_pad_pressed);
    registerFn("pad_released", l_pad_released);
    registerFn("sound_available", l_sound_available);
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
    RegisterExtApi();
    // グローバルの require は pico.require と同じ関数
    lua_getfield(L, -1, "require");
    lua_setglobal(L, "require");
    lua_setglobal(L, "pico");
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

// ---------------- 画像ハンドル ----------------

uint32_t LuaEngine::MakeImageHandle(size_t index, uint32_t generation) {
    // 下位8bit=index+1(1始まり。0はhandle全体を無効値にするため使わない)、
    // 上位24bit=generation。kMaxLuaImagesは4なので8bitで十分過ぎるほど余裕がある
    return (generation << 8) | static_cast<uint32_t>(index + 1);
}

bool LuaEngine::ResolveImageHandle(uint32_t handle, size_t& out_index) const {
    const uint32_t index1 = handle & 0xFF;
    if (index1 == 0 || index1 > kMaxLuaImages) return false;

    const size_t index = index1 - 1;
    const ImageSlot& slot = images_[index];
    const uint32_t generation = handle >> 8;
    // usedを見ずgenerationだけで判定すると、解放直後(まだ再利用されていない)スロットの
    // 「今のgeneration」と「解放された側のhandleが持つ古いgeneration」がたまたま
    // 一致するケースは無い(Unregister相当で必ず1つ進めるため)が、それとは別に
    // 「そもそも今使用中か」も見ておく方が安全なので両方チェックする
    if (!slot.used || slot.generation == 0 || slot.generation != generation) return false;

    out_index = index;
    return true;
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
    names_.erase(std::remove_if(names_.begin(), names_.end(),
        [id](const NameEntry& e) { return e.id == id; }), names_.end());
    tab_links_.erase(std::remove_if(tab_links_.begin(), tab_links_.end(),
        [id](const TabLink& e) { return e.tab == id || e.target == id; }), tab_links_.end());
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

// ---------------- pico.* 関数本体 ----------------

int LuaEngine::l_create(lua_State* L) {
    const char* type_name = luaL_checkstring(L, 1);

    WidgetType type;
    if (!WidgetFactory::TypeFromName(type_name, type)) {
        return luaL_error(L, "pico.create: 未知のウィジェット種別 '%s'", type_name);
    }

    Widget* w = WidgetFactory::Create(type);
    if (!w) {
        return luaL_error(L, "pico.create: '%s' の生成に失敗しました(メモリ不足の可能性)", type_name);
    }

    WidgetFunctions::Add(w);
    lua_pushinteger(L, (lua_Integer)w->getId());
    return 1;
}

int LuaEngine::l_destroy(lua_State* L) {
    LuaEngine* self = Self(L);
    const WidgetId id = (WidgetId)luaL_checkinteger(L, 1);

    Widget* w = WidgetRegistry::Resolve(id);
    if (!w) return 0; // 既に無効なIDは黙って無視(二重destroyを許容)

    self->PruneCallbacksFor(id);
    WidgetFunctions::DestroyLater(w);
    return 0;
}

int LuaEngine::l_set(lua_State* L) {
    const WidgetId id = (WidgetId)luaL_checkinteger(L, 1);
    const char* name = luaL_checkstring(L, 2);

    Widget* w = WidgetRegistry::Resolve(id);
    if (!w) return luaL_error(L, "pico.set: 無効なID");

    WidgetProperty::Id pid;
    if (!WidgetProperty::IdFromName(name, pid)) {
        return luaL_error(L, "pico.set: 未知のプロパティ '%s'", name);
    }

    // 画像・文書をSDのパスから読み込むプロパティは、pico.image_loadと同じ権限(app_dirの外は
    // sd_outside_app_dirが要る)で縛る。縛らないと、Image/ImageView/MarkdownViewのpathから
    // 権限なしにSDの任意のファイルの中身を(画像や文書として)画面に出せてしまう
    if (pid == WidgetProperty::Id::Path && lua_type(L, 3) == LUA_TSTRING) {
        switch (w->getWidgetType()) {
            case WidgetType::Image:
            case WidgetType::ImageView:
            case WidgetType::MarkdownView: {
                const char* path = lua_tostring(L, 3);
                if (path[0] && !Self(L)->SdPathAllowed(path)) {
                    LOG_APP_WARN("pico.set(path): アプリディレクトリ外へのアクセスは許可されていません: %s", path);
                    return luaL_error(L, "pico.set: アプリのフォルダの外のファイルは読めません(sd_outside_app_dir権限が必要です)");
                }
                break;
            }
            default: break;
        }
    }

    bool ok = false;
    switch (lua_type(L, 3)) {
        case LUA_TBOOLEAN:
            ok = WidgetProperty::Set(w, pid, WidgetProperty::Value::MakeBool(lua_toboolean(L, 3)));
            break;
        case LUA_TSTRING:
            ok = WidgetProperty::Set(w, pid, WidgetProperty::Value::MakeStr(lua_tostring(L, 3)));
            break;
        case LUA_TNUMBER: {
            const lua_Number n = lua_tonumber(L, 3);
            // Lua側は1と1.0を書き分けたがらないことが多いが、WidgetProperty側は
            // プロパティごとに期待する型(Int/Float)が固定なので、整数値に見えるなら
            // まずIntとして試し、駄目ならFloatとして試す
            if (n == (lua_Number)(int32_t)n &&
                WidgetProperty::Set(w, pid, WidgetProperty::Value::MakeInt((int32_t)n))) {
                ok = true;
            } else {
                ok = WidgetProperty::Set(w, pid, WidgetProperty::Value::MakeFloat((float)n));
            }
            break;
        }
        default:
            return luaL_error(L, "pico.set: '%s' に対応していない値の型です", name);
    }

    if (!ok) {
        return luaL_error(L, "pico.set: '%s' へは設定できません(型不一致または非対応)", name);
    }
    return 0;
}

int LuaEngine::l_get(lua_State* L) {
    const WidgetId id = (WidgetId)luaL_checkinteger(L, 1);
    const char* name = luaL_checkstring(L, 2);

    Widget* w = WidgetRegistry::Resolve(id);
    if (!w) return luaL_error(L, "pico.get: 無効なID");

    WidgetProperty::Id pid;
    if (!WidgetProperty::IdFromName(name, pid)) {
        return luaL_error(L, "pico.get: 未知のプロパティ '%s'", name);
    }

    WidgetProperty::Value v;
    if (!WidgetProperty::Get(w, pid, v)) {
        lua_pushnil(L); // 非対応の組み合わせは例外にせずnil(「無ければnil」というLuaの慣習に合わせる)
        return 1;
    }

    PushPropertyValue(L, v);
    return 1;
}

int LuaEngine::l_on(lua_State* L) {
    LuaEngine* self = Self(L);
    const WidgetId id = (WidgetId)luaL_checkinteger(L, 1);
    const char* ev = luaL_checkstring(L, 2);
    luaL_checktype(L, 3, LUA_TFUNCTION);

    Widget* w = WidgetRegistry::Resolve(id);
    if (!w) return luaL_error(L, "pico.on: 無効なID");

    EventKind kind;
    if (!EventKindFromName(ev, kind)) {
        return luaL_error(L, "pico.on: 未知のイベント '%s'", ev);
    }

    if (kind == EventKind::Render && w->getWidgetType() != WidgetType::LuaCanvas) {
        return luaL_error(L, "pico.on: 'render'イベントはCanvas(pico.create(\"Canvas\"))のみ対応");
    }

    if (kind == EventKind::Closed) {
        switch (w->getWidgetType()) {
            case WidgetType::MsgDialog:
            case WidgetType::InputDialog:
            case WidgetType::FileSaveDialog:
            case WidgetType::FileSelectDialog:
            case WidgetType::ColorDialog:
            case WidgetType::PickerDialog:
                break;
            default:
                return luaL_error(L, "pico.on: 'closed'イベントはダイアログ(pico.show_*が返すID)のみ対応");
        }
    }

    // ウィジェット固有イベントは対応する種別以外へ登録できない("render"/"closed"と同じ考え方)
    if (kind == EventKind::CheckedChanged && w->getWidgetType() != WidgetType::Checkbox) {
        return luaL_error(L, "pico.on: 'checked_changed'イベントはCheckboxのみ対応");
    }
    if (kind == EventKind::ValueChanged && w->getWidgetType() != WidgetType::NumberSlider) {
        return luaL_error(L, "pico.on: 'value_changed'イベントはNumberSliderのみ対応");
    }
    if (kind == EventKind::SelectItem && w->getWidgetType() != WidgetType::ScrollList) {
        return luaL_error(L, "pico.on: 'select_item'イベントはScrollListのみ対応");
    }
    if (kind == EventKind::TabChanged && w->getWidgetType() != WidgetType::TabBar) {
        return luaL_error(L, "pico.on: 'tab_changed'イベントはTabBarのみ対応");
    }
    if (kind == EventKind::DropdownChanged && w->getWidgetType() != WidgetType::DropdownMenu) {
        return luaL_error(L, "pico.on: 'dropdown_changed'イベントはDropdownMenuのみ対応");
    }
    if (kind == EventKind::TextChanged && w->getWidgetType() != WidgetType::Textbox) {
        return luaL_error(L, "pico.on: 'text_changed'イベントはTextboxのみ対応");
    }

    {
        const char* why = nullptr;
        if (!self->CheckExtEventTarget(kind, w, &why)) {
            return luaL_error(L, "pico.on: '%s'イベントは%sのみ対応", ev, why ? why : "対応するウィジェット");
        }
    }

    lua_pushvalue(L, 3);
    const int ref = luaL_ref(L, LUA_REGISTRYINDEX);

    self->BindCallback(w, id, kind, ref);
    return 0;
}

int LuaEngine::l_add_child(lua_State* L) {
    const WidgetId container_id = (WidgetId)luaL_checkinteger(L, 1);
    const WidgetId child_id = (WidgetId)luaL_checkinteger(L, 2);

    Widget* container = WidgetRegistry::Resolve(container_id);
    Widget* child = WidgetRegistry::Resolve(child_id);
    if (!container || !child) return luaL_error(L, "pico.add_child: 無効なID");

    // 一旦フラットな管理リスト(WidgetFunctions::widgets)から外し、コンテナのadd()が
    // 立てるneeds_children_updateによる次フレームの再登録で、コンテナの子として
    // 正しい位置(=コンテナより後、つまり上)へ入り直す。
    // これをしないと、生成順(child→containerの順で作った場合)によっては
    // 子がフラットリスト中で親より手前になり、親の描画で覆い隠されてしまう。
    WidgetFunctions::Remove(child);

    bool ok = true;
    switch (container->getWidgetType()) {
        case WidgetType::LayoutContainer:
            static_cast<LayoutContainer*>(container)->add(child);
            break;
        case WidgetType::GridContainer:
            static_cast<GridContainer*>(container)->add(child);
            break;
        case WidgetType::ScrollContainer:
            static_cast<ScrollContainer*>(container)->add(child);
            break;
        default:
            ok = false;
            break;
    }

    if (!ok) {
        // 対応外のコンテナ種別。取り外したままにせず元通り登録し直す
        WidgetFunctions::Add(child);
        return luaL_error(L, "pico.add_child: このウィジェット種別は子を追加できません");
    }
    return 0;
}

// ---------------- コンテナからの取り外し / リストへの項目追加 ----------------
// クラスコメント参照。細部の穴埋め(2026-09-21追加)。

int LuaEngine::l_remove_child(lua_State* L) {
    const WidgetId container_id = (WidgetId)luaL_checkinteger(L, 1);
    const WidgetId child_id = (WidgetId)luaL_checkinteger(L, 2);

    Widget* container = WidgetRegistry::Resolve(container_id);
    Widget* child = WidgetRegistry::Resolve(child_id);
    if (!container || !child) return luaL_error(L, "pico.remove_child: 無効なID");

    switch (container->getWidgetType()) {
        case WidgetType::LayoutContainer:
        case WidgetType::GridContainer:
        case WidgetType::ScrollContainer:
            break;
        default:
            return luaL_error(L, "pico.remove_child: このウィジェット種別から子を取り外せません");
    }

    if (child->getParent() != container) {
        return luaL_error(L, "pico.remove_child: 指定したコンテナの子ではありません");
    }

    // removeChild()を境に親がnullへ変わり、getScreenRect()の基準(コンテナ座標→
    // 画面座標)が変わってしまうので、コンテナに属していた間の画面矩形は
    // 今のうちにdirty化しておく(後からでは同じ場所を指せない)
    PICO_GFX::MarkDirty(child->getScreenRect());

    // Widget::removeChild()は仮想関数なので、この時点で型ごとのoverride
    // (children_からの除去+setParent(nullptr))がそのまま呼ばれる
    container->removeChild(child);

    // pico.add_child()がフラットリスト(WidgetFunctions::widgets)から外した分を
    // ここで元に戻す。取り外した子は次フレームから独立したルートウィジェットとして
    // 描画・当たり判定の対象になる(座標はコンテナ内での相対値のまま残るので、
    // 必要なら呼び出し側がpico.set(id,"x"/"y",...)で置き直すこと)
    WidgetFunctions::Add(child);
    child->needsRender();
    return 0;
}

int LuaEngine::l_list_add(lua_State* L) {
    const WidgetId id = (WidgetId)luaL_checkinteger(L, 1);
    const char* text = luaL_checkstring(L, 2);

    Widget* w = WidgetRegistry::Resolve(id);
    if (!w) return luaL_error(L, "pico.list_add: 無効なID");

    switch (w->getWidgetType()) {
        case WidgetType::ScrollList: {
            ScrollListTools::Item item;
            item.text.assign(text);
            static_cast<ScrollList*>(w)->add(item);
            return 0;
        }
        case WidgetType::DropdownMenu:
            static_cast<DropdownMenu*>(w)->add(text);
            return 0;
        default:
            return luaL_error(L, "pico.list_add: ScrollList/DropdownMenuのみ対応");
    }
}

int LuaEngine::l_list_clear(lua_State* L) {
    const WidgetId id = (WidgetId)luaL_checkinteger(L, 1);

    Widget* w = WidgetRegistry::Resolve(id);
    if (!w) return luaL_error(L, "pico.list_clear: 無効なID");

    switch (w->getWidgetType()) {
        case WidgetType::ScrollList:
            static_cast<ScrollList*>(w)->clear();
            return 0;
        case WidgetType::DropdownMenu:
            static_cast<DropdownMenu*>(w)->clear();
            return 0;
        default:
            return luaL_error(L, "pico.list_clear: ScrollList/DropdownMenuのみ対応");
    }
}

int LuaEngine::l_tab_add(lua_State* L) {
    const WidgetId id = (WidgetId)luaL_checkinteger(L, 1);
    const char* label = luaL_checkstring(L, 2);

    Widget* w = WidgetRegistry::Resolve(id);
    if (!w) return luaL_error(L, "pico.tab_add: 無効なID");
    if (w->getWidgetType() != WidgetType::TabBar) {
        return luaL_error(L, "pico.tab_add: TabBarのみ対応");
    }

    // TabBar::addTab()はkMaxTabs(4)に達しているとfalseを返す。呼び出し側が
    // タブ数の上限を検知できるよう、そのままLuaへ返す
    const bool ok = static_cast<TabBar*>(w)->addTab(label);
    lua_pushboolean(L, ok);
    return 1;
}

int LuaEngine::l_log(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);
    LOG_APP_MSG("%s", msg);
    return 0;
}

int LuaEngine::l_show_error(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);
    ErrorFunctions::ShowFatal(msg);
    return 0;
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

namespace {
    // push_scene/change_scene/pop が画面の間で渡す値(引数・結果)をJSONにする。
    // 渡さない(nil)なら空文字列。1KiBに収まらなければ luaL_error
    void EncodeSceneValue(lua_State* L, int idx, const char* api, FixedString<PICO_STR_1KiB>& out) {
        out.clear();
        if (lua_isnoneornil(L, idx)) return;
        const char* err = nullptr; // 静的文字列
        bool too_big = false;
        {   // luaL_error(longjmp)はデストラクタを飛ばすので、std::stringはここで確実に破棄してから投げる
            std::string json;
            const int base = lua_gettop(L);
            const bool ok = LuaJson::EncodeValue(L, idx, json, 0, err);
            lua_settop(L, base);
            if (ok) {
                if (json.size() >= PICO_STR_1KiB) too_big = true;
                else out.assign(json.c_str());
            } else if (!err) {
                err = "?";
            }
        }
        if (err) luaL_error(L, "%s: 値をJSONにできません(%s)", api, err);
        if (too_big) {
            luaL_error(L, "%s: 渡す値が大きすぎます(%dバイトまで。大きなデータはファイルに置いてください)",
                       api, (int)PICO_STR_1KiB - 1);
        }
    }
}

int LuaEngine::l_pop(lua_State* L) {
    LuaEngine* self = Self(L);
    // pico.pop(result): 呼び出し元(push_sceneした画面)の on_result(result) に渡す値(任意)。
    // 親はこのあとPopで戻ってonEnter()からやり直すので、受け渡しはLuaSceneの待ち箱を介す
    if (!lua_isnoneornil(L, 1)) {
        FixedString<PICO_STR_1KiB> json;
        EncodeSceneValue(L, 1, "pico.pop", json);
        if (!self->parent_path_.empty()) {
            LuaScene::PostResult(self->parent_path_.c_str(), json.c_str());
        }
    }
    // LuaSceneがアプリを起動する際はSceneFunctions::Pushなので、Popでランチャへ戻れる
    // (ClocksScene/CalculatorScene等、他のアプリの「戻る」ボタンと同じ仕組み)。
    // 要求を登録するだけで実際の遷移はフレーム境界(SceneFunctions::Update())まで保留される
    SceneFunctions::Pop();
    return 0;
}

int LuaEngine::l_push_scene(lua_State* L) {
    LuaEngine* self = Self(L);
    const char* path = luaL_checkstring(L, 1);

    // pico.push_scene(path [, args]): argsは遷移先で pico.args() として受け取る(JSONにできる値)
    FixedString<PICO_STR_1KiB> args;
    EncodeSceneValue(L, 2, "pico.push_scene", args);

    // LuaScene(path, permissions)のコンストラクタはFixedStringへコピーするだけなので、
    // ここで即座に構築してよい(SDを開くのはSceneFunctions::Update()経由のonEnter()から)。
    // pico.pop()と同じく要求を登録するだけで、実際の遷移・エラー表示(ファイル不在等)は
    // 次のフレーム境界(LuaScene::onEnter())まで保留される。今のスクリプト(=このLuaEngine)は
    // その時点でonExit()経由で破棄されるので、この呼び出し自体は安全に戻ってこられる。
    //
    // 権限は「スクリプトファイル単位」ではなく「アプリ単位」で決まるものとして、
    // 今のLuaEngineが持つLuaPermissionsをそのまま引き継ぐ(push_scene/change_sceneは
    // 同じアプリの内部で別の画面へ移るためのAPIなので、遷移のたびに権限が既定値
    // (最小権限)へ戻ってしまうと、複数画面のLuaアプリで2画面目以降だけ権限が
    // 落ちるという分かりにくい挙動になる)。app_dir自体は遷移先スクリプト自身の
    // 親ディレクトリから改めて計算し直す(LuaScene::onEnter()側)
    LuaScene* scene = new LuaScene(path, self->permissions_);
    // 子から見た「親」は今のスクリプト(pico.pop(result)の宛先)
    scene->setLaunchArgs(args.c_str(), self->script_path_.c_str());
    SceneFunctions::Push(scene);
    return 0;
}

int LuaEngine::l_change_scene(lua_State* L) {
    LuaEngine* self = Self(L);
    const char* path = luaL_checkstring(L, 1);
    FixedString<PICO_STR_1KiB> args;
    EncodeSceneValue(L, 2, "pico.change_scene", args);
    // push_sceneと違いスタックを消費しない(戻れなくなる)版。l_push_sceneのコメント参照
    // (権限の引き継ぎ方も同じ)。置き換えた先から pop(result) したときの宛先は、
    // 今の画面の「親」をそのまま引き継ぐ(置き換えた画面は消えるため)
    LuaScene* scene = new LuaScene(path, self->permissions_);
    scene->setLaunchArgs(args.c_str(), self->parent_path_.c_str());
    SceneFunctions::Change(scene);
    return 0;
}

int LuaEngine::l_launch_app(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);

    // AppFunctions::Launch()と同じPush経路(C++製アプリ含め登録簿の全アプリへ飛べる)。
    // 名前の綴りミス等その場で判定できる失敗だけbool falseで返す
    // (実際のシーン遷移自体はpico.pop()/push_scene同様フレーム境界まで保留される)
    const bool ok = AppFunctions::LaunchByName(name);
    lua_pushboolean(L, ok);
    return 1;
}

// ---- 通知 ----

void LuaEngine::SetLaunchReason(const char* tag, const char* data) {
    has_launch_reason_ = true;
    launch_tag_.assign(tag ? tag : "");
    launch_data_.assign(data ? data : "");
}

namespace {
    // テーブルの文字列フィールドを読む(無ければfalse。文字列以外はエラー)
    template<size_t N>
    bool OptStringField(lua_State* L, int t, const char* name, FixedString<N>& out) {
        lua_getfield(L, t, name);
        if (lua_isnil(L, -1)) { lua_pop(L, 1); return false; }
        if (!lua_isstring(L, -1)) {
            lua_pop(L, 1);
            luaL_error(L, "pico.notify: %s は文字列で指定してください", name);
            return false;
        }
        NotificationFunctions::Sanitize(out, lua_tostring(L, -1));
        lua_pop(L, 1);
        return true;
    }

    // 数値フィールド(整数)。無ければfalse
    bool OptIntField(lua_State* L, int t, const char* name, lua_Integer& out) {
        lua_getfield(L, t, name);
        if (lua_isnil(L, -1)) { lua_pop(L, 1); return false; }
        int isnum = 0;
        const lua_Integer v = lua_tointegerx(L, -1, &isnum);
        lua_pop(L, 1);
        if (!isnum) luaL_error(L, "pico.notify: %s は整数で指定してください", name);
        out = v;
        return true;
    }

    // at = エポック秒 または {year=, month=, day=, hour=, min=, sec=}(現地時刻)
    int64_t AtFromTable(lua_State* L, int t) {
        struct tm tm_ = {};
        lua_Integer v = 0;
        auto need = [&](const char* name) -> lua_Integer {
            if (!OptIntField(L, t, name, v)) luaL_error(L, "pico.notify: at に %s がありません", name);
            return v;
        };
        tm_.tm_year = (int)need("year") - 1900;
        tm_.tm_mon  = (int)need("month") - 1;
        tm_.tm_mday = (int)need("day");
        tm_.tm_hour = OptIntField(L, t, "hour", v) ? (int)v : 0;
        tm_.tm_min  = OptIntField(L, t, "min", v) ? (int)v : 0;
        tm_.tm_sec  = OptIntField(L, t, "sec", v) ? (int)v : 0;
        tm_.tm_isdst = -1;
        const time_t e = mktime(&tm_);
        if (e == (time_t)-1) luaL_error(L, "pico.notify: at の日時が正しくありません");
        return (int64_t)e;
    }
}

int LuaEngine::l_notify(lua_State* L) {
    using namespace NotificationFunctions;
    LuaEngine* self = Self(L);
    luaL_checktype(L, 1, LUA_TTABLE);

    Content c;
    if (!OptStringField(L, 1, "title", c.title) || c.title.empty()) {
        return luaL_error(L, "pico.notify: title は必須です");
    }
    OptStringField(L, 1, "body", c.body);
    OptStringField(L, 1, "tag", c.tag);
    OptStringField(L, 1, "data", c.data);
    lua_getfield(L, 1, "sound");
    if (!lua_isnil(L, -1)) c.sound = lua_toboolean(L, -1);
    lua_pop(L, 1);

    // いつ出すか(どれか1つ)
    When w;
    int triggers = 0;
    lua_Integer v = 0;
    if (OptIntField(L, 1, "delay_ms", v)) {
        if (v <= 0) return luaL_error(L, "pico.notify: delay_ms は1以上です");
        w.trigger = Trigger::Delay; w.delay_ms = (unsigned long)v; triggers++;
    }
    if (OptIntField(L, 1, "every_ms", v)) {
        if (v < (lua_Integer)kMinEveryMs) {
            return luaL_error(L, "pico.notify: every_ms は%lu以上です", kMinEveryMs);
        }
        w.trigger = Trigger::Every; w.every_ms = (unsigned long)v; triggers++;
    }
    lua_getfield(L, 1, "at");
    if (!lua_isnil(L, -1)) {
        if (lua_istable(L, -1)) {
            w.at_epoch = AtFromTable(L, lua_gettop(L));
        } else if (lua_isinteger(L, -1)) {
            w.at_epoch = (int64_t)lua_tointeger(L, -1);
        } else {
            return luaL_error(L, "pico.notify: at はエポック秒か {year=,month=,day=,hour=,min=} です");
        }
        if (w.at_epoch <= 0) return luaL_error(L, "pico.notify: at の日時が正しくありません");
        w.trigger = Trigger::At; triggers++;
    }
    lua_pop(L, 1);
    lua_getfield(L, 1, "daily");
    if (!lua_isnil(L, -1)) {
        int h = -1, m = -1;
        const char* d = lua_tostring(L, -1);
        if (!d || sscanf(d, "%d:%d", &h, &m) != 2 || h < 0 || h > 23 || m < 0 || m > 59) {
            return luaL_error(L, "pico.notify: daily は \"HH:MM\" です");
        }
        w.trigger = Trigger::Daily; w.hour = (uint8_t)h; w.minute = (uint8_t)m; triggers++;
    }
    lua_pop(L, 1);
    lua_getfield(L, 1, "when");
    if (!lua_isnil(L, -1)) {
        const char* cond = lua_tostring(L, -1);
        if (cond && strcmp(cond, "battery_low") == 0) {
            w.trigger = Trigger::BatteryLow;
            if (OptIntField(L, 1, "below", v)) {
                if (v < 1 || v > 99) return luaL_error(L, "pico.notify: below は1〜99です");
                w.below = (uint8_t)v;
            }
        } else if (cond && strcmp(cond, "wifi_connected") == 0) {
            w.trigger = Trigger::WifiConnected;
        } else if (cond && strcmp(cond, "wifi_disconnected") == 0) {
            w.trigger = Trigger::WifiDisconnected;
        } else {
            return luaL_error(L, "pico.notify: when は battery_low / wifi_connected / wifi_disconnected です");
        }
        triggers++;
    }
    lua_pop(L, 1);
    if (triggers > 1) {
        return luaL_error(L, "pico.notify: delay_ms / at / daily / every_ms / when は1つだけ指定してください");
    }

    // 権限はプログラマの誤り(引数の書き間違い)を先に弾いてから見る。
    // SD無し等と同じ「実行時の状態」枠なのでエラーにはせず nil, 理由 を返す
    if (!self->permissions_.notify) {
        LOG_APP_WARN("pico.notify: 通知の権限がありません(app.cfgのpermission_notify)");
        lua_pushnil(L);
        lua_pushstring(L, "通知の権限がありません");
        return 2;
    }

    c.owner.assign(self->app_dir_.c_str());
    AppFunctions::NameForDir(self->app_dir_.c_str(), c.app);

    uint16_t id = 0;
    const Result r = Schedule(c, w, &id);
    if (r != Result::Ok) {
        lua_pushnil(L);
        lua_pushstring(L, ResultToStr(r));
        return 2;
    }
    lua_pushinteger(L, id);
    return 1;
}

int LuaEngine::l_notify_cancel(lua_State* L) {
    using namespace NotificationFunctions;
    LuaEngine* self = Self(L);
    const char* owner = self->app_dir_.c_str();
    int n = 0;
    if (lua_isnoneornil(L, 1)) {
        n = CancelAll(owner);
    } else if (lua_isinteger(L, 1)) {
        const lua_Integer id = lua_tointeger(L, 1);
        n = (id > 0 && id <= 0xFFFF) ? Cancel(owner, (uint16_t)id) : 0;
    } else if (lua_type(L, 1) == LUA_TSTRING) {
        n = CancelTag(owner, lua_tostring(L, 1));
    } else {
        return luaL_error(L, "pico.notify_cancel: 引数は予約のid(整数)かtag(文字列)です");
    }
    lua_pushinteger(L, n);
    return 1;
}

int LuaEngine::l_notify_list(lua_State* L) {
    using namespace NotificationFunctions;
    LuaEngine* self = Self(L);
    lua_newtable(L);
    int out = 0;
    const int n = RuleCount();
    for (int i = 0; i < n; i++) {
        const Rule* r = RuleAt(i);
        if (!r || !(r->content.owner == self->app_dir_.c_str())) continue;
        lua_newtable(L);
        lua_pushinteger(L, r->id);                         lua_setfield(L, -2, "id");
        lua_pushstring(L, r->content.tag.c_str());         lua_setfield(L, -2, "tag");
        lua_pushstring(L, r->content.title.c_str());       lua_setfield(L, -2, "title");
        lua_pushstring(L, TriggerToStr(r->when.trigger));  lua_setfield(L, -2, "kind");
        lua_rawseti(L, -2, ++out);
    }
    return 1;
}

int LuaEngine::l_launch_reason(lua_State* L) {
    LuaEngine* self = Self(L);
    if (!self->has_launch_reason_) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushstring(L, self->launch_tag_.c_str());
    lua_pushstring(L, self->launch_data_.c_str());
    return 2;
}

int LuaEngine::l_content_rect(lua_State* L) {
    // ステータスバーを除いた、シーンが自由に使える領域。他のC++製アプリと同じ
    // Scene::contentRect()を使うので、Luaアプリだけ位置がずれることはない
    const Rect r = Scene::contentRect();
    lua_pushinteger(L, r.x);
    lua_pushinteger(L, r.y);
    lua_pushinteger(L, r.w);
    lua_pushinteger(L, r.h);
    return 4;
}

int LuaEngine::l_get_time(lua_State* L) {
    // TimeFunctions::timeinfoはmain.cpp起動時のTimeFunctions::Setup()以降、333msごとに
    // 更新される(クラスコメント「時刻取得」参照)。NTP未同期の間の値の妥当性は
    // 呼び出し元(このAPI)では保証しない(ClocksScene等、既存の利用箇所と同じ割り切り)
    const struct tm& t = TimeFunctions::timeinfo;

    lua_newtable(L);
    lua_pushinteger(L, TimeFunctions::year);  lua_setfield(L, -2, "year");
    lua_pushinteger(L, TimeFunctions::month); lua_setfield(L, -2, "month");
    lua_pushinteger(L, t.tm_mday); lua_setfield(L, -2, "day");
    lua_pushinteger(L, t.tm_hour); lua_setfield(L, -2, "hour");
    lua_pushinteger(L, t.tm_min);  lua_setfield(L, -2, "min");
    lua_pushinteger(L, t.tm_sec);  lua_setfield(L, -2, "sec");
    lua_pushinteger(L, t.tm_wday); lua_setfield(L, -2, "wday"); // 0=日曜〜6=土曜(tm_wdayそのまま)
    return 1;
}

int LuaEngine::l_get_touch(lua_State* L) {
    // OSData::touchX/touchYはWidgetFunctions::HitTest()(src/functions/Widget_Functions.cpp)
    // が当たり判定にそのまま使っている絶対スクリーン座標。pico.draw_*やpico.content_rect()
    // と同じ座標系なので、press_start等のコールバック内でそのまま使える。
    // isTouchEnd(離した瞬間)でも座標はリセットされず最後の値を保持したままなので
    // (Touch_Functions*.hppのUpdate()参照)、press_endの中で読んでも問題ない。
    lua_pushinteger(L, OSData::touchX);
    lua_pushinteger(L, OSData::touchY);
    lua_pushboolean(L, OSData::isTouched);
    return 3;
}

// ---- 外部コントローラー ----
// 状態はloop()の先頭で1回だけ更新される(PadFunctions)ので、1フレームの中では何度読んでも同じ答え。
// 名前はPadFunctions::ButtonFromName()の小文字("up" "a" "start" …)。知らない名前はエラー
// (綴りの間違いで「押しても反応しない」と悩まないように)

static uint16_t CheckPadButton(lua_State* L, int arg) {
    const char* name = luaL_checkstring(L, arg);
    const uint16_t b = PadFunctions::ButtonFromName(name);
    if (b == 0) luaL_error(L, "知らないボタン名です: %s", name);
    return b;
}

int LuaEngine::l_pad_connected(lua_State* L) {
    lua_pushboolean(L, PadFunctions::IsConnected());
    return 1;
}

int LuaEngine::l_pad_down(lua_State* L) {
    lua_pushboolean(L, PadFunctions::IsDown(CheckPadButton(L, 1)));
    return 1;
}

int LuaEngine::l_pad_pressed(lua_State* L) {
    lua_pushboolean(L, PadFunctions::Pressed(CheckPadButton(L, 1)));
    return 1;
}

int LuaEngine::l_pad_released(lua_State* L) {
    lua_pushboolean(L, PadFunctions::Released(CheckPadButton(L, 1)));
    return 1;
}

int LuaEngine::l_sound_available(lua_State* L) {
    // アンプが刺さっていて、かつsound.cfgでoffにされていないとき(=実際に音が出るとき)だけtrue。
    // falseでもpico.beep()等は呼んでよい(黙って鳴ったことになる)。音で知らせる代わりに
    // 画面でも知らせたいアプリが見分けるためのもの
    lua_pushboolean(L, SoundFunctions::IsAvailable());
    return 1;
}

int LuaEngine::l_beep(lua_State* L) {
    // チャンネル1で矩形波を鳴らすだけの簡易版(pico.sound_playの省略形)。
    // 長さは10秒で頭打ち(うっかり長い値を渡しても困らないように)
    const lua_Integer freq = luaL_checkinteger(L, 1);
    const lua_Integer ms   = luaL_checkinteger(L, 2);
    const uint16_t f = (uint16_t)std::clamp<lua_Integer>(freq, 0, 20000);
    const uint16_t d = (uint16_t)std::clamp<lua_Integer>(ms, 0, 10000);
    Self(L)->used_sound_ = true;
    SoundFunctions::Beep(f, d);
    return 0;
}

namespace {
    // pico.sound_play の wave に書ける名前(ChipSynth::Waveの並びと同じ順)
    const char* const kWaveNames[] = {
        "pulse12", "pulse25", "pulse50", "pulse75", "triangle", "saw", "noise", "noise_short",
    };
    static_assert(sizeof(kWaveNames) / sizeof(kWaveNames[0]) == (size_t)ChipSynth::Wave::kCount,
                  "kWaveNamesをChipSynth::Waveと揃えること");

    // Luaのチャンネル番号(1始まり)→ 0始まり。範囲外はエラー
    uint8_t CheckChannel(lua_State* L, int arg){
        const lua_Integer ch = luaL_checkinteger(L, arg);
        if (ch < 1 || ch > SoundFunctions::kChannels) {
            luaL_error(L, "チャンネルは1〜%dです(%d)", SoundFunctions::kChannels, (int)ch);
        }
        return (uint8_t)(ch - 1);
    }
}

int LuaEngine::l_sound_play(lua_State* L) {
    // pico.sound_play(ch, freq, ms [, {wave=, volume=, envelope=}]) -> bool
    const uint8_t ch = CheckChannel(L, 1);
    const lua_Number freq = luaL_checknumber(L, 2);
    const lua_Integer ms = luaL_checkinteger(L, 3);

    ChipSynth::Note note;
    note.freq_x16 = (freq <= 0) ? 0 : (uint32_t)std::min<lua_Number>(freq * 16.0 + 0.5, 1e9);
    //長さは1分で頭打ち。0は「止めるまで鳴らし続ける」
    note.length_ms = (uint32_t)std::clamp<lua_Integer>(ms, 0, 60000);

    if (!lua_isnoneornil(L, 4)) {
        luaL_checktype(L, 4, LUA_TTABLE);

        lua_getfield(L, 4, "wave");
        if (!lua_isnil(L, -1)) {
            const char* name = luaL_checkstring(L, -1);
            bool found = false;
            for (size_t i = 0; i < (size_t)ChipSynth::Wave::kCount; i++) {
                if (strcmp(name, kWaveNames[i]) == 0) {
                    note.wave = (ChipSynth::Wave)i;
                    found = true;
                    break;
                }
            }
            if (!found) return luaL_error(L, "pico.sound_play: 不明な波形です(%s)", name);
        }
        lua_pop(L, 1);

        lua_getfield(L, 4, "volume");
        if (!lua_isnil(L, -1)) note.volume = (uint8_t)std::clamp<lua_Integer>(luaL_checkinteger(L, -1), 0, 15);
        lua_pop(L, 1);

        lua_getfield(L, 4, "envelope");
        if (!lua_isnil(L, -1)) note.envelope = (int8_t)std::clamp<lua_Integer>(luaL_checkinteger(L, -1), -7, 7);
        lua_pop(L, 1);
    }

    //周波数0は「止める」と同じ扱い(休符を書きやすいように)
    Self(L)->used_sound_ = true;
    if (note.freq_x16 == 0) {
        SoundFunctions::Stop(ch);
        lua_pushboolean(L, 1);
        return 1;
    }
    lua_pushboolean(L, SoundFunctions::Play(ch, note));
    return 1;
}

int LuaEngine::l_sound_stop(lua_State* L) {
    // pico.sound_stop([ch]) chを省略すると全部
    if (lua_isnoneornil(L, 1)) SoundFunctions::StopAll();
    else SoundFunctions::Stop(CheckChannel(L, 1));
    return 0;
}

int LuaEngine::l_sound_playing(lua_State* L) {
    // pico.sound_playing([ch]) -> bool。chを省略するとどれか1つでも
    if (lua_isnoneornil(L, 1)) {
        lua_pushboolean(L, SoundFunctions::IsPlaying());
    } else {
        const uint8_t ch = CheckChannel(L, 1);
        lua_pushboolean(L, (SoundFunctions::ActiveChannels() >> ch) & 1);
    }
    return 1;
}

namespace {
    // 曲の読み込み結果をLuaへ返す: 成功なら true、失敗なら nil, "3行12列: 理由"
    int PushMusicResult(lua_State* L, bool ok, const MmlResult& r) {
        if (ok) {
            lua_pushboolean(L, 1);
            return 1;
        }
        lua_pushnil(L);
        if (r.line > 0) lua_pushfstring(L, "%d行%d列: %s", r.line, r.col, r.message.c_str());
        else lua_pushstring(L, r.message.c_str());
        return 2;
    }
}

int LuaEngine::l_music_play(lua_State* L) {
    // pico.music_play(path) -> true | nil, 理由
    LuaEngine* self = Self(L);
    const char* path = luaL_checkstring(L, 1);
    MmlResult r;
    if (!OSData::SD_usable) {
        r.message.assign("SDカードが使えません");
        return PushMusicResult(L, false, r);
    }
    if (!self->SdPathAllowed(path)) {
        LOG_APP_WARN("pico.music_play: アプリディレクトリ外へのアクセスは許可されていません: %s", path);
        r.message.assign("このアプリからは読めない場所です");
        return PushMusicResult(L, false, r);
    }
    self->used_music_ = true;
    const bool ok = SoundFunctions::MusicPlayFile(path, &r);
    return PushMusicResult(L, ok, r);
}

int LuaEngine::l_music_play_text(lua_State* L) {
    // pico.music_play_text(mml) -> true | nil, 理由
    size_t len = 0;
    const char* text = luaL_checklstring(L, 1, &len);
    Self(L)->used_music_ = true;
    MmlResult r;
    const bool ok = SoundFunctions::MusicPlayText(text, len, &r);
    return PushMusicResult(L, ok, r);
}

int LuaEngine::l_music_stop(lua_State* L) {
    (void)L;
    SoundFunctions::MusicStop();
    return 0;
}

int LuaEngine::l_music_playing(lua_State* L) {
    lua_pushboolean(L, SoundFunctions::MusicPlaying());
    return 1;
}

int LuaEngine::l_wav_play(lua_State* L) {
    // pico.wav_play(path[, {loop=bool, volume=0〜100}]) -> true | nil, 理由
    LuaEngine* self = Self(L);
    const char* path = luaL_checkstring(L, 1);
    bool loop = false;
    uint8_t volume = 100;
    //引数の誤りは状態(SD無し・権限)より先にエラーにする
    if (!lua_isnoneornil(L, 2)) {
        luaL_checktype(L, 2, LUA_TTABLE);
        lua_getfield(L, 2, "loop");
        if (!lua_isnil(L, -1)) {
            luaL_checktype(L, -1, LUA_TBOOLEAN);
            loop = lua_toboolean(L, -1);
        }
        lua_pop(L, 1);
        lua_getfield(L, 2, "volume");
        if (!lua_isnil(L, -1)) volume = (uint8_t)std::clamp<lua_Integer>(luaL_checkinteger(L, -1), 0, 100);
        lua_pop(L, 1);
    }

    if (!OSData::SD_usable) {
        lua_pushnil(L);
        lua_pushstring(L, "SDカードが使えません");
        return 2;
    }
    if (!self->SdPathAllowed(path)) {
        LOG_APP_WARN("pico.wav_play: アプリディレクトリ外へのアクセスは許可されていません: %s", path);
        lua_pushnil(L);
        lua_pushstring(L, "このアプリからは読めない場所です");
        return 2;
    }
    self->used_wav_ = true;
    const char* err = "";
    if (!SoundFunctions::WavPlay(path, loop, volume, &err)) {
        lua_pushnil(L);
        lua_pushstring(L, err);
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

int LuaEngine::l_wav_stop(lua_State* L) {
    (void)L;
    SoundFunctions::WavStop();
    return 0;
}

int LuaEngine::l_wav_playing(lua_State* L) {
    lua_pushboolean(L, SoundFunctions::WavPlaying());
    return 1;
}

int LuaEngine::l_note_freq(lua_State* L) {
    // pico.note_freq("C4" | 60) -> number | nil
    int note = -1;
    if (lua_type(L, 1) == LUA_TNUMBER) {
        if (!lua_isinteger(L, 1)) return luaL_error(L, "pico.note_freq: ノート番号は整数です");
        note = (int)std::clamp<lua_Integer>(lua_tointeger(L, 1), -1, 128);
    } else {
        note = NoteName::Parse(luaL_checkstring(L, 1));
    }
    const float f = NoteName::MidiToFreq(note);
    if (f <= 0.0f) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushnumber(L, f);
    return 1;
}

int LuaEngine::l_invalidate(lua_State* L) {
    const WidgetId id = (WidgetId)luaL_checkinteger(L, 1);

    Widget* w = WidgetRegistry::Resolve(id);
    if (!w) return luaL_error(L, "pico.invalidate: 無効なID");

    // needsRender()はそのウィジェットの画面矩形をPICO_GFX::MarkDirty()し、
    // 次のFlushDirty()でrenderForce()(=LuaCanvasならrenderコールバック)が
    // 呼ばれるようにする。LuaCanvas以外の任意のウィジェットにも使える汎用API
    w->needsRender();
    return 0;
}

int LuaEngine::l_mark_dirty(lua_State* L) {
    const int16_t x = (int16_t)luaL_checkinteger(L, 1);
    const int16_t y = (int16_t)luaL_checkinteger(L, 2);
    const int16_t w = (int16_t)luaL_checkinteger(L, 3);
    const int16_t h = (int16_t)luaL_checkinteger(L, 4);

    // PICO_GFX::MarkDirty()の生の下請け。ウィジェットを介さず任意の矩形を
    // 直接dirty化したい場合向けの低レベルAPI(クラスコメント「直接描画」参照)
    PICO_GFX::MarkDirty({x, y, w, h});
    return 0;
}

// ---------------- 直接描画 ----------------
// クラスコメント「直接描画」参照。ウィジェットを介さずOSData::frameへ直接描き、
// 描いた範囲だけPICO_GFX::MarkDirty()する(既存の各種render()実装と同じ流儀)。
// 色は既存プロパティと同じくPICO 4bitパレット番号をそのままint8_tへキャストするだけで、
// 範囲チェックはしない(WidgetProperty::Setの色プロパティと同じ)。

int LuaEngine::l_draw_pixel(lua_State* L) {
    const int16_t x = (int16_t)luaL_checkinteger(L, 1);
    const int16_t y = (int16_t)luaL_checkinteger(L, 2);
    const int8_t color = (int8_t)luaL_checkinteger(L, 3);

    OSData::frame->drawPixel(x, y, color);
    LuaMarkDirty({x, y, 1, 1});
    return 0;
}

int LuaEngine::l_draw_line(lua_State* L) {
    const int16_t x0 = (int16_t)luaL_checkinteger(L, 1);
    const int16_t y0 = (int16_t)luaL_checkinteger(L, 2);
    const int16_t x1 = (int16_t)luaL_checkinteger(L, 3);
    const int16_t y1 = (int16_t)luaL_checkinteger(L, 4);
    const int8_t color = (int8_t)luaL_checkinteger(L, 5);
    // 6番目の引数: 線の太さ(px、既定1)。2以上は両端の円+胴体の三角形で塗る
    // (drawWideLine()は4bppパレットでアンチエイリアスのブレンドに入りクラッシュするため使わない)
    const int width = (int)std::clamp<lua_Integer>(luaL_optinteger(L, 6, 1), 1, 64);

    if (width <= 1) {
        OSData::frame->drawLine(x0, y0, x1, y1, color);
        LuaMarkDirty({
            (int16_t)std::min(x0, x1), (int16_t)std::min(y0, y1),
            (int16_t)(std::abs(x1 - x0) + 1), (int16_t)(std::abs(y1 - y0) + 1)
        });
        return 0;
    }
    const float radius = width * 0.5f;
    CanvasRaster::DrawThickLine(OSData::frame, x0, y0, x1, y1, radius, color);
    const int pad = width / 2 + 1;
    LuaMarkDirty({
        (int16_t)(std::min(x0, x1) - pad), (int16_t)(std::min(y0, y1) - pad),
        (int16_t)(std::abs(x1 - x0) + 1 + pad * 2), (int16_t)(std::abs(y1 - y0) + 1 + pad * 2)
    });
    return 0;
}

int LuaEngine::l_draw_rect(lua_State* L) {
    const int16_t x = (int16_t)luaL_checkinteger(L, 1);
    const int16_t y = (int16_t)luaL_checkinteger(L, 2);
    const int16_t w = (int16_t)luaL_checkinteger(L, 3);
    const int16_t h = (int16_t)luaL_checkinteger(L, 4);
    const int8_t color = (int8_t)luaL_checkinteger(L, 5);

    OSData::frame->drawRect(x, y, w, h, color);
    LuaMarkDirty({x, y, w, h});
    return 0;
}

int LuaEngine::l_fill_rect(lua_State* L) {
    const int16_t x = (int16_t)luaL_checkinteger(L, 1);
    const int16_t y = (int16_t)luaL_checkinteger(L, 2);
    const int16_t w = (int16_t)luaL_checkinteger(L, 3);
    const int16_t h = (int16_t)luaL_checkinteger(L, 4);
    const int8_t color = (int8_t)luaL_checkinteger(L, 5);

    OSData::frame->fillRect(x, y, w, h, color);
    LuaMarkDirty({x, y, w, h});
    return 0;
}

int LuaEngine::l_draw_circle(lua_State* L) {
    const int16_t x = (int16_t)luaL_checkinteger(L, 1);
    const int16_t y = (int16_t)luaL_checkinteger(L, 2);
    const int16_t r = (int16_t)luaL_checkinteger(L, 3);
    const int8_t color = (int8_t)luaL_checkinteger(L, 4);

    OSData::frame->drawCircle(x, y, r, color);
    LuaMarkDirty({(int16_t)(x - r), (int16_t)(y - r), (int16_t)(r * 2 + 1), (int16_t)(r * 2 + 1)});
    return 0;
}

int LuaEngine::l_fill_circle(lua_State* L) {
    const int16_t x = (int16_t)luaL_checkinteger(L, 1);
    const int16_t y = (int16_t)luaL_checkinteger(L, 2);
    const int16_t r = (int16_t)luaL_checkinteger(L, 3);
    const int8_t color = (int8_t)luaL_checkinteger(L, 4);

    OSData::frame->fillCircle(x, y, r, color);
    LuaMarkDirty({(int16_t)(x - r), (int16_t)(y - r), (int16_t)(r * 2 + 1), (int16_t)(r * 2 + 1)});
    return 0;
}

int LuaEngine::l_clear_rect(lua_State* L) {
    const int16_t x = (int16_t)luaL_checkinteger(L, 1);
    const int16_t y = (int16_t)luaL_checkinteger(L, 2);
    const int16_t w = (int16_t)luaL_checkinteger(L, 3);
    const int16_t h = (int16_t)luaL_checkinteger(L, 4);
    const int8_t color = (int8_t)luaL_optinteger(L, 5, PICO_BACKGROUND);

    OSData::frame->fillRect(x, y, w, h, color);
    LuaMarkDirty({x, y, w, h});
    return 0;
}

int LuaEngine::l_draw_text(lua_State* L) {
    const int16_t x = (int16_t)luaL_checkinteger(L, 1);
    const int16_t y = (int16_t)luaL_checkinteger(L, 2);
    const char* text = luaL_checkstring(L, 3);
    const int8_t color = (int8_t)luaL_optinteger(L, 4, PICO_FORECOLOR);
    const FontFn::FontSize size = (FontFn::FontSize)luaL_optinteger(L, 5, (lua_Integer)FontFn::Normal);
    // 6番目: 揃え。"left"(既定)はxが左端、"center"はxが中心、"right"はxが右端
    const char* align = luaL_optstring(L, 6, "left");

    int16_t draw_x = x;
    if (strcmp(align, "left") != 0) {
        const int tw = Label<PICO_STR_M>::GetTextWidth(size, text);
        if (strcmp(align, "center") == 0) draw_x = (int16_t)(x - tw / 2);
        else if (strcmp(align, "right") == 0) draw_x = (int16_t)(x - tw);
        else return luaL_error(L, "pico.draw_text: align は left / center / right です");
    }

    // 右端をはみ出さないよう、幅は残りスクリーン幅に自動で収める(AppGrid::drawName()等と
    // 同じ理由でmaxWidth=0以下はDrawPlain側がクリップ無しとして扱ってしまうため先に弾く)
    const int16_t max_w = (int16_t)(SCREEN_WIDTH - draw_x);
    if (max_w <= 0) return 0;

    Label<PICO_STR_M>::DrawPlain(size, color, draw_x, y, max_w, text);

    const int16_t line_h = (int16_t)Label<PICO_STR_M>::GetLineHeight(size);
    LuaMarkDirty({draw_x, y, max_w, line_h});
    return 0;
}

int LuaEngine::l_draw_image(lua_State* L) {
    LuaEngine* self = Self(L);
    const uint32_t handle = (uint32_t)luaL_checkinteger(L, 1);
    const int16_t x = (int16_t)luaL_checkinteger(L, 2);
    const int16_t y = (int16_t)luaL_checkinteger(L, 3);

    size_t index;
    if (!self->ResolveImageHandle(handle, index)) {
        return luaL_error(L, "pico.draw_image: 無効なイメージハンドル");
    }

    ImageSlot& slot = self->images_[index];
    IconRender::DrawPimgSprite(slot.sprite, x, y);
    LuaMarkDirty({x, y, (int16_t)slot.sprite.width, (int16_t)slot.sprite.height});
    return 0;
}

// 画像の一部(sx,sy,w,h)だけを(x,y)へ描く。スプライトシート(同じ大きさのタイルを並べた
// 1枚の画像)から1枚ずつ取り出して描くためのもの。画像は4枚までしか持てないので、
// 部品の多い絵(テトリスのミノ等)は1枚にまとめて読み、これで切り出す。
// 実装は「今のクリップ(FlushDirty()のdirty矩形)と描き先の矩形の重なり」へクリップを
// 一時的に狭めてから画像全体をずらしてpushSprite()するだけ(描かれるのは重なりの中だけ)。
// クリップは元へ戻すので、renderコールバックの中で何回呼んでもdirty矩形の外へははみ出さない
int LuaEngine::l_draw_image_part(lua_State* L) {
    LuaEngine* self = Self(L);
    const uint32_t handle = (uint32_t)luaL_checkinteger(L, 1);
    const int32_t x = (int32_t)luaL_checkinteger(L, 2);
    const int32_t y = (int32_t)luaL_checkinteger(L, 3);
    int32_t sx = (int32_t)luaL_checkinteger(L, 4);
    int32_t sy = (int32_t)luaL_checkinteger(L, 5);
    int32_t w = (int32_t)luaL_checkinteger(L, 6);
    int32_t h = (int32_t)luaL_checkinteger(L, 7);

    size_t index;
    if (!self->ResolveImageHandle(handle, index)) {
        return luaL_error(L, "pico.draw_image_part: 無効なイメージハンドル");
    }
    ImageSlot& slot = self->images_[index];

    // 画像の外を指す分は削る(負のsx/syは描き先を右/下へずらして吸収する)
    int32_t dx = x, dy = y;
    if (sx < 0) { w += sx; dx -= sx; sx = 0; }
    if (sy < 0) { h += sy; dy -= sy; sy = 0; }
    if (sx + w > slot.sprite.width) w = slot.sprite.width - sx;
    if (sy + h > slot.sprite.height) h = slot.sprite.height - sy;
    if (w <= 0 || h <= 0) return 0;

    int32_t cx = 0, cy = 0, cw = 0, ch = 0;
    OSData::frame->getClipRect(&cx, &cy, &cw, &ch);
    const Rect clip = Rect{ (int16_t)cx, (int16_t)cy, (int16_t)cw, (int16_t)ch }
        .intersection({ (int16_t)dx, (int16_t)dy, (int16_t)w, (int16_t)h });
    if (clip.w > 0 && clip.h > 0) {
        OSData::frame->setClipRect(clip.x, clip.y, clip.w, clip.h);
        IconRender::DrawPimgSprite(slot.sprite, dx - sx, dy - sy);
        OSData::frame->setClipRect(cx, cy, cw, ch);
    }
    LuaMarkDirty({ (int16_t)dx, (int16_t)dy, (int16_t)w, (int16_t)h });
    return 0;
}

// ---------------- 拡張API(Love2Dとの比較で足したもの) ----------------
// 図形(楕円・三角形・多角形・円弧)、画像の拡大縮小/回転/反転、文字幅、WAVの一時停止/シーク、
// バッテリー・経過時間、ファイルの情報/部分読み、物理キーボード。
// 描画は他のpico.draw_*と同じく`Canvas`のrenderコールバック内で使う。色は4bitパレット番号。

namespace {
    constexpr int kMaxPolyPoints = 32;

    // 描いた範囲(両端を含む座標)をdirtyにする
    void MarkBounds(int minx, int miny, int maxx, int maxy) {
        LuaMarkDirty({(int16_t)minx, (int16_t)miny,
                             (int16_t)(maxx - minx + 1), (int16_t)(maxy - miny + 1)});
    }

    // {x1,y1,x2,y2,...}の平らな配列から点列を読む。点が3つ未満/上限超過/数値以外はエラー
    int ReadPoints(lua_State* L, int idx, int* xs, int* ys, const char* api) {
        luaL_checktype(L, idx, LUA_TTABLE);
        const lua_Integer len = (lua_Integer)lua_rawlen(L, idx);
        if (len % 2 != 0 || len < 6) luaL_error(L, "%s: 点は{x1,y1,x2,y2,x3,y3,...}の形で3つ以上必要です", api);
        if (len / 2 > kMaxPolyPoints) luaL_error(L, "%s: 点は%d個までです", api, kMaxPolyPoints);
        for (lua_Integer i = 0; i < len; i++) {
            lua_rawgeti(L, idx, i + 1);
            if (!lua_isnumber(L, -1)) luaL_error(L, "%s: 点の座標は数値です", api);
            const int v = (int)lua_tonumber(L, -1);
            lua_pop(L, 1);
            if (i % 2 == 0) xs[i / 2] = v; else ys[i / 2] = v;
        }
        return (int)(len / 2);
    }

    void Bounds(const int* xs, const int* ys, int n, int& minx, int& miny, int& maxx, int& maxy) {
        minx = maxx = xs[0];
        miny = maxy = ys[0];
        for (int i = 1; i < n; i++) {
            minx = std::min(minx, xs[i]); maxx = std::max(maxx, xs[i]);
            miny = std::min(miny, ys[i]); maxy = std::max(maxy, ys[i]);
        }
    }

    void DrawPolyOutline(const int* xs, const int* ys, int n, int8_t color, int width) {
        for (int i = 0; i < n; i++) {
            const int j = (i + 1) % n;
            if (width > 1) CanvasRaster::DrawThickLine(OSData::frame, xs[i], ys[i], xs[j], ys[j], width * 0.5f, color);
            else OSData::frame->drawLine(xs[i], ys[i], xs[j], ys[j], color);
        }
    }

    // 偶奇規則のスキャンライン塗り(凹多角形も塗れる)。ピクセルの中心(x+0.5,y+0.5)が内側なら塗る
    void FillPoly(const int* xs, const int* ys, int n, int8_t color) {
        int minx, miny, maxx, maxy;
        Bounds(xs, ys, n, minx, miny, maxx, maxy);
        const int y_from = std::max(miny, 0);
        const int y_to = std::min(maxy, (int)SCREEN_HEIGHT - 1);
        float cross[kMaxPolyPoints];
        for (int y = y_from; y <= y_to; y++) {
            const float cy = y + 0.5f;
            int cnt = 0;
            for (int i = 0; i < n; i++) {
                const int j = (i + 1) % n;
                const float ya = (float)ys[i], yb = (float)ys[j];
                if ((ya <= cy && cy < yb) || (yb <= cy && cy < ya)) {
                    const float t = (cy - ya) / (yb - ya);
                    cross[cnt++] = xs[i] + t * (xs[j] - xs[i]);
                }
            }
            for (int a = 1; a < cnt; a++) { // 挿入ソート(高々32個)
                const float v = cross[a];
                int b = a - 1;
                while (b >= 0 && cross[b] > v) { cross[b + 1] = cross[b]; b--; }
                cross[b + 1] = v;
            }
            for (int a = 0; a + 1 < cnt; a += 2) {
                const int xl = (int)std::ceil(cross[a] - 0.5f);
                const int xr = (int)std::ceil(cross[a + 1] - 0.5f) - 1;
                if (xr >= xl) OSData::frame->drawFastHLine(xl, y, xr - xl + 1, color);
            }
        }
    }

    // 円弧の点列(中心x,y・半径r・角度a0〜a1ラジアン。0=右、増えると時計回り(y下向き))
    int ArcPoints(float cx, float cy, float r, float a0, float a1, int max_points, int* xs, int* ys) {
        int n = (int)(std::fabs(a1 - a0) * r / 3.0f) + 1;
        n = std::clamp(n, 2, max_points - 1);
        for (int i = 0; i <= n; i++) {
            const float a = a0 + (a1 - a0) * i / n;
            xs[i] = (int)std::lround(cx + r * std::cos(a));
            ys[i] = (int)std::lround(cy + r * std::sin(a));
        }
        return n + 1;
    }
}

int LuaEngine::l_draw_ellipse(lua_State* L) {
    const int x = (int)luaL_checkinteger(L, 1), y = (int)luaL_checkinteger(L, 2);
    const int rx = (int)luaL_checkinteger(L, 3), ry = (int)luaL_checkinteger(L, 4);
    const int8_t color = (int8_t)luaL_checkinteger(L, 5);
    if (rx < 0 || ry < 0) return luaL_error(L, "pico.draw_ellipse: 半径は0以上です");
    OSData::frame->drawEllipse(x, y, rx, ry, color);
    MarkBounds(x - rx, y - ry, x + rx, y + ry);
    return 0;
}

int LuaEngine::l_fill_ellipse(lua_State* L) {
    const int x = (int)luaL_checkinteger(L, 1), y = (int)luaL_checkinteger(L, 2);
    const int rx = (int)luaL_checkinteger(L, 3), ry = (int)luaL_checkinteger(L, 4);
    const int8_t color = (int8_t)luaL_checkinteger(L, 5);
    if (rx < 0 || ry < 0) return luaL_error(L, "pico.fill_ellipse: 半径は0以上です");
    OSData::frame->fillEllipse(x, y, rx, ry, color);
    MarkBounds(x - rx, y - ry, x + rx, y + ry);
    return 0;
}

int LuaEngine::l_draw_triangle(lua_State* L) {
    int xs[3], ys[3];
    for (int i = 0; i < 3; i++) {
        xs[i] = (int)luaL_checkinteger(L, 1 + i * 2);
        ys[i] = (int)luaL_checkinteger(L, 2 + i * 2);
    }
    const int8_t color = (int8_t)luaL_checkinteger(L, 7);
    const int width = (int)std::clamp<lua_Integer>(luaL_optinteger(L, 8, 1), 1, 64);
    DrawPolyOutline(xs, ys, 3, color, width);
    int minx, miny, maxx, maxy;
    Bounds(xs, ys, 3, minx, miny, maxx, maxy);
    const int pad = width > 1 ? width / 2 + 1 : 0;
    MarkBounds(minx - pad, miny - pad, maxx + pad, maxy + pad);
    return 0;
}

int LuaEngine::l_fill_triangle(lua_State* L) {
    int xs[3], ys[3];
    for (int i = 0; i < 3; i++) {
        xs[i] = (int)luaL_checkinteger(L, 1 + i * 2);
        ys[i] = (int)luaL_checkinteger(L, 2 + i * 2);
    }
    const int8_t color = (int8_t)luaL_checkinteger(L, 7);
    OSData::frame->fillTriangle(xs[0], ys[0], xs[1], ys[1], xs[2], ys[2], color);
    int minx, miny, maxx, maxy;
    Bounds(xs, ys, 3, minx, miny, maxx, maxy);
    MarkBounds(minx, miny, maxx, maxy);
    return 0;
}

int LuaEngine::l_draw_polygon(lua_State* L) {
    int xs[kMaxPolyPoints], ys[kMaxPolyPoints];
    const int n = ReadPoints(L, 1, xs, ys, "pico.draw_polygon");
    const int8_t color = (int8_t)luaL_checkinteger(L, 2);
    const int width = (int)std::clamp<lua_Integer>(luaL_optinteger(L, 3, 1), 1, 64);
    DrawPolyOutline(xs, ys, n, color, width);
    int minx, miny, maxx, maxy;
    Bounds(xs, ys, n, minx, miny, maxx, maxy);
    const int pad = width > 1 ? width / 2 + 1 : 0;
    MarkBounds(minx - pad, miny - pad, maxx + pad, maxy + pad);
    return 0;
}

int LuaEngine::l_fill_polygon(lua_State* L) {
    int xs[kMaxPolyPoints], ys[kMaxPolyPoints];
    const int n = ReadPoints(L, 1, xs, ys, "pico.fill_polygon");
    const int8_t color = (int8_t)luaL_checkinteger(L, 2);
    FillPoly(xs, ys, n, color);
    int minx, miny, maxx, maxy;
    Bounds(xs, ys, n, minx, miny, maxx, maxy);
    MarkBounds(minx, miny, maxx, maxy);
    return 0;
}

int LuaEngine::l_draw_arc(lua_State* L) {
    const int x = (int)luaL_checkinteger(L, 1), y = (int)luaL_checkinteger(L, 2);
    const int r = (int)luaL_checkinteger(L, 3);
    const float a0 = (float)luaL_checknumber(L, 4), a1 = (float)luaL_checknumber(L, 5);
    const int8_t color = (int8_t)luaL_checkinteger(L, 6);
    const int width = (int)std::clamp<lua_Integer>(luaL_optinteger(L, 7, 1), 1, 64);
    if (r < 0) return luaL_error(L, "pico.draw_arc: 半径は0以上です");
    int xs[kMaxPolyPoints * 2], ys[kMaxPolyPoints * 2];
    const int n = ArcPoints((float)x, (float)y, (float)r, a0, a1, kMaxPolyPoints * 2, xs, ys);
    for (int i = 0; i + 1 < n; i++) {
        if (width > 1) CanvasRaster::DrawThickLine(OSData::frame, xs[i], ys[i], xs[i + 1], ys[i + 1], width * 0.5f, color);
        else OSData::frame->drawLine(xs[i], ys[i], xs[i + 1], ys[i + 1], color);
    }
    const int pad = r + (width > 1 ? width / 2 + 1 : 0); // 円全体を覆う(範囲の計算を簡単にするため)
    MarkBounds(x - pad, y - pad, x + pad, y + pad);
    return 0;
}

// 扇形(パイ)。中心と円弧の点で作る多角形を塗る
int LuaEngine::l_fill_arc(lua_State* L) {
    const int x = (int)luaL_checkinteger(L, 1), y = (int)luaL_checkinteger(L, 2);
    const int r = (int)luaL_checkinteger(L, 3);
    const float a0 = (float)luaL_checknumber(L, 4), a1 = (float)luaL_checknumber(L, 5);
    const int8_t color = (int8_t)luaL_checkinteger(L, 6);
    if (r < 0) return luaL_error(L, "pico.fill_arc: 半径は0以上です");
    int xs[kMaxPolyPoints], ys[kMaxPolyPoints];
    xs[0] = x; ys[0] = y;
    const int n = ArcPoints((float)x, (float)y, (float)r, a0, a1, kMaxPolyPoints - 1, xs + 1, ys + 1);
    FillPoly(xs, ys, n + 1, color);
    MarkBounds(x - r, y - r, x + r, y + r);
    return 0;
}

int LuaEngine::l_text_width(lua_State* L) {
    const char* text = luaL_checkstring(L, 1);
    const FontFn::FontSize size = (FontFn::FontSize)luaL_optinteger(L, 2, (lua_Integer)FontFn::Normal);
    lua_pushinteger(L, Label<PICO_STR_M>::GetTextWidth(size, text));
    return 1;
}

// pico.draw_image_ex(handle, x, y [, r [, sx [, sy [, ox [, oy]]]]])
// Love2Dのlove.graphics.draw(image, x, y, r, sx, sy, ox, oy)と同じ並び:
// 画像の(ox,oy)を(x,y)に置き、rラジアン(時計回り)回して(sx,sy)倍に拡大縮小する。
// sxが負なら左右反転、syが負なら上下反転(syを省くとsxと同じ)。最近傍で、補間はしない。
// 描き先ごとに元の画素を引く逆変換なので隙間ができない。描くのは今のクリップの内側だけ
int LuaEngine::l_draw_image_ex(lua_State* L) {
    LuaEngine* self = Self(L);
    const uint32_t handle = (uint32_t)luaL_checkinteger(L, 1);
    const double x = luaL_checknumber(L, 2);
    const double y = luaL_checknumber(L, 3);
    const double r = luaL_optnumber(L, 4, 0.0);
    const double sx = luaL_optnumber(L, 5, 1.0);
    const double sy = luaL_optnumber(L, 6, sx);
    const double ox = luaL_optnumber(L, 7, 0.0);
    const double oy = luaL_optnumber(L, 8, 0.0);

    size_t index;
    if (!self->ResolveImageHandle(handle, index)) {
        return luaL_error(L, "pico.draw_image_ex: 無効なイメージハンドル");
    }
    if (std::fabs(sx) < 1e-6 || std::fabs(sy) < 1e-6) return 0;
    if (std::fabs(sx) > 64 || std::fabs(sy) > 64) return luaL_error(L, "pico.draw_image_ex: 倍率は64倍までです");

    ImageSlot& slot = self->images_[index];
    const int iw = slot.sprite.width, ih = slot.sprite.height;
    const double c = std::cos(r), s = std::sin(r);

    // 元画像の4隅を描き先へ写して外接矩形を出す
    double minx = 1e9, miny = 1e9, maxx = -1e9, maxy = -1e9;
    const double cx[4] = {0, (double)iw, 0, (double)iw};
    const double cy[4] = {0, 0, (double)ih, (double)ih};
    for (int i = 0; i < 4; i++) {
        const double ux = (cx[i] - ox) * sx, uy = (cy[i] - oy) * sy;
        const double dx = x + c * ux - s * uy;
        const double dy = y + s * ux + c * uy;
        minx = std::min(minx, dx); maxx = std::max(maxx, dx);
        miny = std::min(miny, dy); maxy = std::max(maxy, dy);
    }
    int32_t kx = 0, ky = 0, kw = 0, kh = 0;
    OSData::frame->getClipRect(&kx, &ky, &kw, &kh);
    const Rect area = Rect{(int16_t)kx, (int16_t)ky, (int16_t)kw, (int16_t)kh}
        .intersection({0, 0, (int16_t)SCREEN_WIDTH, (int16_t)SCREEN_HEIGHT});
    const int x0 = std::max((int)std::floor(minx), (int)area.x);
    const int y0 = std::max((int)std::floor(miny), (int)area.y);
    const int x1 = std::min((int)std::ceil(maxx), (int)area.x + area.w);
    const int y1 = std::min((int)std::ceil(maxy), (int)area.y + area.h);

    // 実機では1画素ごとのdoubleの掛け算・割り算・floorが支配的だったので、
    // 逆変換を16.16の固定小数点に直し、1行の中は加算だけで進める(行の頭だけdoubleで出す)。
    // 画素の読み書きも「同じ元画素が続くあいだは読み直さない」「同じ色が続けば横線1本にまとめる」
    constexpr double kFix = 65536.0;
    const double dux = c / sx, dvx = -s / sy;   // 描き先を右へ1画素進めたときの元画像上の動き
    const int64_t stepU = (int64_t)std::llround(dux * kFix);
    const int64_t stepV = (int64_t)std::llround(dvx * kFix);
    const int64_t fw = (int64_t)iw << 16, fh = (int64_t)ih << 16;
    const bool transparent = slot.sprite.transparent;
    LGFX_Sprite& src = slot.sprite.sprite;

    for (int py = y0; py < y1; py++) {
        const double rx0 = x0 + 0.5 - x, ry = py + 0.5 - y;
        int64_t u = (int64_t)std::llround(((c * rx0 + s * ry) / sx + ox) * kFix);
        int64_t v = (int64_t)std::llround(((-s * rx0 + c * ry) / sy + oy) * kFix);

        int run_x = 0, run_len = 0, run_col = -1;
        int last_ix = -1, last_iy = -1;
        uint32_t last_col = 0;
        for (int px = x0; px < x1; px++, u += stepU, v += stepV) {
            int col = -1;  // -1は描かない
            if (u >= 0 && v >= 0 && u < fw && v < fh) {
                const int ix = (int)(u >> 16), iy = (int)(v >> 16);
                if (ix != last_ix || iy != last_iy) {
                    last_col = src.readPixelValue(ix, iy);
                    last_ix = ix; last_iy = iy;
                }
                if (!(transparent && last_col == 0)) col = (int)last_col; // index0は透過(DrawPimgSprite()と同じ)
            }
            if (run_len > 0 && col == run_col) { run_len++; continue; }
            if (run_len > 0) OSData::frame->drawFastHLine(run_x, py, run_len, run_col);
            if (col >= 0) { run_x = px; run_len = 1; run_col = col; }
            else run_len = 0;
        }
        if (run_len > 0) OSData::frame->drawFastHLine(run_x, py, run_len, run_col);
    }
    if (x1 > x0 && y1 > y0) MarkBounds((int)std::floor(minx), (int)std::floor(miny), (int)std::ceil(maxx), (int)std::ceil(maxy));
    return 0;
}

// ---- WAVの一時停止・位置・シーク(SoundFunctionsにあったものをLuaへ出しただけ) ----

int LuaEngine::l_wav_pause(lua_State* L) {
    // pico.wav_pause([paused=true]) -> 鳴らしているWAVがあればtrue
    const bool pause = lua_isnoneornil(L, 1) ? true : (lua_toboolean(L, 1) != 0);
    lua_pushboolean(L, SoundFunctions::WavPause(pause));
    return 1;
}

int LuaEngine::l_wav_paused(lua_State* L) {
    lua_pushboolean(L, SoundFunctions::WavPaused());
    return 1;
}

int LuaEngine::l_wav_position(lua_State* L) {
    lua_pushinteger(L, (lua_Integer)SoundFunctions::WavPositionMs());
    return 1;
}

int LuaEngine::l_wav_duration(lua_State* L) {
    lua_pushinteger(L, (lua_Integer)SoundFunctions::WavDurationMs());
    return 1;
}

int LuaEngine::l_wav_seek(lua_State* L) {
    const lua_Integer ms = luaL_checkinteger(L, 1);
    lua_pushboolean(L, ms >= 0 && SoundFunctions::WavSeekMs((uint32_t)ms));
    return 1;
}

// ---- システム ----

int LuaEngine::l_millis(lua_State* L) {
    // 起動からのミリ秒(単調増加。NTPの同期で飛ばない)。経過時間の計測用
    lua_pushinteger(L, (lua_Integer)millis());
    return 1;
}

int LuaEngine::l_battery(lua_State* L) {
    // pico.battery() -> 残量%(0〜100), 電圧(V), USB給電中か。まだ読めていなければnil
    if (!BatteryFunctions::HasSample()) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, BatteryFunctions::GetPercent());
    lua_pushnumber(L, BatteryFunctions::GetVoltage());
    lua_pushboolean(L, BatteryFunctions::IsExternallyPowered());
    return 3;
}

// pico.on_key(fn)で物理キーボードの打鍵を受け取る(nilで解除)。
// fn(key, mods): keyは文字(UTF-8。Shiftやキー配列は反映済み)か、特殊キーの名前
// ("enter" "backspace" "tab" "escape" "delete" "left" "right" "up" "down" "home" "end"
// "pageup" "pagedown")。modsは{ctrl=,alt=,shift=}。
// 真を返すと「取った」扱い。偽/nilを返すと開いているキーボードの入力へ回る
int LuaEngine::l_on_key(lua_State* L) {
    LuaEngine* self = Self(L);
    if (!lua_isnoneornil(L, 1)) luaL_checktype(L, 1, LUA_TFUNCTION);
    if (self->key_callback_ref_ != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, self->key_callback_ref_);
        self->key_callback_ref_ = LUA_NOREF;
    }
    if (!lua_isnoneornil(L, 1)) {
        lua_pushvalue(L, 1);
        self->key_callback_ref_ = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    return 0;
}

// ---- ファイルの情報・部分読み ----

int LuaEngine::l_sd_stat(lua_State* L) {
    // pico.sd_stat(path) -> {size=バイト数, is_dir=bool} | nil
    LuaEngine* self = Self(L);
    const char* path = luaL_checkstring(L, 1);
    if (!OSData::SD_usable) { lua_pushnil(L); return 1; }
    if (!self->SdPathAllowed(path)) {
        LOG_APP_WARN("pico.sd_stat: アプリディレクトリ外へのアクセスは許可されていません: %s", path);
        lua_pushnil(L);
        return 1;
    }
    if (!OSData::SD.exists(path)) { lua_pushnil(L); return 1; }
    FsFile f = OSData::SD.open(path, O_RDONLY);
    if (!f) { lua_pushnil(L); return 1; }
    const bool is_dir = f.isDir();
    const size_t size = is_dir ? 0 : f.fileSize();
    f.close();
    lua_createtable(L, 0, 2);
    lua_pushinteger(L, (lua_Integer)size);
    lua_setfield(L, -2, "size");
    lua_pushboolean(L, is_dir);
    lua_setfield(L, -2, "is_dir");
    return 1;
}

int LuaEngine::l_sd_read_part(lua_State* L) {
    // pico.sd_read_part(path, offset, length) -> string | nil
    // offsetバイト目からlengthバイトまで(sd_readと同じ上限)。ファイルの終わりを越えたら短く返る。
    // 大きなファイルを少しずつ読む/途中だけ読むためのもの
    LuaEngine* self = Self(L);
    const char* path = luaL_checkstring(L, 1);
    const lua_Integer offset = luaL_checkinteger(L, 2);
    const lua_Integer length = luaL_checkinteger(L, 3);
    if (offset < 0 || length < 0) return luaL_error(L, "pico.sd_read_part: offsetとlengthは0以上です");
    if (!OSData::SD_usable) { lua_pushnil(L); return 1; }
    if (!self->SdPathAllowed(path)) {
        LOG_APP_WARN("pico.sd_read_part: アプリディレクトリ外へのアクセスは許可されていません: %s", path);
        lua_pushnil(L);
        return 1;
    }
    FsFile f = OSData::SD.open(path, O_RDONLY);
    if (!f || f.isDir()) { if (f) f.close(); lua_pushnil(L); return 1; }

    const size_t file_size = f.fileSize();
    size_t want = (size_t)std::min<lua_Integer>(length, (lua_Integer)kMaxSdReadBytes);
    if ((size_t)offset >= file_size) want = 0;
    else want = std::min(want, file_size - (size_t)offset);
    if (want > 0 && !f.seek((uint32_t)offset)) { f.close(); lua_pushnil(L); return 1; }

    luaL_Buffer b;
    luaL_buffinit(L, &b);
    char chunk[256];
    size_t remaining = want;
    bool ok = true;
    while (remaining > 0) {
        const size_t n = std::min(remaining, sizeof(chunk));
        const int got = f.read((uint8_t*)chunk, n);
        if (got <= 0) { ok = false; break; }
        luaL_addlstring(&b, chunk, (size_t)got);
        remaining -= (size_t)got;
    }
    f.close();
    if (!ok) { lua_pushnil(L); return 1; }
    luaL_pushresult(&b);
    return 1;
}

// ---------------- 直接描画エリア ----------------
// クラスコメント(ヘッダ)参照。OSData::frameのクリップ矩形を差し替えるだけの薄いラッパー。

int LuaEngine::l_set_draw_area(lua_State* L) {
    const int32_t x = (int32_t)luaL_checkinteger(L, 1);
    const int32_t y = (int32_t)luaL_checkinteger(L, 2);
    const int32_t w = (int32_t)luaL_checkinteger(L, 3);
    const int32_t h = (int32_t)luaL_checkinteger(L, 4);

    OSData::frame->setClipRect(x, y, w, h);
    return 0;
}

int LuaEngine::l_clear_draw_area(lua_State*) {
    OSData::frame->clearClipRect();
    return 0;
}

// 今のクリップ矩形を返す。Canvasのrenderコールバックの中では、FlushDirty()が
// 「そのCanvasとdirty矩形の重なり」を設定しているので、そこだけ描けば足りる
// (盤面のように部品の多い絵で、変わったところだけを描き直すためのもの)
int LuaEngine::l_get_draw_area(lua_State* L) {
    int32_t x = 0, y = 0, w = 0, h = 0;
    OSData::frame->getClipRect(&x, &y, &w, &h);
    lua_pushinteger(L, x);
    lua_pushinteger(L, y);
    lua_pushinteger(L, w);
    lua_pushinteger(L, h);
    return 4;
}

// ---------------- 画像 ----------------
// ヘッダのクラスコメント「画像」参照。`.pimg`のデコードそのものは
// IconRender::LoadPimgToSprite()(Imageウィジェットのonram=trueと同じ経路)を
// そのまま使い、このLuaEngineインスタンスの固定長スロットで持つだけ。

int LuaEngine::l_image_load(lua_State* L) {
    LuaEngine* self = Self(L);
    const char* path = luaL_checkstring(L, 1);

    if (!OSData::SD_usable) { lua_pushnil(L); return 1; }
    if (!self->SdPathAllowed(path)) {
        LOG_APP_WARN("pico.image_load: アプリディレクトリ外へのアクセスは許可されていません: %s", path);
        lua_pushnil(L);
        return 1;
    }

    size_t index = kMaxLuaImages;
    for (size_t i = 0; i < kMaxLuaImages; ++i) {
        if (!self->images_[i].used) { index = i; break; }
    }
    if (index == kMaxLuaImages) {
        LOG_APP_WARN("pico.image_load: 同時に保持できる画像数の上限(%zu枚)に達しています: %s",
            kMaxLuaImages, path);
        lua_pushnil(L);
        return 1;
    }

    FsFile f = OSData::SD.open(path, O_RDONLY);
    if (!f) { lua_pushnil(L); return 1; }

    IconRender::PimgHeader header;
    if (!IconRender::ReadPimgHeader(f, header)) {
        f.close();
        lua_pushnil(L);
        return 1;
    }

    // 4bpp(1ピクセル半バイト)なので端数切り上げでバイト数を見積もる。
    // LGFX_Sprite側の実際の確保量は多少前後し得るが、予算チェックとしては十分な精度
    const size_t need_bytes = (static_cast<size_t>(header.width) * header.height + 1) / 2;
    if (self->image_bytes_used_ + need_bytes > kMaxLuaImageBytes) {
        f.close();
        LOG_APP_WARN("pico.image_load: %s の読み込みで画像用メモリの上限(%uB)を超えます",
            path, (unsigned)kMaxLuaImageBytes);
        lua_pushnil(L);
        return 1;
    }

    ImageSlot& slot = self->images_[index];
    const bool ok = IconRender::LoadPimgToSprite(f, slot.sprite);
    f.close();
    if (!ok) { lua_pushnil(L); return 1; }

    slot.used = true;
    slot.bytes = need_bytes;
    self->image_bytes_used_ += need_bytes;

    // generation 0 は「一度も使われていないスロット」の予約値なので、初回使用時だけ
    // 1へ進める。2回目以降はimage_free()側で既に進めてあるのでそのまま使う
    // (WidgetRegistry::Register()と同じ考え方)
    if (slot.generation == 0) slot.generation = 1;

    lua_pushinteger(L, (lua_Integer)MakeImageHandle(index, slot.generation));
    return 1;
}

int LuaEngine::l_image_size(lua_State* L) {
    LuaEngine* self = Self(L);
    const uint32_t handle = (uint32_t)luaL_checkinteger(L, 1);

    size_t index;
    if (!self->ResolveImageHandle(handle, index)) {
        return luaL_error(L, "pico.image_size: 無効なイメージハンドル");
    }

    const ImageSlot& slot = self->images_[index];
    lua_pushinteger(L, slot.sprite.width);
    lua_pushinteger(L, slot.sprite.height);
    return 2;
}

int LuaEngine::l_image_free(lua_State* L) {
    LuaEngine* self = Self(L);
    const uint32_t handle = (uint32_t)luaL_checkinteger(L, 1);

    size_t index;
    // 既に無効なハンドル(未割り当て/解放済み)はpico.destroyと同じく黙って無視し、
    // 二重解放をエラーにしない
    if (!self->ResolveImageHandle(handle, index)) return 0;

    if ((int)index == self->image_target_index_) self->EndImageTarget();
    ImageSlot& slot = self->images_[index];
    slot.sprite.sprite.deleteSprite();
    slot.sprite.usable = false;
    self->image_bytes_used_ -= slot.bytes;
    slot.bytes = 0;
    slot.used = false;

    // WidgetRegistry::Unregister()と同じく、ここでgenerationを進めておく
    // (次にこのスロットが再利用されたとき、解放済みの古いハンドルが新しい画像を
    // 指してしまわないようにするため。generationを0へ戻すだけだと「初回使用」と
    // 区別できず同じハンドル値を再発行してしまう)
    slot.generation++;
    if (slot.generation == 0) slot.generation = 1; // 0は予約値なのでwrapしたら1へ飛ばす
    return 0;
}

// ---------------- ラスタキャンバス(CanvasRaster) ----------------
// ヘッダのクラスコメント「ラスタキャンバスの保存/読み込み」参照。

namespace {
    // 3関数共通: idを解決し、CanvasRaster以外ならluaL_error。
    // 呼び出し側は戻り値nullptrをチェックする必要は無い(エラーはここで飛ぶ)
    CanvasRaster* ResolveCanvasRasterOrError(lua_State* L, int arg_index, const char* fn_name) {
        const WidgetId id = (WidgetId)luaL_checkinteger(L, arg_index);
        Widget* w = WidgetRegistry::Resolve(id);
        if (!w) {
            luaL_error(L, "%s: 無効なID", fn_name);
            return nullptr; // 到達しない(luaL_errorはlongjmpする)
        }
        if (w->getWidgetType() != WidgetType::CanvasRaster) {
            luaL_error(L, "%s: CanvasRaster以外には使えません", fn_name);
            return nullptr;
        }
        return static_cast<CanvasRaster*>(w);
    }

    // 実機の上限に関わらず「画面に収まらないサイズを.pimgから復元して確保する」
    // 事故を防ぐための上限(SCREEN_WIDTH/HEIGHT基準)。不正/悪意あるファイルが
    // 巨大なwidth/heightを名乗っていても、ここで弾けばcreateSprite()の
    // 大量確保まで進まない
    bool CanvasSizeSane(uint16_t w, uint16_t h) {
        return w > 0 && h > 0 && w <= SCREEN_WIDTH && h <= SCREEN_HEIGHT;
    }
}

int LuaEngine::l_canvas_clear(lua_State* L) {
    CanvasRaster* cr = ResolveCanvasRasterOrError(L, 1, "pico.canvas_clear");
    cr->canvasClear();
    return 0;
}

int LuaEngine::l_canvas_save(lua_State* L) {
    LuaEngine* self = Self(L);
    CanvasRaster* cr = ResolveCanvasRasterOrError(L, 1, "pico.canvas_save");
    const char* path = luaL_checkstring(L, 2);

    if (!OSData::SD_usable) { lua_pushboolean(L, false); return 1; }
    if (!self->SdWriteAllowed(path, "pico.canvas_save")) {
        lua_pushboolean(L, false);
        return 1;
    }

    FsFile f = OSData::SD.open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (!f) { lua_pushboolean(L, false); return 1; }

    const bool ok = IconRender::EncodePimg(*cr->getSprite(), (uint16_t)cr->getW(), (uint16_t)cr->getH(), f);
    f.close();
    lua_pushboolean(L, ok);
    return 1;
}

int LuaEngine::l_canvas_load(lua_State* L) {
    LuaEngine* self = Self(L);
    CanvasRaster* cr = ResolveCanvasRasterOrError(L, 1, "pico.canvas_load");
    const char* path = luaL_checkstring(L, 2);
    const bool keep_size = lua_toboolean(L, 3);

    if (!OSData::SD_usable) { lua_pushboolean(L, false); return 1; }
    if (!self->SdPathAllowed(path)) {
        LOG_APP_WARN("pico.canvas_load: アプリディレクトリ外へのアクセスは許可されていません: %s", path);
        lua_pushboolean(L, false);
        return 1;
    }

    FsFile f = OSData::SD.open(path, O_RDONLY);
    if (!f) { lua_pushboolean(L, false); return 1; }

    IconRender::PimgHeader header;
    if (!IconRender::ReadPimgHeader(f, header) || !CanvasSizeSane(header.width, header.height)) {
        f.close();
        lua_pushboolean(L, false);
        return 1;
    }

    if (keep_size) {
        // 大きさは変えず、白紙にしてから左上に合わせて読む。DecodePimgBody()は
        // 画像の幅で行を折り返し、スプライトの外へ出た画素はwritePixel()のクリップで
        // 捨てられるので、大きい画像は右/下が切れ、小さい画像は余白が白で残る
        cr->saveUndoPoint();
        cr->getSprite()->clear(PICO_WHITE);
    } else {
        // 保存時と現在のw/hが食い違っていても読み込めるよう、先にキャンバス自体を
        // 画像のサイズへ合わせる(CanvasRaster::resize()。この時点で旧内容は消える)
        cr->resize((int16_t)header.width, (int16_t)header.height);
    }

    const bool ok = IconRender::DecodePimgBody(f, *cr->getSprite(), header.width, header.height);
    f.close();
    if (ok) cr->needsRender();
    lua_pushboolean(L, ok);
    return 1;
}

int LuaEngine::l_canvas_undo(lua_State* L) {
    CanvasRaster* cr = ResolveCanvasRasterOrError(L, 1, "pico.canvas_undo");
    lua_pushboolean(L, cr->undo());
    return 1;
}

// ---------------- SDカードアクセス ----------------
// ヘッダのクラスコメント参照。OSData::SD_usable==falseの間はどれも失敗(false/nil)を
// 返すだけでluaL_errorにはしない。app_dir_の外を指すパスも同じ扱い(SdPathAllowed()参照。
// プログラマの書き間違いだけでなく、悪意あるスクリプトが試す経路でもあるため
// luaL_errorで詳細を返さず、SD無し等と同じ「実行時の状態」枠にまとめてある)。

bool LuaEngine::SdPathAllowed(const char* path) const {
    if (permissions_.sd_outside_app_dir) return true;

    FixedString<PICO_PATH_LEN> normalized;
    if (!PICO_IO::normalize(normalized, path)) return false;

    const size_t dir_len = app_dir_.length();
    // app_dir_=="/"(既定値。LuaScene以外がapp_dirを指定せずLuaEngineを直接使う場合)は
    // 「制限なし」に相当する
    if (dir_len <= 1) return true;

    const char* p = normalized.c_str();
    const char* dir = app_dir_.c_str();
    if (strncmp(p, dir, dir_len) != 0) return false;
    // "/lua/foo"は"/lua"の配下だが、"/luaxxx"のような別ディレクトリを誤って配下と
    // 判定しないよう、続きがパス終端か'/'であることまで確認する
    return p[dir_len] == '\0' || p[dir_len] == '/';
}

namespace {
    // SdWriteAllowed()で比べるための形へ直す。normalized(PICO_IO::normalize()済み)の
    // 各セグメントについて、FATと同じく先頭の空白・末尾の空白と'.'を捨て、ASCIIを小文字にする
    // ("/Lua/Apps/X/APP.CFG." も "/lua/apps/x/app.cfg" と同じファイルを指すため)。
    // 捨てると空になるセグメントは元のまま残す(そういう名前はFATでは開けないので害は無い)
    bool CanonicalizeSdPath(FixedString<PICO_PATH_LEN>& out, const char* normalized) {
        char buf[PICO_PATH_LEN];
        size_t n = 0;
        const char* p = normalized;
        while (*p) {
            while (*p == '/') p++;
            if (!*p) break;
            const char* start = p;
            while (*p && *p != '/') p++;
            const char* b = start;
            const char* e = p;
            while (b < e && *b == ' ') b++;
            while (e > b && (e[-1] == ' ' || e[-1] == '.')) e--;
            if (b == e) { b = start; e = p; }
            if (n + 1 + (size_t)(e - b) >= sizeof(buf)) return false;
            buf[n++] = '/';
            for (const char* q = b; q < e; ++q) {
                char c = *q;
                if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
                buf[n++] = c;
            }
        }
        if (n == 0) buf[n++] = '/';
        buf[n] = '\0';
        return out.assign(buf);
    }

    // pathがdir自身か、その祖先か("/"は全ての祖先)
    bool IsAncestorOrSelf(const char* path, const char* dir) {
        if (strcmp(path, "/") == 0) return true;
        const size_t len = strlen(path);
        return strncmp(dir, path, len) == 0 && (dir[len] == '\0' || dir[len] == '/');
    }

    // "/lua/apps/<名前>"(LuaAppScannerが1アプリとして見るディレクトリ)か
    bool IsScannedAppDir(const char* path) {
        const char* apps = PICO_Path::DIR::LUA_APPS; // "/lua/apps/"
        const size_t n = strlen(apps);
        if (strncmp(path, apps, n) != 0) return false;
        const char* rest = path + n;
        return rest[0] != '\0' && strchr(rest, '/') == nullptr;
    }

    constexpr const char* kAppConfigName = "app.cfg"; // LuaAppScannerが読む名前(小文字で比べる)
}

bool LuaEngine::SdWriteAllowed(const char* path, const char* api, bool is_remove) const {
    if (!SdPathAllowed(path)) {
        LOG_APP_WARN("%s: アプリディレクトリ外へのアクセスは許可されていません: %s", api, path);
        return false;
    }

    FixedString<PICO_PATH_LEN> normalized;
    FixedString<PICO_PATH_LEN> target;
    FixedString<PICO_PATH_LEN> own_dir;
    if (!PICO_IO::normalize(normalized, path) || !CanonicalizeSdPath(target, normalized.c_str()) ||
        !CanonicalizeSdPath(own_dir, app_dir_.c_str())) {
        LOG_APP_WARN("%s: パスを解釈できません: %s", api, path);
        return false;
    }

    const char* t = target.c_str();
    const char* slash = strrchr(t, '/');
    FixedString<PICO_PATH_LEN> parent;
    if (slash == t) parent.assign("/");
    else parent.assign(t, (size_t)(slash - t));

    bool is_protected = false;
    if (strcmp(slash + 1, kAppConfigName) == 0) {
        // 自分のapp.cfg、またはスキャン対象のアプリのapp.cfg(sd_outside_app_dirを持つアプリが
        // 他のアプリ、あるいは新しく作ったアプリのapp.cfgで権限を足すのも防ぐ)
        is_protected = strcmp(parent.c_str(), own_dir.c_str()) == 0 || IsScannedAppDir(parent.c_str());
    }
    if (!is_protected && is_remove) {
        // app.cfgを含むディレクトリごと消すのも同じ扱い
        is_protected = IsAncestorOrSelf(t, own_dir.c_str()) || IsScannedAppDir(t) ||
                       IsAncestorOrSelf(t, "/lua/apps");
    }
    if (is_protected) {
        LOG_APP_WARN("%s: アプリの設定ファイル(app.cfg)は書き換えられません: %s", api, path);
        return false;
    }
    return true;
}

int LuaEngine::l_sd_exists(lua_State* L) {
    LuaEngine* self = Self(L);
    const char* path = luaL_checkstring(L, 1);
    lua_pushboolean(L, OSData::SD_usable && self->SdPathAllowed(path) && OSData::SD.exists(path));
    return 1;
}

int LuaEngine::l_sd_read(lua_State* L) {
    LuaEngine* self = Self(L);
    const char* path = luaL_checkstring(L, 1);
    if (!OSData::SD_usable) { lua_pushnil(L); return 1; }
    if (!self->SdPathAllowed(path)) {
        LOG_APP_WARN("pico.sd_read: アプリディレクトリ外へのアクセスは許可されていません: %s", path);
        lua_pushnil(L);
        return 1;
    }

    FsFile f = OSData::SD.open(path, O_RDONLY);
    if (!f) { lua_pushnil(L); return 1; }

    const size_t file_size = f.fileSize();
    if (file_size > kMaxSdReadBytes) {
        f.close();
        LOG_APP_WARN("pico.sd_read: %s が上限(%uB)を超えています(%uB)",
            path, (unsigned)kMaxSdReadBytes, (unsigned)file_size);
        lua_pushnil(L);
        return 1;
    }

    // MarkdownView::load()/LuaScene::loadAndRun()と同じく、ファイル全体ぶんの
    // 一時バッファをヒープへ一度に確保せず、スタック上の小さなチャンクで読み進める
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    char chunk[256];
    size_t remaining = file_size;
    bool ok = true;
    while (remaining > 0) {
        const size_t want = (remaining < sizeof(chunk)) ? remaining : sizeof(chunk);
        const int got = f.read((uint8_t*)chunk, want);
        if (got <= 0) { ok = false; break; } // 読み取り失敗。読めたところまでで打ち切る
        luaL_addlstring(&b, chunk, (size_t)got);
        remaining -= (size_t)got;
    }
    f.close();

    if (!ok) {
        // luaL_Bufferへ積んだ分は使わず捨てる(luaL_pushresultしないままリターンして良い。
        // Luaのスタック上のuserdataはGCが回収する)
        lua_pushnil(L);
        return 1;
    }

    luaL_pushresult(&b);
    return 1;
}

int LuaEngine::l_sd_write(lua_State* L) {
    LuaEngine* self = Self(L);
    const char* path = luaL_checkstring(L, 1);
    size_t len = 0;
    const char* data = luaL_checklstring(L, 2, &len);
    const bool append = lua_toboolean(L, 3);

    if (!OSData::SD_usable) { lua_pushboolean(L, false); return 1; }
    if (!self->SdWriteAllowed(path, "pico.sd_write")) {
        lua_pushboolean(L, false);
        return 1;
    }

    FsFile f = OSData::SD.open(path, O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC));
    if (!f) { lua_pushboolean(L, false); return 1; }

    const bool ok = (len == 0) || (f.write(data, len) == len);
    f.close();
    lua_pushboolean(L, ok);
    return 1;
}

int LuaEngine::l_sd_remove(lua_State* L) {
    LuaEngine* self = Self(L);
    const char* path = luaL_checkstring(L, 1);
    if (!OSData::SD_usable) { lua_pushboolean(L, false); return 1; }
    if (!self->SdWriteAllowed(path, "pico.sd_remove", true)) {
        lua_pushboolean(L, false);
        return 1;
    }

    // FileExplorer::on_press_delete()と同じ判断(ディレクトリなら再帰削除)
    FsFile f = OSData::SD.open(path);
    bool ok;
    if (!f) {
        ok = false;
    } else if (f.isDir()) {
        f.close();
        ok = PICO_IO::removeRecursive(path);
    } else {
        f.close();
        ok = OSData::SD.remove(path);
    }
    lua_pushboolean(L, ok);
    return 1;
}

int LuaEngine::l_sd_mkdir(lua_State* L) {
    LuaEngine* self = Self(L);
    const char* path = luaL_checkstring(L, 1);
    if (!OSData::SD_usable) { lua_pushboolean(L, false); return 1; }
    if (!self->SdWriteAllowed(path, "pico.sd_mkdir")) {
        lua_pushboolean(L, false);
        return 1;
    }

    lua_pushboolean(L, OSData::SD.mkdir(path));
    return 1;
}

int LuaEngine::l_sd_list(lua_State* L) {
    LuaEngine* self = Self(L);
    const char* path = luaL_checkstring(L, 1);
    if (!OSData::SD_usable) { lua_pushnil(L); return 1; }
    if (!self->SdPathAllowed(path)) {
        LOG_APP_WARN("pico.sd_list: アプリディレクトリ外へのアクセスは許可されていません: %s", path);
        lua_pushnil(L);
        return 1;
    }

    FsFile dir = OSData::SD.open(path, O_RDONLY);
    if (!dir || !dir.isDir()) {
        if (dir) dir.close();
        lua_pushnil(L);
        return 1;
    }

    // FileExplorer::update_list()と同じ走査方法。{name=..., is_dir=...}の配列を返す
    lua_newtable(L);
    int idx = 1;
    FsFile file;
    char name[128];
    while (file.openNext(&dir, O_RDONLY)) {
        if (file.getName(name, sizeof(name))) {
            lua_newtable(L);
            lua_pushstring(L, name);
            lua_setfield(L, -2, "name");
            lua_pushboolean(L, file.isDir());
            lua_setfield(L, -2, "is_dir");
            lua_pushinteger(L, file.isDir() ? 0 : (lua_Integer)file.fileSize());
            lua_setfield(L, -2, "size");
            lua_rawseti(L, -2, idx++);
        }
        file.close();
    }
    dir.close();
    return 1;
}

// ---------------- 設定ファイル ----------------
// ヘッダのl_config_readの説明参照。書式はPICO_Config(key=value、'#'でコメント、後勝ち)。

namespace {
    // config_read/config_getの共通の入口。読めるならtrue(ファイルがあり、ディレクトリでなく、
    // 大きさがpico.sd_readと同じ上限以内)。読めない理由はここでログへ出す
    bool OpenableConfig(LuaEngine* self, const char* path, const char* api, size_t max_bytes,
                        bool path_allowed) {
        (void)self;
        if (!OSData::SD_usable) return false;
        if (!path_allowed) {
            LOG_APP_WARN("%s: アプリディレクトリ外へのアクセスは許可されていません: %s", api, path);
            return false;
        }
        if (!OSData::SD.exists(path)) return false;
        FsFile f = OSData::SD.open(path, O_RDONLY);
        if (!f) return false;
        const bool is_dir = f.isDir();
        const size_t size = f.fileSize();
        f.close();
        if (is_dir) return false;
        if (size > max_bytes) {
            LOG_APP_WARN("%s: %s が上限(%uB)を超えています(%uB)", api, path,
                (unsigned)max_bytes, (unsigned)size);
            return false;
        }
        return true;
    }

    // キーとして書けるか。読み戻したときに同じキーになる形だけを許す
    // ('='を含まない・前後に空白が無い・'#'で始まらない・改行を含まない)
    bool ValidConfigKey(const char* key) {
        const size_t len = strlen(key);
        if (len == 0 || len >= PICO_Config::kConfigMaxKeyLen) return false;
        if (key[0] == '#' || key[0] == ' ' || key[0] == '\t') return false;
        if (key[len - 1] == ' ' || key[len - 1] == '\t') return false;
        for (const char* p = key; *p; ++p) {
            if (*p == '=' || *p == '\r' || *p == '\n') return false;
        }
        return true;
    }
}

int LuaEngine::l_config_read(lua_State* L) {
    LuaEngine* self = Self(L);
    const char* path = luaL_checkstring(L, 1);
    if (!OpenableConfig(self, path, "pico.config_read", kMaxSdReadBytes, self->SdPathAllowed(path))) {
        lua_pushnil(L);
        return 1;
    }

    lua_newtable(L);
    const bool ok = PICO_Config::ParseFile(path, [&](const char* key, const char* value) {
        // 後勝ち: 同じキーが後で出てくればそのまま上書きされる
        lua_pushstring(L, value);
        lua_setfield(L, -2, key);
    });
    if (!ok) {
        lua_pop(L, 1);
        lua_pushnil(L);
    }
    return 1;
}

int LuaEngine::l_config_get(lua_State* L) {
    LuaEngine* self = Self(L);
    const char* path = luaL_checkstring(L, 1);
    const char* key = luaL_checkstring(L, 2);
    if (!OpenableConfig(self, path, "pico.config_get", kMaxSdReadBytes, self->SdPathAllowed(path))) {
        lua_pushnil(L);
        return 1;
    }

    FixedString<PICO_Config::kConfigMaxValueLen> found;
    bool has = false;
    PICO_Config::ParseFile(path, [&](const char* k, const char* v) {
        if (strcmp(k, key) == 0) { found.assign(v); has = true; } // 後勝ち
    });
    if (has) lua_pushstring(L, found.c_str());
    else lua_pushnil(L);
    return 1;
}

int LuaEngine::l_config_write(lua_State* L) {
    LuaEngine* self = Self(L);
    const char* path = luaL_checkstring(L, 1);
    const char* key = luaL_checkstring(L, 2);
    if (!ValidConfigKey(key)) {
        return luaL_error(L, "pico.config_write: キーが不正です('='・改行・前後の空白を含まず、"
                             "'#'で始まらない%d字未満の文字列): %s",
                          (int)PICO_Config::kConfigMaxKeyLen, key);
    }

    // 値は文字列/数値/真偽値。読み戻し(PICO_Config::ConfigValue::AsXxx)で同じ値になる表記にする
    char num_buf[48];
    const char* value = nullptr;
    switch (lua_type(L, 3)) {
    case LUA_TBOOLEAN:
        value = PICO_Config::ConfigValue::FromBool(lua_toboolean(L, 3));
        break;
    case LUA_TNUMBER:
        if (lua_isinteger(L, 3)) {
            snprintf(num_buf, sizeof(num_buf), "%lld", (long long)lua_tointeger(L, 3));
        } else {
            // AsFloat()は指数表記を受け付けないので%gは使えない。小数点以下の余分な0は削る
            snprintf(num_buf, sizeof(num_buf), "%.6f", (double)lua_tonumber(L, 3));
            char* end = num_buf + strlen(num_buf);
            while (end > num_buf + 1 && end[-1] == '0' && end[-2] != '.') *--end = '\0';
        }
        value = num_buf;
        break;
    case LUA_TSTRING:
        value = lua_tostring(L, 3);
        break;
    default:
        return luaL_error(L, "pico.config_write: 値は文字列・数値・真偽値のどれかです");
    }

    if (!OSData::SD_usable) { lua_pushboolean(L, false); return 1; }
    // 改行を含む値を通すと、次の行として別のキー(権限等)を差し込めてしまう
    if (strchr(value, '\n') || strchr(value, '\r')) {
        LOG_APP_WARN("pico.config_write: 値に改行は使えません (%s)", key);
        lua_pushboolean(L, false);
        return 1;
    }
    if (!self->SdWriteAllowed(path, "pico.config_write")) {
        lua_pushboolean(L, false);
        return 1;
    }

    lua_pushboolean(L, PICO_Config::SetValue(path, key, value));
    return 1;
}

// ---------------- ダイアログ ----------------
// ヘッダのクラスコメント「ダイアログ」参照。いずれも
// new Xxx(...) → WidgetFunctions::AddDialog() → setVisible(true) → WireDialogClosed()
// という同じ手順を踏み、生成したWidgetIdを返す。

int LuaEngine::l_show_message(lua_State* L) {
    const char* text = luaL_checkstring(L, 1);
    const char* cancel_text = luaL_checkstring(L, 2);
    const char* ok_text = luaL_checkstring(L, 3);

    MsgDialog* dialog = new MsgDialog(text, cancel_text, ok_text);
    if (!dialog) return luaL_error(L, "pico.show_message: 生成に失敗しました(メモリ不足の可能性)");

    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    const WidgetId id = dialog->getId();
    Self(L)->WireDialogClosed(dialog, id);

    lua_pushinteger(L, (lua_Integer)id);
    return 1;
}

int LuaEngine::l_show_input(lua_State* L) {
    const char* label = luaL_checkstring(L, 1);
    const char* initial_text = luaL_optstring(L, 2, "");
    // 省略時はtrue(単一行)。lua_toboolean()は未指定/nilをfalseとして返すため、
    // 「複数行を明示的に指定しない限り単一行」にするには先にnoneornilを見る必要がある
    const bool is_single_line = lua_isnoneornil(L, 3) ? true : (bool)lua_toboolean(L, 3);

    // ボタンの文字。省略時は従来どおり「決定」「キャンセル」、空文字列ならそのボタンを出さない
    const char* submit_text = luaL_optstring(L, 4, "決定");
    const char* cancel_text = luaL_optstring(L, 5, "キャンセル");

    InputDialog* dialog = new InputDialog(label, is_single_line, submit_text, cancel_text);
    if (!dialog) return luaL_error(L, "pico.show_input: 生成に失敗しました(メモリ不足の可能性)");
    if (initial_text && *initial_text) dialog->setInput(initial_text);

    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    const WidgetId id = dialog->getId();
    Self(L)->WireDialogClosed(dialog, id);

    lua_pushinteger(L, (lua_Integer)id);
    return 1;
}

int LuaEngine::l_show_file_save(lua_State* L) {
    const char* start_dir = luaL_optstring(L, 1, "/");
    const char* default_name = luaL_optstring(L, 2, nullptr);

    FileSaveDialog* dialog = new FileSaveDialog(start_dir);
    if (!dialog) return luaL_error(L, "pico.show_file_save: 生成に失敗しました(メモリ不足の可能性)");
    if (default_name) dialog->setFileName(default_name);

    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    const WidgetId id = dialog->getId();
    Self(L)->WireDialogClosed(dialog, id);

    lua_pushinteger(L, (lua_Integer)id);
    return 1;
}

int LuaEngine::l_show_file_select(lua_State* L) {
    const char* start_dir = luaL_optstring(L, 1, "/");

    FileSelectDialog* dialog = new FileSelectDialog(start_dir);
    if (!dialog) return luaL_error(L, "pico.show_file_select: 生成に失敗しました(メモリ不足の可能性)");

    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    const WidgetId id = dialog->getId();
    Self(L)->WireDialogClosed(dialog, id);

    lua_pushinteger(L, (lua_Integer)id);
    return 1;
}

int LuaEngine::l_show_color(lua_State* L) {
    ColorDialog* dialog = new ColorDialog();
    if (!dialog) return luaL_error(L, "pico.show_color: 生成に失敗しました(メモリ不足の可能性)");

    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    const WidgetId id = dialog->getId();
    Self(L)->WireDialogClosed(dialog, id);

    lua_pushinteger(L, (lua_Integer)id);
    return 1;
}

// ---------------- ネットワーク ----------------
// ヘッダのクラスコメント「ネットワーク」と、冒頭の「2026-10-05」の4を参照。
//
//   pico.http_request(method, url, body, content_type, callback [, opts]) -> リクエストID | false
//     opts = { headers = { ["Authorization"] = "Bearer ..." }, save_to = "/path/to/file" }
//     callback(ok, status, body, err, headers, info)
//       headers = 応答ヘッダ(小文字の名前: content-type / location / etag / last-modified / content-length)
//       info    = { size = 受け取った本文のバイト数, saved = save_toのパス(保存したときだけ) }
//     走るのは1本だけ。2本目以降は(最大kMaxHttpQueue本)順番に始める。待たせられなければfalse
//   pico.http_cancel([id]) -> 取り消せたか(idなしは全部。コールバックは呼ばれない)

void LuaEngine::FreePending(lua_State* L, PendingHttp* r) {
    if (!r) return;
    if (r->callback_ref != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, r->callback_ref);
    delete r;
}

// idle(何も走っていない)ときに r を走らせる。成功したら r の中身を HttpState へ移すので、
// 呼び出し側は r の callback_ref を触らない(r自体は呼び出し側がdeleteする)。
// 失敗したら false と理由(静的文字列)を返す
bool LuaEngine::StartHttp(PendingHttp* r, const char** why) {
    *why = "";
    if (!http_) http_ = new HttpState();
    HttpState* st = http_;

    st->sink.body.clear();
    st->body_buf.clear();
    st->content_type_buf.clear();
    st->save_path.clear();
    st->save_part.clear();
    st->file_sink.closeFile();
    st->file_sink.total = 0;

    // HttpRequestは送信ボディ/Content-Typeを非所有ポインタで受け取るため、
    // 呼び出し側の一時的な文字列をそのまま渡さず、リクエストが終わるまで
    // 生きているst->body_buf/content_type_bufへ一度コピーしてから渡す
    const void* body_ptr = nullptr;
    size_t body_len = 0;
    if (!r->body.empty()) {
        st->body_buf.assign(r->body.data(), r->body.size());
        body_ptr = st->body_buf.c_str();
        body_len = st->body_buf.length();
    }
    const char* content_type_ptr = nullptr;
    if (!r->content_type.empty()) {
        st->content_type_buf.assign(r->content_type);
        content_type_ptr = st->content_type_buf.c_str();
    }

    st->request.clearExtraHeaders();
    for (const auto& h : r->headers) {
        if (!st->request.addExtraHeader(h.first.c_str(), h.second.c_str())) {
            *why = "リクエストヘッダが長すぎる/不正";
            return false;
        }
    }

    IHttpSink* sink = &st->sink;
    if (!r->save_to.empty()) {
        // 保存先: <path>.part へ書き、最後まで受け取れたら差し替える
        st->save_path.assign(r->save_to.c_str());
        st->save_part.assign(r->save_to.c_str());
        if (!st->save_part.append(".part")) {
            st->save_path.clear();
            *why = "保存先のパスが長すぎる";
            return false;
        }
        st->file_sink.file = OSData::SD.open(st->save_part.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
        if (!st->file_sink.file) {
            st->save_path.clear();
            st->save_part.clear();
            *why = "保存先を開けない";
            return false;
        }
        st->file_sink.is_open = true;
        sink = &st->file_sink;
    }
    // 保存するときは、全体の10秒ではなく「何も届かない時間」で打ち切る(大きなファイル向け)
    st->request.setIdleTimeout(!r->save_to.empty());

    if (!st->request.begin(r->url, r->method, sink, body_ptr, body_len, content_type_ptr)) {
        st->file_sink.closeFile();
        if (st->saving()) OSData::SD.remove(st->save_part.c_str());
        st->save_path.clear();
        st->save_part.clear();
        *why = "リクエストを開始できない";
        return false;
    }

    st->callback_ref = r->callback_ref;
    st->cur_id = r->id;
    r->callback_ref = LUA_NOREF; // HttpStateへ移した
    return true;
}

int LuaEngine::l_http_request(lua_State* L) {
    LuaEngine* self = Self(L);
    const char* method_str = luaL_checkstring(L, 1);
    const char* url_str = luaL_checkstring(L, 2);
    size_t body_len = 0;
    const char* body = lua_isnoneornil(L, 3) ? nullptr : luaL_checklstring(L, 3, &body_len);
    const char* content_type = lua_isnoneornil(L, 4) ? nullptr : luaL_checkstring(L, 4);
    luaL_checktype(L, 5, LUA_TFUNCTION);
    if (!lua_isnoneornil(L, 6)) luaL_checktype(L, 6, LUA_TTABLE);

    HttpRequest::Method method;
    if (!HttpMethodFromName(method_str, method)) {
        return luaL_error(L, "pico.http_request: 未知のメソッド '%s'(GET/POST/PUT/PATCH/DELETEのいずれか)", method_str);
    }

    // opts.headers / opts.save_to の形の誤りはプログラムの誤りとして先にエラーにする
    // (権限や状態による拒否より前。std::vector等はまだ作らない=longjmpで漏らさない)
    const char* save_to = nullptr;
    if (lua_istable(L, 6)) {
        lua_getfield(L, 6, "save_to");
        if (!lua_isnil(L, -1)) save_to = luaL_checkstring(L, -1);
        lua_pop(L, 1);   // save_toの文字列は引数のテーブルが保持しているので有効なまま
        lua_getfield(L, 6, "headers");
        if (!lua_isnil(L, -1)) {
            if (!lua_istable(L, -1)) return luaL_error(L, "pico.http_request: opts.headers はテーブルで指定してください");
            size_t total = 0;
            lua_pushnil(L);
            while (lua_next(L, -2) != 0) {
                if (lua_type(L, -2) != LUA_TSTRING) return luaL_error(L, "pico.http_request: ヘッダ名は文字列で指定してください");
                const char* name = lua_tostring(L, -2);
                if (lua_type(L, -1) != LUA_TSTRING && lua_type(L, -1) != LUA_TNUMBER) {
                    return luaL_error(L, "pico.http_request: ヘッダ '%s' の値は文字列か数値で指定してください", name);
                }
                // 数値はここで文字列にしない(キー走査中の変換は避ける)ので長さは別に数える
                char numbuf[40];
                const char* value = numbuf;
                if (lua_type(L, -1) == LUA_TSTRING) {
                    value = lua_tostring(L, -1);
                } else if (lua_isinteger(L, -1)) {
                    snprintf(numbuf, sizeof(numbuf), "%lld", (long long)lua_tointeger(L, -1));
                } else {
                    snprintf(numbuf, sizeof(numbuf), "%.14g", (double)lua_tonumber(L, -1));
                }
                if (!HttpHeaderValid(name, value)) {
                    return luaL_error(L, "pico.http_request: ヘッダ '%s' の名前か値に使えない文字があります", name);
                }
                if (HttpHeaderReserved(name)) {
                    return luaL_error(L, "pico.http_request: ヘッダ '%s' は指定できません(自動で付く/変えると危険)", name);
                }
                total += strlen(name) + strlen(value) + 4;
                if (total > kMaxHttpHeaderBytes) {
                    return luaL_error(L, "pico.http_request: ヘッダの合計が大きすぎます(%dバイトまで)", (int)kMaxHttpHeaderBytes);
                }
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    }

    if (!self->permissions_.network) {
        LOG_APP_WARN("pico.http_request: このアプリにはネットワーク権限がありません");
        lua_pushboolean(L, false);
        return 1;
    }

    Url url;
    if (!UrlTools::Parse(url, url_str)) {
        lua_pushboolean(L, false); // 不正なURL(httpsも通る。接続はHttp_Transportが担う)
        return 1;
    }

    if (body_len > kMaxHttpBodyBytes) {
        LOG_APP_WARN("pico.http_request: リクエストボディが上限(%uB)を超えています",
            (unsigned)kMaxHttpBodyBytes);
        lua_pushboolean(L, false);
        return 1;
    }

    if (save_to) {
        // 保存先はSDへの書き込みなので、sd_writeと同じ確認(app_dir外・app.cfgは拒否)を通す
        if (!OSData::SD_usable || !self->SdWriteAllowed(save_to, "pico.http_request(save_to)")) {
            lua_pushboolean(L, false);
            return 1;
        }
    }

    // 走っているものがある、または待っているものがあるなら順番待ち(待たせられなければ拒否)
    const bool must_wait = self->HttpBusy();
    if (must_wait && self->http_queue_.size() >= kMaxHttpQueue) {
        lua_pushboolean(L, false);
        return 1;
    }

    PendingHttp* r = new PendingHttp();
    r->method = method;
    r->url = url;
    if (body && body_len > 0) r->body.assign(body, body_len);
    if (content_type && *content_type) r->content_type.assign(content_type);
    if (save_to) r->save_to = save_to;

    if (lua_istable(L, 6)) {
        // 上の検査を通っているので型はもう確かめない。ここからはlongjmpしうる関数(lua_next等の
        // 型変換)を避けて読む: 値が数値のものは上で文字列化した結果と同じ書式で作り直す
        lua_getfield(L, 6, "headers");
        if (lua_istable(L, -1)) {
            lua_pushnil(L);
            while (lua_next(L, -2) != 0) {
                char numbuf[40];
                const char* value = numbuf;
                if (lua_type(L, -1) == LUA_TSTRING) value = lua_tostring(L, -1);
                else if (lua_isinteger(L, -1)) snprintf(numbuf, sizeof(numbuf), "%lld", (long long)lua_tointeger(L, -1));
                else snprintf(numbuf, sizeof(numbuf), "%.14g", (double)lua_tonumber(L, -1));
                r->headers.emplace_back(lua_tostring(L, -2), value);
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    }

    HttpState* st = self->http_;
    const int id = st ? st->next_id : 1;
    if (!st) { self->http_ = new HttpState(); st = self->http_; }
    st->next_id++;
    r->id = id;

    lua_pushvalue(L, 5);
    r->callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    if (must_wait) {
        self->http_queue_.push_back(r);
    } else {
        const char* why = "";
        if (!self->StartHttp(r, &why)) {
            LOG_APP_WARN("pico.http_request: %s", why);
            FreePending(L, r);
            lua_pushboolean(L, false);
            return 1;
        }
        delete r;
    }

    lua_pushinteger(L, id);
    return 1;
}

int LuaEngine::l_http_cancel(lua_State* L) {
    LuaEngine* self = Self(L);
    bool found = false;
    const bool all = lua_isnoneornil(L, 1);
    const int id = all ? 0 : (int)luaL_checkinteger(L, 1);

    if (self->http_ && self->http_->callback_ref != LUA_NOREF && (all || self->http_->cur_id == id)) {
        HttpState* st = self->http_;
        st->request.cancel();
        st->file_sink.closeFile();
        if (st->saving()) OSData::SD.remove(st->save_part.c_str());
        st->save_path.clear();
        st->save_part.clear();
        luaL_unref(L, LUA_REGISTRYINDEX, st->callback_ref);
        st->callback_ref = LUA_NOREF;
        found = true;
    }
    for (size_t i = 0; i < self->http_queue_.size();) {
        if (all || self->http_queue_[i]->id == id) {
            FreePending(L, self->http_queue_[i]);
            self->http_queue_.erase(self->http_queue_.begin() + (long)i);
            found = true;
        } else {
            i++;
        }
    }
    lua_pushboolean(L, found);
    return 1;
}

bool LuaEngine::HttpBusy() const {
    return (http_ && http_->callback_ref != LUA_NOREF) || !http_queue_.empty();
}

// 順番待ちの先頭を走らせる(何も走っていないときだけ)。始められなかったものは
// 失敗としてそのコールバックを呼び、次の待ちへ進む
void LuaEngine::StartQueuedHttp() {
    while (!http_queue_.empty() && !(http_ && http_->callback_ref != LUA_NOREF)) {
        PendingHttp* r = http_queue_.front();
        http_queue_.erase(http_queue_.begin());
        const char* why = "";
        if (StartHttp(r, &why)) {
            delete r;
            return;
        }
        // 始められなかった: コールバックへ失敗を知らせる(ok=false, status=0, body=nil, err=理由)
        const int ref = r->callback_ref;
        r->callback_ref = LUA_NOREF;
        delete r;
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        lua_pushboolean(L, 0);
        lua_pushinteger(L, 0);
        lua_pushnil(L);
        lua_pushstring(L, why);
        if (ProtectedCall(4) != LUA_OK) {
            ReportError("pico.http_requestのコールバックでエラーが発生しました");
        }
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
    }
}

void LuaEngine::UpdateHttp() {
    if (http_ && http_->callback_ref != LUA_NOREF) {
        // 保存(ダウンロード)中は1フレームに何回か進めて速度を稼ぐ(1回で読むのは1KBまで)。
        // 時間で区切って画面を止めない
        if (http_->saving()) {
            const unsigned long t0 = millis();
            for (int i = 0; i < 16; i++) {
                http_->request.update();
                if (http_->request.getStatus() != TaskTools::PROCESSING) break;
                if (millis() - t0 >= 6) break;
            }
        } else {
            http_->request.update();
        }
        if (http_->request.getStatus() != TaskTools::PROCESSING) FinishHttp();
    }
    // 何も走っていなければ待っているものを始める(終わったフレームのうちに次へ進める)
    StartQueuedHttp();
}

// 終わったリクエストの結果をコールバックへ渡す
void LuaEngine::FinishHttp() {
    HttpState* st = http_;
    bool ok = (st->request.getStatus() == TaskTools::SUCCESS);
    const int status_code = ok ? st->request.response().statusCode() : 0;
    const char* err_text = ok ? nullptr : st->request.failureToStr();

    // 保存先へ書いていたら、最後まで受け取れたときだけ本来の名前へ差し替える
    const bool saved = st->saving();
    const size_t saved_bytes = st->file_sink.total;
    FixedString<PICO_PATH_LEN> saved_path;
    if (saved) {
        st->file_sink.closeFile();
        if (ok) {
            OSData::SD.remove(st->save_path.c_str());
            if (OSData::SD.rename(st->save_part.c_str(), st->save_path.c_str())) {
                saved_path.assign(st->save_path);
            } else {
                ok = false;
                err_text = "保存先へ差し替えられない";
                OSData::SD.remove(st->save_part.c_str());
            }
        } else {
            OSData::SD.remove(st->save_part.c_str());
        }
        st->save_path.clear();
        st->save_part.clear();
    }

    // 先に外しておく: コールバック内からpico.http_request()を再度呼べるようにするため
    // (走っているかの判定はcallback_refを見ている。待ちがあれば新しい要求はその後ろへ並ぶ)
    const int ref = st->callback_ref;
    st->callback_ref = LUA_NOREF;
    st->cur_id = 0;

    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    lua_pushboolean(L, ok);
    lua_pushinteger(L, status_code);
    if (ok && !saved && !st->sink.body.empty()) {
        // 本文にNULが混じり得るため、strlen前提のlua_pushstring()ではなく
        // 長さ明示のlua_pushlstring()を使う
        lua_pushlstring(L, st->sink.body.c_str(), st->sink.body.length());
    } else {
        lua_pushnil(L);
    }
    if (!ok) lua_pushstring(L, err_text ? err_text : "失敗");
    else lua_pushnil(L);

    // 応答ヘッダ(小文字の名前)
    lua_createtable(L, 0, 5);
    if (st->request.getStatus() == TaskTools::SUCCESS || st->request.response().headersDone()) {
        const HttpResponse& res = st->request.response();
        if (!res.contentType().empty()) {
            lua_pushstring(L, res.contentType().c_str());
            lua_setfield(L, -2, "content-type");
        }
        if (!res.location().empty()) {
            lua_pushstring(L, res.location().c_str());
            lua_setfield(L, -2, "location");
        }
        if (!res.validator().empty()) {
            lua_pushstring(L, res.validator().c_str());
            lua_setfield(L, -2, res.validatorIsEtag() ? "etag" : "last-modified");
        }
        if (res.contentLength() >= 0) {
            lua_pushinteger(L, res.contentLength());
            lua_setfield(L, -2, "content-length");
        }
    }
    // 受け取った本文の大きさ(と保存先)
    lua_createtable(L, 0, 2);
    lua_pushinteger(L, saved ? (lua_Integer)saved_bytes : (lua_Integer)st->sink.body.length());
    lua_setfield(L, -2, "size");
    if (!saved_path.empty()) {
        lua_pushstring(L, saved_path.c_str());
        lua_setfield(L, -2, "saved");
    }

    if (ProtectedCall(6) != LUA_OK) {
        ReportError("pico.http_requestのコールバックでエラーが発生しました");
    }

    luaL_unref(L, LUA_REGISTRYINDEX, ref);
}

// ---------------- JSON ----------------

int LuaEngine::l_json_decode(lua_State* L) {
    size_t len = 0;
    const char* s = luaL_checklstring(L, 1, &len);
    const bool keep_null = lua_toboolean(L, 2) != 0;
    const char* err = nullptr;
    size_t pos = 0;
    if (!LuaJson::Decode(L, s, len, keep_null, err, pos)) {
        lua_pushnil(L);
        lua_pushfstring(L, "%s (位置 %d)", err, (int)pos);
        return 2;
    }
    return 1;
}

int LuaEngine::l_json_encode(lua_State* L) {
    luaL_checkany(L, 1);
    const char* err = nullptr;
    std::string out;
    const bool ok = LuaJson::EncodeValue(L, 1, out, 0, err);
    lua_settop(L, 1);
    if (!ok) {
        lua_pushnil(L);
        lua_pushstring(L, err ? err : "失敗");
        return 2;
    }
    // (メモリ不足でlua_pushlstringがlongjmpするとoutが漏れるが、その時はアプリごと閉じるので許容する)
    lua_pushlstring(L, out.data(), out.size());
    return 1;
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

namespace {
    constexpr size_t kMaxStoreBytes = PICO_STR_16KiB;
    constexpr size_t kMaxStoreFileBytes = 24 * 1024;   // 暗号化した保存ファイルの読み込み上限
}

int LuaEngine::l_store_load(lua_State* L) {
    LuaEngine* self = Self(L);
    // pico.store_load([{password = "..."}]) 暗号化して保存したstoreを読む(パスワードを付けて保存したときだけ必要)
    const char* password = nullptr;
    if (lua_istable(L, 1)) {
        lua_getfield(L, 1, "password");   // スタックに残したまま使う(文字列を生かしておくため)
        if (lua_type(L, -1) == LUA_TSTRING && lua_tostring(L, -1)[0]) password = lua_tostring(L, -1);
    }
    if (!OSData::SD_usable) { lua_pushnil(L); return 1; }
    FixedString<PICO_PATH_LEN> path;
    if (!PICO_IO::join(path, self->app_dir_, "store.json")) { lua_pushnil(L); return 1; }
    if (!OSData::SD.exists(path.c_str())) { lua_pushnil(L); return 1; }

    const char* why = "";
    // 暗号化すると約4/3倍になるので、読む上限は平文の上限より少し大きくしてある
    if (pushFileString(L, path.c_str(), kMaxStoreFileBytes, &why) == 0) {
        LOG_APP_WARN("pico.store_load: %s", why);
        lua_pushnil(L);
        return 1;
    }
    size_t len = 0;
    const char* s = lua_tolstring(L, -1, &len);
    const char* err = nullptr;
    size_t pos = 0;

    if (SecretAead::IsEncrypted(s, len)) {
        SecretAead::Result r;
        bool decoded = false;
        {
            std::string plain;
            r = SecretAead::Decrypt(s, len, self->app_dir_.c_str(), password, plain);
            if (r == SecretAead::Result::Ok) {
                decoded = LuaJson::Decode(L, plain.data(), plain.size(), false, err, pos);
            }
        }
        if (r != SecretAead::Result::Ok) {
            LOG_APP_WARN("pico.store_load: %s", SecretAead::ResultToStr(r));
            lua_pushnil(L);
            lua_pushstring(L, SecretAead::ResultToStr(r));
            return 2;
        }
        if (!decoded) {
            LOG_APP_WARN("pico.store_load: store.jsonが壊れています: %s", err ? err : "?");
            lua_pushnil(L);
        }
        return 1;
    }

    if (!LuaJson::Decode(L, s, len, false, err, pos)) {
        LOG_APP_WARN("pico.store_load: store.jsonが壊れています: %s", err ? err : "?");
        lua_pushnil(L);
    }
    return 1;
}

int LuaEngine::l_store_save(lua_State* L) {
    LuaEngine* self = Self(L);
    luaL_checkany(L, 1);
    // pico.store_save(tbl [, {encrypt = true, password = "..."}])
    //   encrypt=trueで暗号化して保存する(passwordを渡すとそのパスワードで。渡さなければアプリごとの固定鍵)。
    //   passwordだけ渡してもencrypt=true扱い。暗号化すると平文の上限は12KiB
    bool encrypt = false;
    const char* password = nullptr;
    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "encrypt");
        encrypt = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        lua_getfield(L, 2, "password");
        if (lua_type(L, -1) == LUA_TSTRING && lua_tostring(L, -1)[0]) { password = lua_tostring(L, -1); encrypt = true; }
        // passwordの文字列はスタックに残したまま使う
    }
    lua_settop(L, lua_istable(L, 2) ? 3 : 1);

    const char* err = nullptr;
    bool ok = false;
    bool too_big = false;
    const char* enc_err = nullptr;
    FixedString<PICO_PATH_LEN> path, part;
    {
        std::string json;
        ok = LuaJson::EncodeValue(L, 1, json, 0, err);
        if (ok && json.size() > kMaxStoreBytes) { too_big = true; ok = false; }
        if (ok && encrypt) {
            if (json.size() > SecretAead::kMaxPlainBytes) {
                too_big = true;
                ok = false;
            } else {
                std::string enc;
                const SecretAead::Result r = SecretAead::Encrypt((const uint8_t*)json.data(), json.size(),
                                                                 self->app_dir_.c_str(), password, enc);
                if (r != SecretAead::Result::Ok) { ok = false; enc_err = SecretAead::ResultToStr(r); }
                else json.swap(enc);
            }
        }
        if (ok) {
            ok = OSData::SD_usable
                && PICO_IO::join(path, self->app_dir_, "store.json")
                && self->SdWriteAllowed(path.c_str(), "pico.store_save");
            if (ok) {
                part.assign(path);
                part.append(".tmp");
                // 一時ファイルへ書いてから差し替える(書き込み中の電源断で既存の内容を壊さない)
                FsFile f = OSData::SD.open(part.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
                ok = f && (json.empty() || f.write(json.data(), json.size()) == json.size());
                if (f) f.close();
                if (ok) {
                    OSData::SD.remove(path.c_str());
                    ok = OSData::SD.rename(part.c_str(), path.c_str());
                } else if (OSData::SD_usable) {
                    OSData::SD.remove(part.c_str());
                }
            }
        }
    }
    if (err) return luaL_error(L, "pico.store_save: 値をJSONにできません(%s)", err);
    if (too_big) {
        LOG_APP_WARN("pico.store_save: 大きすぎます(%uBまで)", (unsigned)(encrypt ? SecretAead::kMaxPlainBytes : kMaxStoreBytes));
    }
    if (enc_err) LOG_APP_WARN("pico.store_save: 暗号化できません: %s", enc_err);
    lua_pushboolean(L, ok);
    return 1;
}

// ---------------- require ----------------

namespace {
    constexpr size_t kMaxModuleBytes = 32 * 1024;
    constexpr int kMaxPreloadModules = 16;
    char g_require_marker = 0; // 実行中のモジュールの目印(循環の検出)

    bool IsIdentChar(char c) {
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
    }

    // "a.b" の形。英数字・_・-・区切りの.だけ。..や先頭末尾の.は不可
    bool ModuleNameValid(const char* name) {
        if (!name || !*name) return false;
        const size_t n = strlen(name);
        if (n > 47) return false;
        if (name[0] == '.' || name[n - 1] == '.') return false;
        for (size_t i = 0; i < n; i++) {
            const char c = name[i];
            if (c == '.') {
                if (i + 1 < n && name[i + 1] == '.') return false;
                continue;
            }
            if (!IsIdentChar(c) && c != '-') return false;
        }
        return true;
    }

    // registry[key] のテーブルをスタックに積む(無ければ作る)
    void PushRegTable(lua_State* L, const char* key) {
        lua_getfield(L, LUA_REGISTRYINDEX, key);
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            lua_newtable(L);
            lua_pushvalue(L, -1);
            lua_setfield(L, LUA_REGISTRYINDEX, key);
        }
    }

    // ソース中の require("名前") / require "名前" / require('名前') を拾って worklist (テーブルの添字idx)へ足す。
    // 文字列として拾うだけ(コメントの中でも拾うが、無いモジュールは無視されるだけで害は無い)
    void ScanRequires(lua_State* L, const char* src, size_t len, int wl_idx) {
        static const char kWord[] = "require";
        const size_t wn = sizeof(kWord) - 1;
        for (size_t i = 0; i + wn < len; i++) {
            if (src[i] != 'r' || memcmp(src + i, kWord, wn) != 0) continue;
            if (i > 0 && IsIdentChar(src[i - 1])) continue;
            size_t j = i + wn;
            if (j < len && IsIdentChar(src[j])) continue;
            while (j < len && (src[j] == ' ' || src[j] == '\t')) j++;
            if (j < len && src[j] == '(') { j++; while (j < len && (src[j] == ' ' || src[j] == '\t')) j++; }
            if (j >= len || (src[j] != '"' && src[j] != '\'')) continue;
            const char q = src[j++];
            const size_t start = j;
            while (j < len && src[j] != q && src[j] != '\n') j++;
            if (j >= len || src[j] != q) continue;
            const size_t nlen = j - start;
            if (nlen == 0 || nlen > 47) continue;
            char name[48];
            memcpy(name, src + start, nlen);
            name[nlen] = '\0';
            if (!ModuleNameValid(name)) continue;

            const size_t count = lua_rawlen(L, wl_idx);
            if ((int)count >= kMaxPreloadModules) return;
            bool dup = false;
            for (size_t k = 1; k <= count && !dup; k++) {
                lua_rawgeti(L, wl_idx, (lua_Integer)k);
                dup = (strcmp(lua_tostring(L, -1), name) == 0);
                lua_pop(L, 1);
            }
            if (dup) continue;
            lua_pushstring(L, name);
            lua_rawseti(L, wl_idx, (lua_Integer)count + 1);
        }
    }
}

// 1つの名前の置き場所を探す(<app_dir>/<a/b>.lua → <app_dir>/<a/b>/init.lua)
bool LuaEngine::ResolveModulePath(const char* name, FixedString<PICO_PATH_LEN>& out) const {
    if (!OSData::SD_usable) return false;
    FixedString<PICO_STR_M> rel;
    rel.assign(name);
    char* buf = const_cast<char*>(rel.c_str());
    for (char* p = buf; *p; p++) if (*p == '.') *p = '/';

    static const char* const kSuffix[] = {".lua", "/init.lua"};
    for (const char* suf : kSuffix) {
        FixedString<PICO_PATH_LEN> cand;
        if (!PICO_IO::join(cand, app_dir_, rel.c_str())) continue;
        if (!cand.append(suf)) continue;
        if (!SdPathAllowed(cand.c_str())) continue;
        if (OSData::SD.exists(cand.c_str())) { out.assign(cand); return true; }
    }
    return false;
}

// path のファイルを文字列としてスタックに積む(積んだら1、できなければ0で何も積まない)
int LuaEngine::pushFileString(lua_State* L, const char* path, size_t max, const char** why) {
    *why = "";
    FsFile f = OSData::SD.open(path, O_RDONLY);
    if (!f) { *why = "開けません"; return 0; }
    const size_t file_size = f.fileSize();
    if (file_size > max) {
        f.close();
        *why = "大きすぎます";
        return 0;
    }
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    char chunk[256];
    size_t remaining = file_size;
    bool ok = true;
    while (remaining > 0) {
        const size_t want = (remaining < sizeof(chunk)) ? remaining : sizeof(chunk);
        const int got = f.read((uint8_t*)chunk, want);
        if (got <= 0) { ok = false; break; }
        luaL_addlstring(&b, chunk, (size_t)got);
        remaining -= (size_t)got;
    }
    f.close();
    if (!ok) {
        luaL_pushresult(&b); // バッファの後始末(箱をスタックから外す)
        lua_pop(L, 1);
        *why = "読み取りに失敗しました";
        return 0;
    }
    luaL_pushresult(&b);
    return 1;
}

int LuaEngine::PreloadTrampoline(lua_State* L) {
    LuaEngine* self = static_cast<LuaEngine*>(lua_touserdata(L, 1));
    const char* src = static_cast<const char*>(lua_touserdata(L, 2));
    const size_t len = (size_t)lua_tointeger(L, 3);
    self->preloadModules(src, len);
    return 0;
}

// スクリプトが require する名前を拾い、モジュールを(実行の外で)読み込んでおく。
// 読み込んだ関数は registry.pico_preload[名前] に、構文エラーなどはその文字列で置く
void LuaEngine::preloadModules(const char* src, size_t len) {
    lua_newtable(L);                       // worklist
    const int wl = lua_gettop(L);
    ScanRequires(L, src, len, wl);
    if (lua_rawlen(L, wl) == 0) { lua_pop(L, 1); return; }

    PushRegTable(L, "pico_preload");       // preload
    const int pre = lua_gettop(L);

    for (size_t i = 1; i <= lua_rawlen(L, wl); i++) {   // 読んだモジュールが増やした分も辿る
        lua_rawgeti(L, wl, (lua_Integer)i);
        const char* name = lua_tostring(L, -1);          // worklistが保持しているので有効
        lua_getfield(L, pre, name);
        const bool already = !lua_isnil(L, -1);
        lua_pop(L, 1);
        if (already) { lua_pop(L, 1); continue; }

        // OS同梱のモジュール(pico.ui / pico.async)は、アプリのフォルダより優先してそのソースを使う
        if (const char* bsrc = LuaBuiltin::Source(name)) {
            FixedString<PICO_STR_M> bname;
            bname.assign("=");
            bname.append(name);
            luaL_loadbufferx(L, bsrc, strlen(bsrc), bname.c_str(), "t"); // 成功なら関数、失敗ならメッセージ
            lua_setfield(L, pre, name);
            lua_pop(L, 1);
            continue;
        }

        FixedString<PICO_PATH_LEN> path;
        if (!ResolveModulePath(name, path)) { lua_pop(L, 1); continue; } // 無ければ実行時にエラーになる

        const char* why = "";
        if (pushFileString(L, path.c_str(), kMaxModuleBytes, &why) == 0) {
            lua_pushfstring(L, "module '%s' を読めません(%s): %s", name, path.c_str(), why);
            lua_setfield(L, pre, name);
            lua_pop(L, 1);
            continue;
        }
        size_t slen = 0;
        const char* s = lua_tolstring(L, -1, &slen);
        ScanRequires(L, s, slen, wl);

        FixedString<PICO_PATH_LEN> chunkname;
        chunkname.assign("@");
        chunkname.append(path);
        if (luaL_loadbufferx(L, s, slen, chunkname.c_str(), "t") == LUA_OK) {
            lua_setfield(L, pre, name);        // 関数
        } else {
            lua_setfield(L, pre, name);        // エラーメッセージ(文字列)
        }
        lua_pop(L, 2);                          // ソースの文字列と名前
    }
    lua_settop(L, wl - 1);
}

int LuaEngine::l_require(lua_State* L) {
    LuaEngine* self = Self(L);
    const char* name = luaL_checkstring(L, 1);
    if (!ModuleNameValid(name)) {
        return luaL_error(L, "require: モジュール名 '%s' は使えません(英数字・_・-と、区切りの.だけ)", name);
    }

    PushRegTable(L, "pico_loaded");
    const int loaded = lua_gettop(L);
    lua_getfield(L, loaded, name);
    if (!lua_isnil(L, -1)) {
        if (lua_islightuserdata(L, -1) && lua_touserdata(L, -1) == &g_require_marker) {
            return luaL_error(L, "require: '%s' が循環しています", name);
        }
        return 1; // 読み込み済み
    }
    lua_pop(L, 1);

    // 先読みしてあれば、その関数(文字列ならその時のエラー)を使う
    PushRegTable(L, "pico_preload");
    const int pre = lua_gettop(L);
    lua_getfield(L, pre, name);
    FixedString<PICO_PATH_LEN> path;
    if (lua_isstring(L, -1) && !lua_isnumber(L, -1)) {
        return luaL_error(L, "%s", lua_tostring(L, -1));
    }
    if (lua_isfunction(L, -1)) {
        lua_pushnil(L);
        lua_setfield(L, pre, name);           // 先読みの置き場から外す(関数は下の呼び出しで使い切る)
        self->ResolveModulePath(name, path);  // 表示用(デバッガ・エラー)。見つからなければ空のまま
    } else {
        lua_pop(L, 1);
        // 先読みで拾えなかった名前(組み立てた名前など)。実行中のコンパイルはスタックを使うので、
        // Luaの呼び出しが浅い(トップレベル近く)ときだけ許す
        lua_Debug ar;
        if (lua_getstack(L, 6, &ar)) {
            return luaL_error(L, "require: '%s' を実行中に読み込めません(関数の奥から呼ばれています)。"
                                 "スクリプトの先頭で require(\"%s\") と書くか、トップレベルで呼んでください", name, name);
        }
        if (!self->ResolveModulePath(name, path)) {
            return luaL_error(L, "require: モジュール '%s' が見つかりません(アプリのフォルダの %s.lua または %s/init.lua)",
                              name, name, name);
        }
        const char* why = "";
        if (pushFileString(L, path.c_str(), kMaxModuleBytes, &why) == 0) {
            return luaL_error(L, "require: '%s' を読めません(%s): %s", name, path.c_str(), why);
        }
        size_t slen = 0;
        const char* s = lua_tolstring(L, -1, &slen);
        FixedString<PICO_PATH_LEN> chunkname;
        chunkname.assign("@");
        chunkname.append(path);
        if (luaL_loadbufferx(L, s, slen, chunkname.c_str(), "t") != LUA_OK) {
            return lua_error(L);              // 構文エラーのメッセージ
        }
        lua_remove(L, -2);                    // ソースの文字列
    }

    // スタック: [..., loaded, pre, fn]
    lua_pushlightuserdata(L, &g_require_marker);
    lua_setfield(L, loaded, name);            // 実行中の目印(循環の検出)
    lua_pushvalue(L, -1);
    lua_pushstring(L, name);
    lua_pushstring(L, path.c_str());
    if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
        // 失敗した目印を残さない。エラーはそのまま呼び出し元へ(打ち切りの握り潰しにならない)
        lua_pushnil(L);
        lua_setfield(L, loaded, name);
        return lua_error(L);
    }
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_pushboolean(L, 1);
    }
    lua_pushvalue(L, -1);
    lua_setfield(L, loaded, name);
    return 1;
}
