// pico.socket.*(src/lua/LuaEngine_Socket.cpp)のテスト。127.0.0.1 に立てた偽物
// (socket_fake_servers.hpp)と本物のソケットで、Luaから繋いで往復する。
//
//   権限が無ければ nil, 理由 / 引数の誤りはエラー / 種類違いの操作はエラー
//   TCP(open → data → 相手が閉じて close) / UDP(sendto → data, 送り元) /
//   WebSocket(open → message → close の番号と理由) / MQTT(connect → subscribed → message → published → close)
//   同時に開ける数の上限 / コールバックの中で閉じる / アプリを閉じると挨拶して閉じる / バックグラウンドには無い
//
// 部品そのものの細かい振る舞い(枠・パケット・時間切れ)は socket_test の担当。
#include "lua/LuaEngine.hpp"
#include "system/Log_Functions.hpp"
#include "gui/core/GFX_Functions.hpp"
#include "gui/core/Keyboard_Functions.hpp"
#include "gui/widgets/interfaces/ITextInputTarget.hpp"
#include "gui/widgets/Widget.hpp"
#include "gui/core/Widget_Functions.hpp"
#include "socket_fake_servers.hpp"

#include <Arduino.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// ---- モック(lua_ext_test.cppと同じ方針) ----
void PICO_GFX::MarkDirty(const Rect&){}
void PICO_GFX::Setup(){}
void PICO_GFX::FlushDirty(){}
void PICO_GFX::DrawDialogBackground(){}
void LogFunctions::Log(LogType, const char* fmt, ...){
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

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}
static int l_check(lua_State* L){
    const bool cond = lua_toboolean(L, 1);
    const char* label = luaL_optstring(L, 2, "(no label)");
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
    return 0;
}

static void SetInt(LuaEngine& e, const char* name, int v){
    lua_pushinteger(e.raw(), v);
    lua_setglobal(e.raw(), name);
}

static bool GlobalTrue(LuaEngine& e, const char* name){
    lua_getglobal(e.raw(), name);
    const bool v = lua_toboolean(e.raw(), -1);
    lua_pop(e.raw(), 1);
    return v;
}

// 時計を進めつつ、名前の付いたLuaのグローバルが真になるまで回す
static bool PumpUntil(LuaEngine& e, const char* flag, int rounds = 4000){
    for(int i = 0; i < rounds; i++){
        if(GlobalTrue(e, flag)) return true;
        e.UpdateSockets();
        PicoHostClock::now += 1;
        usleep(200);
    }
    return GlobalTrue(e, flag);
}

