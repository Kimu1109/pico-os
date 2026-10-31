// pico.socket.*(TCP・UDP・WebSocket・MQTT)。ネットワークの権限(permission_network)が要る。
//
// HTTP(pico.http_request)と違い、繋いだままにして双方向に何度もやり取りするためのもの
// (Home Assistant の WebSocket API・MQTTのブローカ・LANのIoT機器・リアルタイムのチャット)。
// 設計の記録は docs/design/browser-net.md「ソケット(pico.socket)」。
//
//   pico.socket.tcp(host, port, callback [, {tls = true}])     -> id | nil, 理由
//       callback("open") / callback("data", bytes) / callback("close", err_or_nil)
//   pico.socket.udp(callback [, {port = 待つ番号}])           -> id | nil, 理由
//       callback("data", bytes, from_host, from_port)
//   pico.socket.websocket(url, callback [, opts])              -> id | nil, 理由   (ws:// / wss://)
//       opts = { protocol = "...", headers = {...}, max_message = バイト, ping_interval = 秒(0で送らない) }
//       callback("open", protocol) / callback("message", data, is_binary) / callback("close", err_or_nil, code, reason)
//   pico.socket.mqtt(url, callback [, opts])                   -> id | nil, 理由   (mqtt:// / mqtts://)
//       opts = { client_id, username, password, keepalive = 秒, clean = true, max_packet = バイト,
//                will = { topic, payload, qos, retain } }
//       callback("connect", session_present) / callback("message", topic, payload, {qos, retain})
//       callback("subscribed", packet_id, granted_qos) / callback("unsubscribed", packet_id)
//       callback("published", packet_id) / callback("close", err_or_nil)
//   pico.socket.send(id, data [, {binary = true}])  -> 送れたか   (tcp / websocket)
//   pico.socket.sendto(id, host, port, data)        -> 送れたか   (udp)
//   pico.socket.publish(id, topic, payload [, {qos = 0|1, retain = false}]) -> packet_id(QoS0は0) | false
//   pico.socket.subscribe(id, topic [, qos])        -> packet_id | false
//   pico.socket.unsubscribe(id, topic)              -> packet_id | false
//   pico.socket.close(id)                           -> 閉じ始めたか(この後に必ず "close" が1回来る)
//   pico.socket.state(id)                           -> "connecting" | "open" | "closing" | "closed" | nil
//
// - 同時に持てるのは kMaxSockets 本まで。アプリを閉じれば全部閉じる(MQTTは DISCONNECT、WebSocketは close を送る)
// - 繋ぐ(とTLSのハンドシェイク)は作った次のフレームで同期的に進む(HTTPと同じ割り切り。最大3秒)
// - バックグラウンドのスクリプトでは使えない(常駐の接続は電池とWi-Fiを使い続けるため)

#include "lua/LuaEngine.hpp"
#include "lua/LuaEngine_Internal.hpp"

#include "net/Http_Transport.hpp"
#include "net/Mqtt_Client.hpp"
#include "net/Udp_Socket.hpp"
#include "net/WebSocket_Client.hpp"
#include "system/Log_Functions.hpp"
#include "util/Url.hpp"

#include <cstring>
#include <new>
#include "system/I18n_Functions.hpp"

using namespace LuaInternal;

namespace {
    constexpr int kMaxSockets = 4;
    // 1フレームで1本から受け取るイベントの数の上限(続きは次のフレーム。画面を止めない)
    constexpr int kMaxEventsPerFrame = 16;
    // TCPで1フレームに読む回数(1回 kScratchBytes まで)
    constexpr int kTcpReadsPerFrame = 4;
    constexpr size_t kScratchBytes = UdpSocket::kMaxDatagram;
    // 送る1回の上限(Luaの予算を1回で食い潰さないため。HTTPの本文と同じ)
    constexpr size_t kMaxSendBytes = PICO_STR_16KiB;
    // TCPで繋ぐときの待ち時間(HTTPと同じ。到達しない相手でここまで画面が止まりうる)
    constexpr unsigned long kTcpConnectTimeoutMs = 3000;

    enum class Kind : uint8_t { Tcp, Udp, WebSocket, Mqtt };
    enum class TcpPhase : uint8_t { Connecting, Open };

