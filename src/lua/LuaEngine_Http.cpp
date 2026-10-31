// pico.* のうちHTTP(http_request の順番待ちと受信)とJSON
#include "lua/LuaEngine.hpp"
#include "lua/LuaEngine_Internal.hpp"
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
#include "gui/icons/Image_Codec.hpp"
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
#include "system/I18n_Functions.hpp"


using namespace LuaInternal;

namespace {
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
        // 上限を超えて断った(info.too_large。誤りの文は言語で変わるので、アプリはこちらで見分ける)
        bool too_large = false;
        bool write(const void* data, size_t len) override {
            if (body.length() + len > kMaxHttpResponseBytes) { too_large = true; return false; }
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

    // 足せるリクエストヘッダの合計(HttpRequestの extra_header_ 1KiBに、区切りを含めて収まる大きさ)。
    // 1行(名前: 値)は HttpRequest::kMaxHeaderLineBytes 未満(Spotify等のBearerトークン300〜400文字が入る)
    constexpr size_t kMaxHttpHeaderBytes = 1000;

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
    bool keep_alive = false;
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


// ---------------- ネットワーク ----------------
// ヘッダのクラスコメント「ネットワーク」と、冒頭の「2026-10-05」の4を参照。
//
//   pico.http_request(method, url, body, content_type, callback [, opts]) -> リクエストID | false
//     opts = { headers = { ["Authorization"] = "Bearer ..." }, save_to = "/path/to/file", keep_alive = true }
//     callback(ok, status, body, err, headers, info)
//       headers = 応答ヘッダ(小文字の名前: content-type / location / etag / last-modified / content-length)
//       info    = { size = 受け取った本文のバイト数, saved = save_toのパス(保存したときだけ),
//                   reused = 持っていた接続を使い回して送ったか }
//     走るのは1本だけ。2本目以降は(最大kMaxHttpQueue本)順番に始める。待たせられなければfalse
//     keep_alive = true の要求は、応答の後も接続(httpsならTLSの約40KBごと)を持っておき、次の
//     keep_alive = true の要求が同じ相手(ホスト・ポート・http/https)ならハンドシェイクを飛ばして送る。
//     keep_aliveでない要求を始めると持っていた接続は閉じる。アプリを閉じれば必ず閉じる
//   pico.http_cancel([id]) -> 取り消せたか(idなしは全部。コールバックは呼ばれない)
//   pico.http_close() -> 持っていた接続を閉じたか(走っている要求は、終わったときに閉じる)

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
    st->sink.too_large = false;
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
            *why = TR("リクエストヘッダが長すぎる/不正");
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
            *why = TR("保存先のパスが長すぎる");
            return false;
        }
        st->file_sink.file = OSData::SD.open(st->save_part.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
        if (!st->file_sink.file) {
            st->save_path.clear();
            st->save_part.clear();
            *why = TR("保存先を開けない");
            return false;
        }
        st->file_sink.is_open = true;
        sink = &st->file_sink;
    }
    // 保存するときは、全体の10秒ではなく「何も届かない時間」で打ち切る(大きなファイル向け)
    st->request.setIdleTimeout(!r->save_to.empty());
    // 接続の使い回し。falseにすると持っていた接続はここで閉じる(begin()の前に決める)
    st->request.setKeepAlive(r->keep_alive);

    if (!st->request.begin(r->url, r->method, sink, body_ptr, body_len, content_type_ptr)) {
        st->file_sink.closeFile();
        if (st->saving()) OSData::SD.remove(st->save_part.c_str());
        st->save_path.clear();
        st->save_part.clear();
        *why = TR("リクエストを開始できない");
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
    bool keep_alive = false;
    if (lua_istable(L, 6)) {
        lua_getfield(L, 6, "keep_alive");
        if (!lua_isnil(L, -1)) {
            if (!lua_isboolean(L, -1)) return luaL_error(L, "pico.http_request: opts.keep_alive は真偽値で指定してください");
            keep_alive = lua_toboolean(L, -1) != 0;
        }
        lua_pop(L, 1);
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
                if (strlen(name) + strlen(value) + 4 >= HttpRequest::kMaxHeaderLineBytes) {
                    return luaL_error(L, "pico.http_request: ヘッダ '%s' が長すぎます(1行%dバイト未満)", name,
                                      (int)HttpRequest::kMaxHeaderLineBytes - 4);
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
    r->keep_alive = keep_alive;

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

int LuaEngine::l_http_close(lua_State* L) {
    LuaEngine* self = Self(L);
    bool had = false;
    if (self->http_) {
        had = self->http_->request.hasIdleConnection();
        // 待っている接続は今閉じる。走っている要求があれば、その応答の後で閉じる
        self->http_->request.setKeepAlive(false);
    }
    lua_pushboolean(L, had);
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
            ReportError(TR("pico.http_requestのコールバックでエラーが発生しました"));
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
                err_text = TR("保存先へ差し替えられない");
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
    if (!ok) lua_pushstring(L, err_text ? err_text : TR("失敗"));
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
    // 受け取った本文の大きさ(と保存先・接続を使い回したか)
    lua_createtable(L, 0, 4);
    lua_pushboolean(L, st->request.reusedConnection());
    lua_setfield(L, -2, "reused");
    if (!ok && !saved && st->sink.too_large) {
        lua_pushboolean(L, 1);
        lua_setfield(L, -2, "too_large");
    }
    lua_pushinteger(L, saved ? (lua_Integer)saved_bytes : (lua_Integer)st->sink.body.length());
    lua_setfield(L, -2, "size");
    if (!saved_path.empty()) {
        lua_pushstring(L, saved_path.c_str());
        lua_setfield(L, -2, "saved");
    }

    if (ProtectedCall(6) != LUA_OK) {
        ReportError(TR("pico.http_requestのコールバックでエラーが発生しました"));
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
        lua_pushfstring(L, TR("%s (位置 %d)"), err, (int)pos);
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
        lua_pushstring(L, err ? err : TR("失敗"));
        return 2;
    }
    // (メモリ不足でlua_pushlstringがlongjmpするとoutが漏れるが、その時はアプリごと閉じるので許容する)
    lua_pushlstring(L, out.data(), out.size());
    return 1;
}

void LuaEngine::DestroyHttp() {
    for (PendingHttp* r : http_queue_) delete r; // 順番待ちの控え(Luaのrefはlua_close()が捨てる)
    http_queue_.clear();
    delete http_;
    http_ = nullptr;
}