int main(){
    FakeServers::FakeTcpEcho tcp;
    FakeServers::FakeUdpEcho udp;
    FakeServers::FakeWsServer ws;
    FakeServers::FakeMqttBroker mqtt;
    check(tcp.start() && udp.start() && ws.start() && mqtt.start(), "偽物の相手を立てる");

    LuaPermissions net;
    net.network = true;

    // ---- 権限・引数 ----
    {
        LuaEngine e(256 * 1024, LuaPermissions{}, "/sapp");
        lua_register(e.raw(), "check", l_check);
        const bool ok = e.Run(R"LUA(
            local id, err = pico.socket.tcp("127.0.0.1", 1, function() end)
            check(id == nil and err == "ネットワークの権限がありません", "権限が無ければ nil, 理由")
            check(pico.socket.mqtt("mqtt://x", function() end) == nil, "mqtt も権限が要る")
            check(not pcall(pico.socket.tcp, "h", 0, function() end), "ポート0はエラー")
            check(not pcall(pico.socket.tcp, "h", 1, "notfn"), "コールバックが無ければエラー")
            check(pico.socket.state(1) == nil, "知らないidの状態は nil")
            check(pico.socket.close(1) == false, "知らないidは閉じられない")
        )LUA", "perm");
        check(ok, "権限の確認の実行");
    }
    {
        LuaEngine e(256 * 1024, net, "/sapp", true);
        lua_register(e.raw(), "check", l_check);
        const bool ok = e.Run(R"LUA(
            check(pico.socket == nil, "バックグラウンドには pico.socket が無い")
        )LUA", "bg");
        check(ok, "バックグラウンドの確認の実行");
    }

    // ---- TCP・UDP ----
    {
        LuaEngine e(256 * 1024, net, "/sapp");
        lua_register(e.raw(), "check", l_check);
        SetInt(e, "TCP_PORT", tcp.port);
        SetInt(e, "UDP_PORT", udp.port);
        bool ok = e.Run(R"LUA(
            got = ""
            events = {}
            t = pico.socket.tcp("127.0.0.1", TCP_PORT, function(ev, a)
                events[#events + 1] = ev
                if ev == "open" then
                    check(pico.socket.state(t) == "open", "tcp: open の中では状態が open")
                    check(pico.socket.send(t, "hello\n"), "tcp: 送る")
                elseif ev == "data" then
                    got = got .. a
                    if got == "hello\n" then pico.socket.send(t, "bye\n") end
                elseif ev == "close" then
                    check(a == nil, "tcp: 相手が閉じたときの理由は nil")
                    tcp_closed = true
                end
            end)
            check(type(t) == "number", "tcp: id が返る")
            check(pico.socket.state(t) == "connecting", "tcp: 最初は connecting")
            check(pico.socket.send(t, "x") == false, "tcp: 繋がる前は送れない")
            check(not pcall(pico.socket.publish, t, "a", "b"), "tcp: publish は種類違いでエラー")
            check(not pcall(pico.socket.sendto, t, "h", 1, "b"), "tcp: sendto は種類違いでエラー")

            u = pico.socket.udp(function(ev, data, host, port)
                if ev == "data" then
                    check(data == "echo:ping" and host == "127.0.0.1" and port == UDP_PORT, "udp: 返事と送り元")
                    udp_ok = true
                end
            end)
            check(type(u) == "number" and pico.socket.state(u) == "open", "udp: すぐ使える")
            check(pico.socket.sendto(u, "127.0.0.1", UDP_PORT, "ping"), "udp: 送る")
            check(not pcall(pico.socket.send, u, "x"), "udp: send は種類違いでエラー")
        )LUA", "tcp");
        check(ok, "TCP・UDPの実行");
        check(PumpUntil(e, "tcp_closed"), "tcp: 往復して相手が閉じる");
        check(PumpUntil(e, "udp_ok"), "udp: 往復");
        ok = e.Run(R"LUA(
            check(got == "hello\nbye\n", "tcp: 届いたもの")
            check(events[1] == "open" and events[#events] == "close", "tcp: open が先、close が最後")
            check(pico.socket.state(t) == nil, "tcp: close の後は id が消える")
            check(pico.socket.close(u) == true, "udp: 閉じる")
            check(pico.socket.close(u) == false, "udp: 2回目は閉じ始めない")
            check(pico.socket.state(u) == "closed", "udp: 閉じた")
        )LUA", "tcp2");
        check(ok, "TCP・UDPの後始末の実行");
        e.UpdateSockets();
        ok = e.Run(R"LUA(
            check(pico.socket.state(u) == nil, "udp: close を渡した後は id が消える")
            local bad, err = pico.socket.tcp("127.0.0.1", 1, function(ev, a)
                if ev == "close" then refused_err = a end
            end)
            check(type(bad) == "number", "tcp: 繋がらない相手でもまず id")
        )LUA", "tcp3");
        check(ok, "TCPの失敗の実行");
        for(int i = 0; i < 5; i++) e.UpdateSockets();
        ok = e.Run(R"LUA(
            check(refused_err == "接続できない", "tcp: 繋がらなければ close に理由")
        )LUA", "tcp4");
        check(ok, "TCPの失敗の確認");
    }

    // ---- WebSocket ----
    {
        LuaEngine e(256 * 1024, net, "/sapp");
        lua_register(e.raw(), "check", l_check);
        SetInt(e, "WS_PORT", ws.port);
        bool ok = e.Run(R"LUA(
            msgs = {}
            w = pico.socket.websocket("ws://127.0.0.1:" .. WS_PORT .. "/ws", function(ev, a, b, c)
                if ev == "open" then
                    check(a == "chat", "ws: open にサブプロトコル")
                    check(pico.socket.send(w, "abc"), "ws: テキストを送る")
                    check(pico.socket.send(w, "\0\1", {binary = true}), "ws: バイナリを送る")
                elseif ev == "message" then
                    msgs[#msgs + 1] = (b and "bin:" or "text:") .. a
                    if #msgs == 3 then pico.socket.close(w) end
                elseif ev == "close" then
                    check(a == nil and b == 1000, "ws: こちらから閉じたら err=nil, 1000")
                    ws_closed = true
                end
            end, {protocol = "chat", headers = {["Authorization"] = "Bearer abc"}, ping_interval = 0})
            check(type(w) == "number", "ws: id が返る")
            check(not pcall(pico.socket.websocket, "ws://x", function() end, {headers = 1}), "ws: headers の型違いはエラー")
            check(not pcall(pico.socket.websocket, "ws://x", function() end, {max_message = 10}), "ws: max_message が小さすぎるとエラー")
            local bad, err = pico.socket.websocket("http://x/", function() end)
            check(bad == nil and err ~= nil, "ws: ws:// でなければ nil, 理由")
        )LUA", "ws");
        check(ok, "WebSocketの実行");
        check(PumpUntil(e, "ws_closed"), "ws: 開いて往復して閉じる");
        ok = e.Run(R"LUA(
            check(msgs[1] == "text:hello" and msgs[2] == "text:abc" and msgs[3] == "bin:\0\1", "ws: 届いたメッセージ")
        )LUA", "ws2");
        check(ok, "WebSocketの確認");
        {
            std::lock_guard<std::mutex> lk(ws.mu);
            check(ws.last_request.find("Authorization: Bearer abc\r\n") != std::string::npos, "ws: 足したヘッダが届く");
        }
        // 開いたままアプリを閉じる → 1001 で閉じる挨拶
        ok = e.Run(R"LUA(
            w2 = pico.socket.websocket("ws://127.0.0.1:" .. WS_PORT .. "/", function(ev)
                if ev == "open" then w2_open = true end
            end)
        )LUA", "ws3");
        check(ok && PumpUntil(e, "w2_open"), "ws: 2本目を開く");
    }
    {
        bool saw = false;
        for(int i = 0; i < 3000 && !saw; i++){ saw = ws.saw("close:1001:"); usleep(500); }
        check(saw, "ws: アプリを閉じると 1001 で閉じる挨拶を送る");
    }

    // ---- MQTT ----
    int disc_before = 0;
    {
        LuaEngine e(256 * 1024, net, "/sapp");
        lua_register(e.raw(), "check", l_check);
        SetInt(e, "MQTT_PORT", mqtt.port);
        bool ok = e.Run(R"LUA(
            log = {}
            m = pico.socket.mqtt("mqtt://127.0.0.1:" .. MQTT_PORT, function(ev, a, b, c)
                if ev == "connect" then
                    check(a == false, "mqtt: session_present は false")
                    sub_id = pico.socket.subscribe(m, "home/+/state", 1)
                    check(type(sub_id) == "number" and sub_id > 0, "mqtt: subscribe は識別子")
                elseif ev == "subscribed" then
                    check(a == sub_id and b == 1, "mqtt: subscribed(識別子と認められたQoS)")
                    pub_id = pico.socket.publish(m, "home/light/state", "on", {qos = 1, retain = true})
                    check(type(pub_id) == "number" and pub_id > 0, "mqtt: QoS 1 の publish は識別子")
                    check(pico.socket.publish(m, "home/x", "y") == 0, "mqtt: QoS 0 の publish は0")
                elseif ev == "published" then
                    check(a == pub_id, "mqtt: published の識別子")
                    published = true
                elseif ev == "message" then
                    log[#log + 1] = a .. "=" .. b .. ":" .. c.qos .. ":" .. tostring(c.retain)
                    got_msg = true
                elseif ev == "close" then
                    check(a == nil, "mqtt: close() で閉じたら err=nil")
                    mqtt_closed = true
                end
            end, {client_id = "luapico", username = "u", password = "p", keepalive = 30,
                  will = {topic = "luapico/status", payload = "offline", retain = true}})
            check(type(m) == "number", "mqtt: id が返る")
            check(not pcall(pico.socket.subscribe, m, "a/#/b"), "mqtt: 形の違う話題はエラー")
            check(not pcall(pico.socket.publish, m, "a/+", "x"), "mqtt: publish にワイルドカードはエラー")
            check(not pcall(pico.socket.publish, m, "a", "x", {qos = 2}), "mqtt: publish の QoS 2 はエラー")
            check(not pcall(pico.socket.mqtt, "mqtt://x", function() end, {clean = false}), "mqtt: clean=false に client_id が無ければエラー")
            check(not pcall(pico.socket.mqtt, "mqtt://x", function() end, {will = {payload = "x"}}), "mqtt: will.topic が無ければエラー")
            check(pico.socket.publish(m, "a", "b") == false, "mqtt: 繋がる前の publish は false")
        )LUA", "mqtt");
        check(ok, "MQTTの実行");
        check(PumpUntil(e, "published"), "mqtt: 繋いで購読して送る");
        check(PumpUntil(e, "got_msg"), "mqtt: 購読した話題が届く");
        check(mqtt.saw("connect:luapico:u:p:ka30:clean:will=luapico/status=offline"), "mqtt: CONNECT の中身");
        check(mqtt.saw("pub:home/light/state:1:retain:on"), "mqtt: retain つきで届く");
        ok = e.Run(R"LUA(
            check(log[1] == "home/light/state=on:1:false", "mqtt: message(話題・中身・qos・retain)")
            check(pico.socket.state(m) == "open", "mqtt: 繋がっている")
            check(pico.socket.close(m) == true, "mqtt: 閉じる")
        )LUA", "mqtt2");
        check(ok, "MQTTの確認");
        check(PumpUntil(e, "mqtt_closed"), "mqtt: close が来る");
        check(mqtt.saw("disconnect"), "mqtt: DISCONNECT を送る");

        // 上限: 4本まで
        ok = e.Run(R"LUA(
            local ids = {}
            for i = 1, 4 do ids[i] = pico.socket.udp(function() end) end
            local fifth, err = pico.socket.udp(function() end)
            check(ids[4] ~= nil and fifth == nil and err == "同時に開けるソケットの数を超えています", "上限: 5本目は断る")
            for i = 1, 4 do pico.socket.close(ids[i]) end
        )LUA", "limit");
        check(ok, "上限の実行");
        e.UpdateSockets();
        ok = e.Run(R"LUA(
            check(pico.socket.udp(function() end) ~= nil, "上限: 閉じれば また開ける")
        )LUA", "limit2");
        check(ok, "上限の後の実行");

        // コールバックのエラーはアプリを止めず、次のイベントも届く
        ok = e.Run(R"LUA(
            n_err = 0
            m2 = pico.socket.mqtt("mqtt://127.0.0.1:" .. MQTT_PORT, function(ev)
                if ev == "connect" then m2_open = true; error("わざと") end
            end, {client_id = "m2", keepalive = 0})
        )LUA", "cb_err");
        check(ok && PumpUntil(e, "m2_open"), "コールバックのエラーの後も動く");
        disc_before = mqtt.count("disconnect");
    }
    {
        // アプリを閉じると(LuaEngineを壊すと)DISCONNECT を送る
        const int before = disc_before;
        bool saw = false;
        for(int i = 0; i < 3000 && !saw; i++){ saw = mqtt.count("disconnect") > before; usleep(500); }
        check(saw, "mqtt: アプリを閉じると DISCONNECT");
    }

    tcp.stop();
    udp.stop();
    ws.stop();
    mqtt.stop();
    WidgetFunctions::ClearSceneWidgets();
    printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