    struct LuaSocket {
        int id = 0;
        Kind kind = Kind::Tcp;
        int callback_ref = LUA_NOREF;

        // 種類ごとの本体(どれか1つだけ)
        HttpTransport* tcp = nullptr;
        Url tcp_url;
        TcpPhase tcp_phase = TcpPhase::Connecting;
        UdpSocket* udp = nullptr;
        WebSocketClient* ws = nullptr;
        MqttClient* mqtt = nullptr;

        // TCP/UDPの "close" を次のフレームで渡す(WebSocket/MQTTは本体が Closed を出す)
        bool close_pending = false;
        FixedString<PICO_STR_LL> close_error;   // 空なら nil を渡す

        ~LuaSocket(){
            delete tcp;
            delete udp;
            delete ws;
            delete mqtt;
        }
    };

    const char* KindName(Kind k){
        switch(k){
            case Kind::Tcp: return "tcp";
            case Kind::Udp: return "udp";
            case Kind::WebSocket: return "websocket";
            default: return "mqtt";
        }
    }
}

// pico.socket.* の状態。使わないアプリのメモリを0に保つため、初めて作るときに new する
struct LuaEngine::SocketState {
    LuaSocket* slots[kMaxSockets] = {};
    int next_id = 1;
    uint8_t scratch[kScratchBytes];
};

namespace LuaEngineSocket {
    LuaEngine::SocketState* State(LuaEngine* self){
        if(!self->socketState()) self->setSocketState(new (std::nothrow) LuaEngine::SocketState());
        return self->socketState();
    }

    LuaSocket* Find(LuaEngine* self, lua_Integer id){
        LuaEngine::SocketState* st = self->socketState();
        if(!st) return nullptr;
        for(LuaSocket* s : st->slots){
            if(s && s->id == id) return s;
        }
        return nullptr;
    }

    // 種類が合わなければプログラムの誤りとしてエラーにする(閉じた後のidは nil として扱う)
    LuaSocket* Check(lua_State* L, int idx, Kind kind, const char* api){
        LuaEngine* self = Self(L);
        const lua_Integer id = luaL_checkinteger(L, idx);
        LuaSocket* s = Find(self, id);
        if(s && s->kind != kind){
            luaL_error(L, "%s: %d は %s のソケットです", api, (int)id, KindName(s->kind));
            return nullptr;
        }
        return s;
    }

    // 権限と空きを見る。だめなら nil, 理由 を積んで false
    bool Precheck(lua_State* L, const char* api){
        LuaEngine* self = Self(L);
        if(!self->permissions().network){
            LOG_APP_WARN("%s: このアプリにはネットワーク権限がありません", api);
            lua_pushnil(L);
            lua_pushstring(L, TR("ネットワークの権限がありません"));
            return false;
        }
        LuaEngine::SocketState* st = State(self);
        int used = 0;
        if(st){
            for(LuaSocket* s : st->slots) if(s) used++;
        }
        if(!st || used >= kMaxSockets){
            lua_pushnil(L);
            lua_pushstring(L, st ? TR("同時に開けるソケットの数を超えています") : TR("メモリが足りません"));
            return false;
        }
        return true;
    }

    // 本体を作り終えた s を空いている場所へ入れ、id を積む
    int Register(lua_State* L, LuaSocket* s, int callback_idx){
        LuaEngine::SocketState* st = Self(L)->socketState();
        lua_pushvalue(L, callback_idx);
        s->callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);
        s->id = st->next_id++;
        for(LuaSocket*& slot : st->slots){
            if(!slot){ slot = s; break; }
        }
        lua_pushinteger(L, s->id);
        return 1;
    }

    int Fail(lua_State* L, LuaSocket* s, const char* why){
        delete s;
        lua_pushnil(L);
        lua_pushstring(L, why);
        return 2;
    }

    // opts[key] が真偽値なら読む(型違いはエラー)
    bool OptBool(lua_State* L, int t, const char* key, bool def){
        if(!lua_istable(L, t)) return def;
        lua_getfield(L, t, key);
        bool v = def;
        if(!lua_isnil(L, -1)){
            if(!lua_isboolean(L, -1)) luaL_error(L, "opts.%s は真偽値で指定してください", key);
            v = lua_toboolean(L, -1) != 0;
        }
        lua_pop(L, 1);
        return v;
    }

    lua_Integer OptInt(lua_State* L, int t, const char* key, lua_Integer def, lua_Integer lo, lua_Integer hi){
        if(!lua_istable(L, t)) return def;
        lua_getfield(L, t, key);
        lua_Integer v = def;
        if(!lua_isnil(L, -1)){
            if(!lua_isinteger(L, -1)) luaL_error(L, "opts.%s は整数で指定してください", key);
            v = lua_tointeger(L, -1);
            if(v < lo || v > hi) luaL_error(L, "opts.%s は %d〜%d です", key, (int)lo, (int)hi);
        }
        lua_pop(L, 1);
        return v;
    }

    // opts[key] の文字列を out へ(無ければ空)。長すぎればエラー
    template <size_t N>
    bool OptStr(lua_State* L, int t, const char* key, FixedString<N>& out){
        out.clear();
        if(!lua_istable(L, t)) return false;
        lua_getfield(L, t, key);
        bool has = false;
        if(!lua_isnil(L, -1)){
            if(lua_type(L, -1) != LUA_TSTRING) luaL_error(L, "opts.%s は文字列で指定してください", key);
            size_t len = 0;
            const char* s = lua_tolstring(L, -1, &len);
            if(strlen(s) != len) luaL_error(L, "opts.%s にNULは入れられません", key);
            if(!out.assign(s, len)) luaL_error(L, "opts.%s が長すぎます(%dバイトまで)", key, (int)N - 1);
            has = true;
        }
        lua_pop(L, 1);
        return has;
    }

    // ---------------- 作る ----------------

    int l_tcp(lua_State* L){
        const char* host = luaL_checkstring(L, 1);
        const lua_Integer port = luaL_checkinteger(L, 2);
        luaL_checktype(L, 3, LUA_TFUNCTION);
        if(!lua_isnoneornil(L, 4)) luaL_checktype(L, 4, LUA_TTABLE);
        if(port < 1 || port > 65535) return luaL_error(L, "pico.socket.tcp: ポートは1〜65535です");
        const bool tls = OptBool(L, 4, "tls", false);
        if(!Precheck(L, "pico.socket.tcp")) return 2;

        LuaSocket* s = new (std::nothrow) LuaSocket();
        if(!s) return Fail(L, nullptr, TR("メモリが足りません"));
        s->kind = Kind::Tcp;
        if(!*host || !s->tcp_url.host.assign(host)) return Fail(L, s, TR("ホスト名が空か長すぎます"));
        s->tcp_url.port = (uint16_t)port;
        s->tcp_url.secure = tls;
        s->tcp_url.path.assign("/");
        s->tcp = new (std::nothrow) HttpTransport();
        if(!s->tcp) return Fail(L, s, TR("メモリが足りません"));
        return Register(L, s, 3);
    }

    int l_udp(lua_State* L){
        luaL_checktype(L, 1, LUA_TFUNCTION);
        if(!lua_isnoneornil(L, 2)) luaL_checktype(L, 2, LUA_TTABLE);
        const lua_Integer port = OptInt(L, 2, "port", 0, 0, 65535);
        if(!Precheck(L, "pico.socket.udp")) return 2;

        LuaSocket* s = new (std::nothrow) LuaSocket();
        if(!s) return Fail(L, nullptr, TR("メモリが足りません"));
        s->kind = Kind::Udp;
        s->udp = new (std::nothrow) UdpSocket();
        if(!s->udp) return Fail(L, s, TR("メモリが足りません"));
        if(!s->udp->begin((uint16_t)port)) return Fail(L, s, TR("UDPの待ち受けを始められません(番号が使用中?)"));
        return Register(L, s, 1);
    }

    int l_websocket(lua_State* L){
        const char* url = luaL_checkstring(L, 1);
        luaL_checktype(L, 2, LUA_TFUNCTION);
        if(!lua_isnoneornil(L, 3)) luaL_checktype(L, 3, LUA_TTABLE);
        FixedString<PICO_STR_M> protocol;
        OptStr(L, 3, "protocol", protocol);
        const lua_Integer max_message = OptInt(L, 3, "max_message", (lua_Integer)WebSocketClient::kDefaultMaxMessage,
                                               (lua_Integer)WebSocketClient::kMinMaxMessage,
                                               (lua_Integer)WebSocketClient::kMaxMaxMessage);
        const lua_Integer ping_s = OptInt(L, 3, "ping_interval",
                                          (lua_Integer)(WebSocketClient::kDefaultPingIntervalMs / 1000), 0, 3600);
        // ヘッダは形だけ先に確かめる(足すのは作った後。ここで失敗するとlongjmpで漏れるため)
        if(lua_istable(L, 3)){
            lua_getfield(L, 3, "headers");
            if(!lua_isnil(L, -1)){
                if(!lua_istable(L, -1)) return luaL_error(L, "pico.socket.websocket: opts.headers はテーブルで指定してください");
                lua_pushnil(L);
                while(lua_next(L, -2) != 0){
                    if(lua_type(L, -2) != LUA_TSTRING || lua_type(L, -1) != LUA_TSTRING){
                        return luaL_error(L, "pico.socket.websocket: ヘッダの名前と値は文字列で指定してください");
                    }
                    lua_pop(L, 1);
                }
            }
            lua_pop(L, 1);
        }
        if(!Precheck(L, "pico.socket.websocket")) return 2;

        LuaSocket* s = new (std::nothrow) LuaSocket();
        if(!s) return Fail(L, nullptr, TR("メモリが足りません"));
        s->kind = Kind::WebSocket;
        s->ws = new (std::nothrow) WebSocketClient();
        if(!s->ws) return Fail(L, s, TR("メモリが足りません"));
        if(!s->ws->setProtocol(protocol.c_str())) return Fail(L, s, TR("opts.protocol に使えない文字があります"));
        if(lua_istable(L, 3)){
            lua_getfield(L, 3, "headers");
            if(lua_istable(L, -1)){
                lua_pushnil(L);
                while(lua_next(L, -2) != 0){
                    const bool ok = s->ws->addHeader(lua_tostring(L, -2), lua_tostring(L, -1));
                    lua_pop(L, 1);
                    if(!ok){
                        lua_pop(L, 2);
                        return Fail(L, s, TR("ヘッダが長すぎるか、使えない名前・文字があります"));
                    }
                }
            }
            lua_pop(L, 1);
        }
        s->ws->setPingInterval((unsigned long)ping_s * 1000UL);
        if(!s->ws->begin(url, (size_t)max_message)) return Fail(L, s, TR("URLが ws:// か wss:// でない、またはメモリが足りません"));
        return Register(L, s, 2);
    }

    int l_mqtt(lua_State* L){
        const char* url = luaL_checkstring(L, 1);
        luaL_checktype(L, 2, LUA_TFUNCTION);
        if(!lua_isnoneornil(L, 3)) luaL_checktype(L, 3, LUA_TTABLE);
        const lua_Integer max_packet = OptInt(L, 3, "max_packet", (lua_Integer)MqttClient::kDefaultMaxPacket,
                                              (lua_Integer)MqttClient::kMinMaxPacket, (lua_Integer)MqttClient::kMaxMaxPacket);
        if(!Precheck(L, "pico.socket.mqtt")) return 2;

        // 約1KBあるのでスタックに置かない。読む途中のエラー(longjmp)で漏れないよう、
        // Luaのuserdataに置いてGCに任せる(デストラクタは要らない作り: FixedString だけ)
        void* mem = lua_newuserdatauv(L, sizeof(MqttClient::Options), 0);
        MqttClient::Options* o = new (mem) MqttClient::Options();
        const int top = lua_gettop(L);
        OptStr(L, 3, "client_id", o->client_id);
        OptStr(L, 3, "username", o->username);
        o->has_password = OptStr(L, 3, "password", o->password);
        o->keepalive_s = (uint16_t)OptInt(L, 3, "keepalive", 60, 0, 65535);
        o->clean_session = OptBool(L, 3, "clean", true);
        if(lua_istable(L, 3)){
            lua_getfield(L, 3, "will");
            if(!lua_isnil(L, -1)){
                if(!lua_istable(L, -1)) return luaL_error(L, "pico.socket.mqtt: opts.will はテーブルで指定してください");
                const int w = lua_gettop(L);
                if(!OptStr(L, w, "topic", o->will_topic)) return luaL_error(L, "pico.socket.mqtt: opts.will.topic がありません");
                OptStr(L, w, "payload", o->will_payload);
                o->will_qos = (uint8_t)OptInt(L, w, "qos", 0, 0, 2);
                o->will_retain = OptBool(L, w, "retain", false);
            }
            lua_pop(L, 1);
        }
        if(o->client_id.empty() && !o->clean_session){
            return luaL_error(L, "pico.socket.mqtt: clean = false には opts.client_id が要ります");
        }
        if(!o->will_topic.empty() && !MqttPacket::ValidTopic(o->will_topic.c_str(), false)){
            return luaL_error(L, "pico.socket.mqtt: opts.will.topic が話題の形になっていません");
        }

        LuaSocket* s = new (std::nothrow) LuaSocket();
        if(!s) return Fail(L, nullptr, TR("メモリが足りません"));
        s->kind = Kind::Mqtt;
        s->mqtt = new (std::nothrow) MqttClient();
        const bool ok = s->mqtt && s->mqtt->begin(url, *o, (size_t)max_packet);
        lua_settop(L, top - 1); // Options のuserdataを捨てる(begin() が写した)
        if(!ok) return Fail(L, s, TR("URLが mqtt:// か mqtts:// でない、またはメモリが足りません"));
        return Register(L, s, 2);
    }

    // ---------------- 送る ----------------

    int l_send(lua_State* L){
        LuaEngine* self = Self(L);
        const lua_Integer id = luaL_checkinteger(L, 1);
        size_t len = 0;
        const char* data = luaL_checklstring(L, 2, &len);
        if(!lua_isnoneornil(L, 3)) luaL_checktype(L, 3, LUA_TTABLE);
        const bool binary = OptBool(L, 3, "binary", false);
        if(len > kMaxSendBytes) return luaL_error(L, "pico.socket.send: 1回に送れるのは%dバイトまでです", (int)kMaxSendBytes);
        LuaSocket* s = Find(self, id);
        bool ok = false;
        if(s && !s->close_pending){
            if(s->kind == Kind::Tcp){
                if(s->tcp_phase == TcpPhase::Open){
                    ok = s->tcp->write((const uint8_t*)data, len) == len;
                }
            }else if(s->kind == Kind::WebSocket){
                ok = binary ? s->ws->sendBinary(data, len) : s->ws->sendText(data, len);
            }else{
                return luaL_error(L, "pico.socket.send: %s のソケットには使えません(%s)", KindName(s->kind),
                                  s->kind == Kind::Udp ? "sendto を使う" : "publish を使う");
            }
        }
        lua_pushboolean(L, ok);
        return 1;
    }

    int l_sendto(lua_State* L){
        LuaSocket* s = Check(L, 1, Kind::Udp, "pico.socket.sendto");
        const char* host = luaL_checkstring(L, 2);
        const lua_Integer port = luaL_checkinteger(L, 3);
        size_t len = 0;
        const char* data = luaL_checklstring(L, 4, &len);
        if(port < 1 || port > 65535) return luaL_error(L, "pico.socket.sendto: ポートは1〜65535です");
        if(len > UdpSocket::kMaxDatagram) {
            return luaL_error(L, "pico.socket.sendto: 1つのデータグラムは%dバイトまでです", (int)UdpSocket::kMaxDatagram);
        }
        const bool ok = s && !s->close_pending && s->udp->sendTo(host, (uint16_t)port, data, len);
        lua_pushboolean(L, ok);
        return 1;
    }

    void PushPacketId(lua_State* L, int32_t id){
        if(id < 0) lua_pushboolean(L, 0);
        else lua_pushinteger(L, id);
    }

    int l_publish(lua_State* L){
        LuaSocket* s = Check(L, 1, Kind::Mqtt, "pico.socket.publish");
        const char* topic = luaL_checkstring(L, 2);
        size_t len = 0;
        const char* payload = luaL_checklstring(L, 3, &len);
        if(!lua_isnoneornil(L, 4)) luaL_checktype(L, 4, LUA_TTABLE);
        const lua_Integer qos = OptInt(L, 4, "qos", 0, 0, 1);
        const bool retain = OptBool(L, 4, "retain", false);
        if(len > kMaxSendBytes) return luaL_error(L, "pico.socket.publish: 1回に送れるのは%dバイトまでです", (int)kMaxSendBytes);
        if(!MqttPacket::ValidTopic(topic, false)) return luaL_error(L, "pico.socket.publish: 話題 '%s' は送れる形でありません", topic);
        PushPacketId(L, s ? s->mqtt->publish(topic, payload, len, (uint8_t)qos, retain) : -1);
        return 1;
    }

    int l_subscribe(lua_State* L){
        LuaSocket* s = Check(L, 1, Kind::Mqtt, "pico.socket.subscribe");
        const char* topic = luaL_checkstring(L, 2);
        const lua_Integer qos = luaL_optinteger(L, 3, 0);
        if(qos < 0 || qos > 2) return luaL_error(L, "pico.socket.subscribe: QoSは0〜2です");
        if(!MqttPacket::ValidTopic(topic, true)) return luaL_error(L, "pico.socket.subscribe: 話題 '%s' の形が違います", topic);
        PushPacketId(L, s ? s->mqtt->subscribe(topic, (uint8_t)qos) : -1);
        return 1;
    }

    int l_unsubscribe(lua_State* L){
        LuaSocket* s = Check(L, 1, Kind::Mqtt, "pico.socket.unsubscribe");
        const char* topic = luaL_checkstring(L, 2);
        if(!MqttPacket::ValidTopic(topic, true)) return luaL_error(L, "pico.socket.unsubscribe: 話題 '%s' の形が違います", topic);
        PushPacketId(L, s ? s->mqtt->unsubscribe(topic) : -1);
        return 1;
    }

    // ---------------- 閉じる・状態 ----------------

    // 閉じ始める。"close" は UpdateSockets() が渡す
    bool BeginClose(LuaSocket* s){
        switch(s->kind){
            case Kind::Tcp:
            case Kind::Udp:
                if(s->close_pending) return false;
                if(s->tcp) s->tcp->close();
                if(s->udp) s->udp->close();
                s->close_pending = true;
                s->close_error.clear();
                return true;
            case Kind::WebSocket: {
                const WebSocketClient::State st = s->ws->state();
                if(st == WebSocketClient::State::Closing || st == WebSocketClient::State::Closed) return false;
                s->ws->close();
                return true;
            }
            case Kind::Mqtt:
                if(s->mqtt->state() == MqttClient::State::Closed) return false;
                s->mqtt->disconnect();
                return true;
        }
        return false;
    }

    int l_close(lua_State* L){
        LuaSocket* s = Find(Self(L), luaL_checkinteger(L, 1));
        lua_pushboolean(L, s && BeginClose(s));
        return 1;
    }

    const char* StateName(LuaSocket* s){
        if(s->close_pending) return "closed";
        switch(s->kind){
            case Kind::Tcp: return s->tcp_phase == TcpPhase::Open ? "open" : "connecting";
            case Kind::Udp: return "open";
            case Kind::WebSocket:
                switch(s->ws->state()){
                    case WebSocketClient::State::Open: return "open";
                    case WebSocketClient::State::Closing: return "closing";
                    case WebSocketClient::State::Closed: return "closed";
                    default: return "connecting";
                }
            case Kind::Mqtt:
                switch(s->mqtt->state()){
                    case MqttClient::State::Connected: return "open";
                    case MqttClient::State::Closed: return "closed";
                    default: return "connecting";
                }
        }
        return "closed";
    }

    int l_state(lua_State* L){
        LuaSocket* s = Find(Self(L), luaL_checkinteger(L, 1));
        if(!s){
            lua_pushnil(L);
            return 1;
        }
        lua_pushstring(L, StateName(s));
        return 1;
    }

    bool Connecting(LuaSocket* s){
        switch(s->kind){
            case Kind::Tcp: return !s->close_pending && s->tcp_phase == TcpPhase::Connecting;
            case Kind::WebSocket: {
                const WebSocketClient::State st = s->ws->state();
                return st == WebSocketClient::State::Connecting || st == WebSocketClient::State::Handshake;
            }
            case Kind::Mqtt: {
                const MqttClient::State st = s->mqtt->state();
                return st == MqttClient::State::Connecting || st == MqttClient::State::WaitConnack;
            }
            default: return false;
        }
    }
}

// ---------------- LuaEngine 側の口 ----------------

bool LuaEngine::SocketBusy() const {
    if(!socket_) return false;
    for(LuaSocket* s : socket_->slots){
        if(s && LuaEngineSocket::Connecting(s)) return true;
    }
    return false;
}

void LuaEngine::DestroySockets(){
    if(!socket_) return;
    for(LuaSocket*& s : socket_->slots){
        if(!s) continue;
        //行儀よく閉じる挨拶だけ送る(返事は待たない)。Luaのrefは lua_close() が捨てる
        if(s->mqtt) s->mqtt->disconnect();
        if(s->ws) s->ws->close(WsFrame::kCloseGoingAway);
        delete s;
        s = nullptr;
    }
    delete socket_;
    socket_ = nullptr;
}

// s のコールバックを呼ぶ(関数・イベント名・引数を積んだ状態で。nargs はイベント名を含む引数の数)
void LuaEngine::CallSocket(int nargs){
    if(ProtectedCall(nargs) != LUA_OK){
        ReportError(TR("pico.socket のコールバックでエラーが発生しました"));
    }
}

void LuaEngine::UpdateSockets(){
    if(!socket_ || !L) return;
    for(int i = 0; i < kMaxSockets; i++){
        LuaSocket* s = socket_->slots[i];
        if(!s) continue;
        bool closed = false;
        const int ref = s->callback_ref;

        if(s->kind == Kind::Tcp || s->kind == Kind::Udp){
            if(!s->close_pending && s->kind == Kind::Tcp && s->tcp_phase == TcpPhase::Connecting){
                if(s->tcp->connect(s->tcp_url, kTcpConnectTimeoutMs)){
                    s->tcp_phase = TcpPhase::Open;
                    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
                    lua_pushstring(L, "open");
                    CallSocket(1);
                }else{
                    s->close_pending = true;
                    s->close_error.assign(s->tcp->errorText());
                }
            }else if(!s->close_pending && s->kind == Kind::Tcp){
                for(int r = 0; r < kTcpReadsPerFrame && !s->close_pending; r++){
                    if(s->tcp->available() <= 0) break;
                    const int n = s->tcp->read(socket_->scratch, sizeof(socket_->scratch));
                    if(n <= 0) break;
                    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
                    lua_pushstring(L, "data");
                    lua_pushlstring(L, (const char*)socket_->scratch, (size_t)n);
                    CallSocket(2);
                }
                //相手が閉じた(読み残しが無くなってから知らせる)
                if(!s->close_pending && !s->tcp->connected() && s->tcp->available() <= 0){
                    s->tcp->close();
                    s->close_pending = true;
                    s->close_error.clear();
                }
            }else if(!s->close_pending){
                for(int r = 0; r < kMaxEventsPerFrame && !s->close_pending; r++){
                    char from[UdpSocket::kMaxAddrText];
                    uint16_t from_port = 0;
                    const int n = s->udp->receive(socket_->scratch, sizeof(socket_->scratch), from, from_port);
                    if(n < 0) break;
                    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
                    lua_pushstring(L, "data");
                    lua_pushlstring(L, (const char*)socket_->scratch, (size_t)n);
                    lua_pushstring(L, from);
                    lua_pushinteger(L, from_port);
                    CallSocket(4);
                }
            }
            if(s->close_pending){
                lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
                lua_pushstring(L, "close");
                if(s->close_error.empty()) lua_pushnil(L);
                else lua_pushstring(L, s->close_error.c_str());
                CallSocket(2);
                closed = true;
            }
        }else if(s->kind == Kind::WebSocket){
            WebSocketClient::Event ev;
            for(int r = 0; r < kMaxEventsPerFrame && !closed && s->ws->poll(ev); r++){
                lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
                switch(ev.type){
                    case WebSocketClient::EventType::Open:
                        lua_pushstring(L, "open");
                        lua_pushstring(L, s->ws->protocol());
                        CallSocket(2);
                        break;
                    case WebSocketClient::EventType::Message:
                        lua_pushstring(L, "message");
                        lua_pushlstring(L, (const char*)ev.data, ev.len);
                        lua_pushboolean(L, ev.binary);
                        CallSocket(3);
                        break;
                    case WebSocketClient::EventType::Closed:
                        lua_pushstring(L, "close");
                        if(ev.error) lua_pushstring(L, ev.error);
                        else lua_pushnil(L);
                        lua_pushinteger(L, ev.code);
                        lua_pushlstring(L, (const char*)ev.data, ev.len);
                        CallSocket(4);
                        closed = true;
                        break;
                }
            }
        }else{
            MqttClient::Event ev;
            for(int r = 0; r < kMaxEventsPerFrame && !closed && s->mqtt->poll(ev); r++){
                lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
                switch(ev.type){
                    case MqttClient::EventType::Connected:
                        lua_pushstring(L, "connect");
                        lua_pushboolean(L, ev.session_present);
                        CallSocket(2);
                        break;
                    case MqttClient::EventType::Message:
                        lua_pushstring(L, "message");
                        lua_pushlstring(L, (const char*)ev.topic, ev.topic_len);
                        lua_pushlstring(L, (const char*)ev.payload, ev.payload_len);
                        lua_createtable(L, 0, 2);
                        lua_pushinteger(L, ev.qos);
                        lua_setfield(L, -2, "qos");
                        lua_pushboolean(L, ev.retain);
                        lua_setfield(L, -2, "retain");
                        CallSocket(4);
                        break;
                    case MqttClient::EventType::Subscribed:
                        lua_pushstring(L, "subscribed");
                        lua_pushinteger(L, ev.packet_id);
                        lua_pushinteger(L, ev.granted_qos);
                        CallSocket(3);
                        break;
                    case MqttClient::EventType::Unsubscribed:
                        lua_pushstring(L, "unsubscribed");
                        lua_pushinteger(L, ev.packet_id);
                        CallSocket(2);
                        break;
                    case MqttClient::EventType::Published:
                        lua_pushstring(L, "published");
                        lua_pushinteger(L, ev.packet_id);
                        CallSocket(2);
                        break;
                    case MqttClient::EventType::Closed:
                        lua_pushstring(L, "close");
                        if(ev.error) lua_pushstring(L, ev.error);
                        else lua_pushnil(L);
                        CallSocket(2);
                        closed = true;
                        break;
                }
            }
        }

        //コールバックの中でアプリが閉じられた(DestroySockets 済み)
        if(!socket_) return;
        if(closed){
            luaL_unref(L, LUA_REGISTRYINDEX, ref);
            delete socket_->slots[i];
            socket_->slots[i] = nullptr;
        }
    }
}

void LuaEngine::RegisterSocketApi(){
    // スタックのトップは pico テーブル。pico.socket を作ってそこへ並べる
    lua_newtable(L);
    api_cur_ = &api_socket_;
    registerFn("tcp", LuaEngineSocket::l_tcp);
    registerFn("udp", LuaEngineSocket::l_udp);
    registerFn("websocket", LuaEngineSocket::l_websocket);
    registerFn("mqtt", LuaEngineSocket::l_mqtt);
    registerFn("send", LuaEngineSocket::l_send);
    registerFn("sendto", LuaEngineSocket::l_sendto);
    registerFn("publish", LuaEngineSocket::l_publish);
    registerFn("subscribe", LuaEngineSocket::l_subscribe);
    registerFn("unsubscribe", LuaEngineSocket::l_unsubscribe);
    registerFn("close", LuaEngineSocket::l_close);
    registerFn("state", LuaEngineSocket::l_state);
    finishApiTable(api_socket_, 0);
    api_cur_ = &api_pico_;
    lua_setfield(L, -2, "socket");
}
