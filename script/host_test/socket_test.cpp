// ソケットの部品(crypto/Sha1・net/Ws_Frame・net/WebSocket_Client・net/Mqtt_Packet・net/Mqtt_Client・
// net/Udp_Socket)のテスト。
//
//   前半 … 通信しない読み書き(SHA-1の試験値、RFC 6455 の Accept の例、枠/パケットの組み立てと、
//          1バイトずつ食わせても同じに読めること、規約違反の拒否)
//   後半 … 127.0.0.1 に立てた偽物(socket_fake_servers.hpp)と本物のソケットで往復する
//          (WebSocketの開始の挨拶・ping・分割・閉じ方、MQTTの購読・QoS 1・遺言・認証、UDP)
//
// millis() はテストが進める時計(stubs/Arduino.h)なので、タイムアウトも待たずに確かめられる。
#include "crypto/Sha1.hpp"
#include "net/Ws_Frame.hpp"
#include "net/WebSocket_Client.hpp"
#include "net/Mqtt_Packet.hpp"
#include "net/Mqtt_Client.hpp"
#include "net/Udp_Socket.hpp"
#include "system/Log_Functions.hpp"
#include "socket_fake_servers.hpp"

#include <Arduino.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

void LogFunctions::Log(LogType, const char* fmt, ...){
    if(!getenv("SOCKET_TEST_VERBOSE")) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

static std::string Hex(const uint8_t* p, size_t n){
    static const char* d = "0123456789abcdef";
    std::string s;
    for(size_t i = 0; i < n; i++){ s += d[p[i] >> 4]; s += d[p[i] & 15]; }
    return s;
}

static std::string Sha1Hex(const std::string& in){
    Sha1 s;
    s.update(in.data(), in.size());
    uint8_t out[20];
    s.finish(out);
    return Hex(out, 20);
}

// 時計を進めながら少し待つ(ソケットの向こうのスレッドに番を回す)
static void Tick(unsigned long ms = 1){
    PicoHostClock::now += ms;
    usleep(200);
}

// ---------------- WebSocketの読み書き ----------------

struct WsFeed {
    std::vector<std::string> events;
    uint8_t buf[4096];
    WsFrame::Parser p;
    WsFeed(){ p.reset(buf, sizeof(buf)); }
    void feed(const std::string& bytes, size_t step){
        size_t pos = 0;
        while(pos < bytes.size()){
            const size_t n = std::min(step, bytes.size() - pos);
            size_t off = 0;
            while(off < n){
                size_t used = 0;
                const auto r = p.feed((const uint8_t*)bytes.data() + pos + off, n - off, used);
                off += used;
                switch(r){
                    case WsFrame::Parser::Result::Message:
                        events.push_back(std::string(p.binary() ? "bin:" : "text:") + std::string((const char*)p.data(), p.size()));
                        break;
                    case WsFrame::Parser::Result::Ping:
                        events.push_back("ping:" + std::string((const char*)p.control(), p.controlSize()));
                        break;
                    case WsFrame::Parser::Result::Pong:
                        events.push_back("pong");
                        break;
                    case WsFrame::Parser::Result::Close:
                        events.push_back("close:" + std::to_string(p.closeCode()) + ":" +
                                         std::string((const char*)p.control(), p.controlSize()));
                        break;
                    case WsFrame::Parser::Result::Error:
                        events.push_back("error:" + std::to_string(p.errorCode()));
                        return;
                    case WsFrame::Parser::Result::NeedMore:
                        break;
                }
            }
            pos += n;
        }
    }
};

static std::string Frame(uint8_t op, const std::string& payload, bool fin = true, const uint8_t* mask = nullptr){
    uint8_t h[WsFrame::kMaxHeaderBytes];
    const size_t n = WsFrame::EncodeHeader(h, (WsFrame::Opcode)op, fin, payload.size(), mask);
    std::string s((const char*)h, n);
    std::string body = payload;
    if(mask) WsFrame::ApplyMask((uint8_t*)&body[0], body.size(), mask);
    return s + body;
}

static void TestCodecs(){
    // ---- SHA-1(FIPS 180-2 の試験値) ----
    check(Sha1Hex("") == "da39a3ee5e6b4b0d3255bfef95601890afd80709", "SHA-1: 空");
    check(Sha1Hex("abc") == "a9993e364706816aba3e25717850c26c9cd0d89d", "SHA-1: abc");
    check(Sha1Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "84983e441c3bd26ebaae4aa1f95129e5e54670f1", "SHA-1: 448ビット(2ブロックにまたがる)");
    {
        Sha1 s;
        const std::string chunk(1000, 'a');
        for(int i = 0; i < 1000; i++) s.update(chunk.data(), chunk.size());
        uint8_t out[20];
        s.finish(out);
        check(Hex(out, 20) == "34aa973cd4c4daa4f61eeb2bdbad27316534016f", "SHA-1: 'a'を100万個(分けて食わせる)");
    }

    // ---- 開始の挨拶(RFC 6455 1.3 の例) ----
    {
        char accept[WsFrame::kAcceptChars + 1];
        WsFrame::AcceptFor("dGhlIHNhbXBsZSBub25jZQ==", accept);
        check(strcmp(accept, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") == 0, "WebSocket: RFCの例のAccept");
        const uint8_t nonce[16] = {'t','h','e',' ','s','a','m','p','l','e',' ','n','o','n','c','e'};
        char key[WsFrame::kKeyChars + 1];
        WsFrame::MakeKey(nonce, key);
        check(strcmp(key, "dGhlIHNhbXBsZSBub25jZQ==") == 0, "WebSocket: 鍵は16バイトのbase64");
    }

    // ---- 枠の頭 ----
    {
        uint8_t h[WsFrame::kMaxHeaderBytes];
        const uint8_t mask[4] = {1, 2, 3, 4};
        check(WsFrame::EncodeHeader(h, WsFrame::Text, true, 5, nullptr) == 2 && h[0] == 0x81 && h[1] == 5, "枠: 短い(2バイト)");
        check(WsFrame::EncodeHeader(h, WsFrame::Text, true, 125, mask) == 6 && h[1] == (0x80 | 125), "枠: 125バイトはマスク付きで6バイト");
        check(WsFrame::EncodeHeader(h, WsFrame::Binary, true, 126, nullptr) == 4 && h[1] == 126 && h[2] == 0 && h[3] == 126,
              "枠: 126バイトから16ビットの長さ");
        check(WsFrame::EncodeHeader(h, WsFrame::Binary, false, 65536, mask) == 14 && h[0] == 0x02 && h[1] == (0x80 | 127)
              && h[7] == 1 && h[8] == 0, "枠: 65536バイトから64ビットの長さ(FIN=0)");
        uint8_t data[6] = {'a', 'b', 'c', 'd', 'e', 'f'};
        WsFrame::ApplyMask(data, 3, mask);
        WsFrame::ApplyMask(data + 3, 3, mask, 3);
        WsFrame::ApplyMask(data, 6, mask);
        check(memcmp(data, "abcdef", 6) == 0, "枠: マスクは分けてかけても同じ(2回で元に戻る)");
    }

    // ---- 読み取り(1バイトずつでも、まとめてでも同じ) ----
    {
        std::string all;
        all += Frame(WsFrame::Text, "Hello");
        all += Frame(WsFrame::Text, "Hel", false);
        all += Frame(WsFrame::Ping, "p1");                  //分割の途中の制御の枠
        all += Frame(WsFrame::Continuation, "lo!", true);
        all += Frame(WsFrame::Binary, std::string("\x00\x01\x02", 3));
        all += Frame(WsFrame::Text, std::string(300, 'x'));  //16ビットの長さ
        all += Frame(WsFrame::Text, "");                     //空のメッセージ
        all += Frame(WsFrame::Pong, "");
        all += Frame(WsFrame::Close, std::string("\x03\xe9", 2) + "away");
        for(size_t step : {(size_t)1, (size_t)7, all.size()}){
            WsFeed f;
            f.feed(all, step);
            const std::vector<std::string> want = {
                "text:Hello", "ping:p1", "text:Hello!", std::string("bin:\x00\x01\x02", 7), "text:" + std::string(300, 'x'),
                "text:", "pong", "close:1001:away",
            };
            char label[64];
            snprintf(label, sizeof(label), "枠の読み取り: %u バイトずつ", (unsigned)step);
            check(f.events == want, label);
        }
    }
    {
        WsFeed f;
        f.feed(Frame(WsFrame::Close, ""), 1);
        check(f.events.size() == 1 && f.events[0] == "close:1005:", "枠の読み取り: 番号の無いcloseは1005");
    }
    {
        WsFeed f;
        const uint8_t mask[4] = {9, 9, 9, 9};
        f.feed(Frame(WsFrame::Text, "x", true, mask), 100);
        check(f.events.size() == 1 && f.events[0] == "error:1002", "枠の読み取り: サーバからのマスク付きは規約違反");
    }
    {
        WsFeed f;
        f.feed(Frame(WsFrame::Continuation, "x"), 100);
        check(f.events.size() == 1 && f.events[0] == "error:1002", "枠の読み取り: 始まりの無い継続は規約違反");
    }
    {
        WsFeed f;
        f.feed(Frame(WsFrame::Text, "a", false) + Frame(WsFrame::Text, "b"), 100);
        check(f.events.size() == 1 && f.events[0] == "error:1002", "枠の読み取り: 分割の途中に次のメッセージは規約違反");
    }
    {
        WsFeed f;
        f.feed(Frame(WsFrame::Ping, std::string(126, 'p')), 200);
        check(f.events.size() == 1 && f.events[0] == "error:1002", "枠の読み取り: 126バイトのpingは規約違反");
    }
    {
        WsFeed f;
        f.feed(Frame(WsFrame::Text, std::string(3000, 'a'), false) + Frame(WsFrame::Continuation, std::string(2000, 'b')), 512);
        check(f.events.size() == 1 && f.events[0] == "error:1009", "枠の読み取り: 溜め場所(4KiB)を超える分割は1009");
    }
    {
        WsFeed f;
        std::string bad = Frame(WsFrame::Text, "x");
        bad[0] = (char)(bad[0] | 0x40); //RSV1
        f.feed(bad, 100);
        check(f.events.size() == 1 && f.events[0] == "error:1002", "枠の読み取り: RSVの立った枠は規約違反");
    }

    // ---- MQTT ----
    {
        uint8_t b[4];
        check(MqttPacket::EncodeRemaining(b, 0) == 1 && b[0] == 0, "MQTT: 残りの長さ 0");
        check(MqttPacket::EncodeRemaining(b, 127) == 1 && b[0] == 127, "MQTT: 残りの長さ 127");
        check(MqttPacket::EncodeRemaining(b, 128) == 2 && b[0] == 0x80 && b[1] == 1, "MQTT: 残りの長さ 128");
        check(MqttPacket::EncodeRemaining(b, 16383) == 2 && b[0] == 0xFF && b[1] == 0x7F, "MQTT: 残りの長さ 16383");
        check(MqttPacket::EncodeRemaining(b, 16384) == 3, "MQTT: 残りの長さ 16384 は3バイト");
        check(MqttPacket::EncodeRemaining(b, 268435455) == 4 && b[3] == 0x7F, "MQTT: 残りの長さの最大は4バイト");
    }
    {
        MqttPacket::ConnectOptions o;
        o.client_id = "pico";
        o.keepalive_s = 60;
        uint8_t b[64];
        const size_t n = MqttPacket::EncodeConnect(b, sizeof(b), o);
        const uint8_t want[] = {0x10, 16, 0, 4, 'M', 'Q', 'T', 'T', 4, 0x02, 0, 60, 0, 4, 'p', 'i', 'c', 'o'};
        check(n == sizeof(want) && memcmp(b, want, n) == 0, "MQTT: CONNECT(clean・keepalive 60)のバイト列");
        o.username = "u";
        o.password = "pw";
        o.will_topic = "st";
        const uint8_t will[] = {'o', 'f', 'f'};
        o.will_payload = will;
        o.will_len = 3;
        o.will_qos = 1;
        o.will_retain = true;
        const size_t n2 = MqttPacket::EncodeConnect(b, sizeof(b), o);
        check(n2 == 18 + 4 + 5 + 3 + 4 && b[9] == (0x80 | 0x40 | 0x20 | 0x08 | 0x04 | 0x02), "MQTT: CONNECT の印(ユーザー・パスワード・遺言)");
        check(MqttPacket::EncodeConnect(b, 20, o) == 0, "MQTT: 入りきらないCONNECTは0");
        MqttPacket::ConnectOptions bad;
        bad.client_id = "";
        bad.clean_session = false;
        check(MqttPacket::EncodeConnect(b, sizeof(b), bad) == 0, "MQTT: 名前の無い持ち越しのセッションは作れない");
        MqttPacket::ConnectOptions nouser;
        nouser.password = "x";
        check(MqttPacket::EncodeConnect(b, sizeof(b), nouser) == 0, "MQTT: ユーザー無しのパスワードは送れない");
    }
    {
        uint8_t b[300];
        const size_t n = MqttPacket::EncodePublishHeader(b, sizeof(b), "a/b", 5, 1, true, false, 0x1234);
        const uint8_t want[] = {0x33, 2 + 3 + 2 + 5, 0, 3, 'a', '/', 'b', 0x12, 0x34};
        check(n == sizeof(want) && memcmp(b, want, n) == 0, "MQTT: PUBLISH の頭(QoS 1・retain)");
        check(MqttPacket::EncodePublishHeader(b, sizeof(b), "a", 0, 1, false, false, 0) == 0, "MQTT: QoS 1 に識別子0は使えない");
        const size_t s = MqttPacket::EncodeSubscribe(b, sizeof(b), 10, "h/#", 1);
        const uint8_t ws[] = {0x82, 8, 0, 10, 0, 3, 'h', '/', '#', 1};
        check(s == sizeof(ws) && memcmp(b, ws, s) == 0, "MQTT: SUBSCRIBE のバイト列");
        const size_t u = MqttPacket::EncodeUnsubscribe(b, sizeof(b), 11, "h/#");
        check(u == 9 && b[0] == 0xA2, "MQTT: UNSUBSCRIBE のバイト列");
        check(MqttPacket::EncodeAck(b, MqttPacket::PUBREL, 5) == 4 && b[0] == 0x62 && b[3] == 5, "MQTT: PUBREL の下位ビットは0010");
        check(MqttPacket::EncodeEmpty(b, MqttPacket::PINGREQ) == 2 && b[0] == 0xC0 && b[1] == 0, "MQTT: PINGREQ");
    }
    {
        check(MqttPacket::ValidTopic("home/light", false), "話題: 普通の話題");
        check(!MqttPacket::ValidTopic("home/+", false), "話題: 送るときはワイルドカードを使えない");
        check(MqttPacket::ValidTopic("home/+/state", true) && MqttPacket::ValidTopic("#", true) && MqttPacket::ValidTopic("a/#", true),
              "話題: 購読はワイルドカード可");
        check(!MqttPacket::ValidTopic("a/#/b", true) && !MqttPacket::ValidTopic("a+", true) && !MqttPacket::ValidTopic("", true),
              "話題: # は最後だけ・段まるごと・空は不可");
        check(!MqttPacket::ValidTopic(std::string(257, 'a').c_str(), false), "話題: 256バイトを超えるものは不可");
    }
    {
        // PUBLISH(QoS 1) + PINGRESP + SUBACK を1バイトずつ
        std::string all;
        all += std::string("\x32\x0a\x00\x03t/1\x00\x07hey", 12);
        all += std::string("\xd0\x00", 2);
        all += std::string("\x90\x03\x00\x0a\x01", 5);
        for(size_t step : {(size_t)1, all.size()}){
            uint8_t buf[64];
            MqttPacket::Parser p;
            p.reset(buf, sizeof(buf));
            std::vector<std::string> got;
            size_t pos = 0;
            while(pos < all.size()){
                const size_t n = std::min(step, all.size() - pos);
                size_t off = 0;
                while(off < n){
                    size_t used = 0;
                    const auto r = p.feed((const uint8_t*)all.data() + pos + off, n - off, used);
                    off += used;
                    if(r != MqttPacket::Parser::Result::Packet) continue;
                    if(p.type() == MqttPacket::PUBLISH){
                        MqttPacket::Publish pub;
                        MqttPacket::ParsePublish(p.flags(), p.body(), p.bodyLen(), pub);
                        got.push_back("pub:" + std::string((const char*)pub.topic, pub.topic_len) + ":" +
                                      std::to_string(pub.qos) + ":" + std::to_string(pub.packet_id) + ":" +
                                      std::string((const char*)pub.payload, pub.payload_len));
                    }else{
                        got.push_back("t" + std::to_string(p.type()) + ":" + std::to_string(p.bodyLen()));
                    }
                }
                pos += n;
            }
            check(got == std::vector<std::string>({"pub:t/1:1:7:hey", "t13:0", "t9:3"}),
                  step == 1 ? "MQTT: 1バイトずつ読む" : "MQTT: まとめて読む");
        }
        // 溜め場所より大きいものは頭だけ残る
        uint8_t small[8];
        MqttPacket::Parser p;
        p.reset(small, sizeof(small));
        const std::string big = std::string("\x30\x14\x00\x03t/1", 7) + std::string(15, 'z');
        size_t used = 0;
        const auto r = p.feed((const uint8_t*)big.data(), big.size(), used);
        check(r == MqttPacket::Parser::Result::Packet && p.truncated() && p.bodyLen() == 8 && used == big.size(),
              "MQTT: 溜め場所を超えたら読み捨てて truncated");
        const uint8_t badlen[] = {0x30, 0xFF, 0xFF, 0xFF, 0xFF};
        p.reset(small, sizeof(small));
        check(p.feed(badlen, sizeof(badlen), used) == MqttPacket::Parser::Result::Error, "MQTT: 5バイトの残りの長さは不正");
    }
}

// ---------------- WebSocketの往復 ----------------

struct WsRun {
    WebSocketClient ws;
    std::vector<std::string> events;
    // falseが返るまで poll() し、イベントを文字列で溜める
    void pump(int rounds = 1){
        for(int i = 0; i < rounds; i++){
            WebSocketClient::Event ev;
            while(ws.poll(ev)){
                switch(ev.type){
                    case WebSocketClient::EventType::Open: events.push_back("open"); break;
                    case WebSocketClient::EventType::Message:
                        events.push_back(std::string(ev.binary ? "bin:" : "text:") + std::string((const char*)ev.data, ev.len));
                        break;
                    case WebSocketClient::EventType::Closed:
                        events.push_back("closed:" + std::to_string(ev.code) + ":" +
                                         std::string((const char*)ev.data, ev.len) + ":" + (ev.error ? ev.error : "-"));
                        break;
                }
            }
            Tick();
        }
    }
    bool waitFor(const std::string& want, int rounds = 3000){
        for(int i = 0; i < rounds; i++){
            for(const auto& e : events) if(e == want) return true;
            pump();
        }
        for(const auto& e : events) if(e == want) return true;
        return false;
    }
    bool waitPrefix(const std::string& prefix, int rounds = 3000){
        for(int i = 0; i < rounds; i++){
            for(const auto& e : events) if(e.compare(0, prefix.size(), prefix) == 0) return true;
            pump();
        }
        return false;
    }
};

static bool WaitSaw(FakeServers::Base& srv, const std::string& s, int rounds = 3000){
    for(int i = 0; i < rounds; i++){
        if(srv.saw(s)) return true;
        usleep(500);
    }
    return srv.saw(s);
}

static void TestWebSocket(){
    FakeServers::FakeWsServer srv;
    check(srv.start(), "WebSocket: 偽物のサーバを立てる");
    const std::string url = "ws://127.0.0.1:" + std::to_string(srv.port) + "/api/websocket?x=1";

    {
        WsRun r;
        check(r.ws.setProtocol("chat") && r.ws.addHeader("Authorization", "Bearer t"), "WebSocket: サブプロトコルとヘッダ");
        check(!r.ws.addHeader("Upgrade", "x") && !r.ws.addHeader("Bad Name", "x") && !r.ws.addHeader("X", "a\r\nb"),
              "WebSocket: 挨拶に要るヘッダ・不正な名前/値は足せない");
        check(r.ws.begin(url.c_str()), "WebSocket: begin");
        check(r.ws.state() == WebSocketClient::State::Connecting, "WebSocket: 最初は Connecting");
        check(r.waitFor("text:hello"), "WebSocket: 開いて、挨拶と同じ送信に載った最初のメッセージを受ける");
        check(r.events.size() >= 2 && r.events[0] == "open", "WebSocket: Open が先");
        check(strcmp(r.ws.protocol(), "chat") == 0, "WebSocket: サーバが選んだサブプロトコル");
        {
            std::lock_guard<std::mutex> lk(srv.mu);
            check(srv.last_request.find("GET /api/websocket?x=1 HTTP/1.1\r\n") == 0, "WebSocket: パスとクエリ");
            check(srv.last_request.find("Authorization: Bearer t\r\n") != std::string::npos, "WebSocket: 足したヘッダが届く");
            check(srv.last_request.find("Sec-WebSocket-Version: 13\r\n") != std::string::npos, "WebSocket: 版は13");
        }
        check(r.ws.sendText("abc", 3), "WebSocket: テキストを送る");
        check(r.waitFor("text:abc"), "WebSocket: 送り返される(マスクして送れている)");
        check(WaitSaw(srv, "text:abc"), "WebSocket: サーバがマスクを外して読める");
        const std::string bin("\x00\xff\x10", 3);
        check(r.ws.sendBinary(bin.data(), bin.size()) && r.waitFor("bin:" + bin), "WebSocket: バイナリの往復");
        const std::string longtext(70000, 'q');
        check(r.ws.sendText(longtext.data(), longtext.size()) && WaitSaw(srv, "text:" + longtext),
              "WebSocket: 64ビットの長さで送る(上限は受けるときだけ)");
        r.ws.sendText("ping-me", 7);
        check(r.waitFor("text:pinged"), "WebSocket: サーバのpingの後のメッセージ");
        check(WaitSaw(srv, "pong:x"), "WebSocket: pingに自動で同じ中身のpongを返す");
        r.ws.sendText("big", 3);
        check(r.waitFor("text:" + std::string(1000, 'a') + std::string(1000, 'b')), "WebSocket: 分割されたメッセージを1つにつなぐ");
        check(WaitSaw(srv, "pong:mid"), "WebSocket: 分割の途中のpingにも答える");
        r.ws.close(1000, "done");
        check(r.ws.state() == WebSocketClient::State::Closing, "WebSocket: close() で Closing");
        check(!r.ws.sendText("x", 1), "WebSocket: 閉じかけは送れない");
        check(r.waitFor("closed:1000::-"), "WebSocket: 相手の返事で Closed(正常)");
        check(WaitSaw(srv, "close:1000:done"), "WebSocket: 番号と理由が届く");
        check(r.ws.state() == WebSocketClient::State::Closed, "WebSocket: 閉じた");
        WebSocketClient::Event ev;
        check(!r.ws.poll(ev), "WebSocket: Closed の後は何も来ない");
    }
    {
        WsRun r;
        r.ws.begin(url.c_str());
        check(r.waitFor("text:hello"), "WebSocket: 2本目を開く");
        r.ws.sendText("close-me", 8);
        check(r.waitFor("closed:1000:bye:-"), "WebSocket: サーバから閉じる(番号と理由を受ける)");
        check(WaitSaw(srv, "close-echo"), "WebSocket: サーバのcloseに返事をする");
    }
    {
        WsRun r;
        r.ws.begin(url.c_str());
        check(r.waitFor("text:hello"), "WebSocket: 3本目を開く");
        const int ends = srv.count("end");
        r.ws.sendText("huge", 4);
        check(r.waitPrefix("closed:1009::"), "WebSocket: 上限(8KiB)を超えるメッセージは1009で閉じる");
        //1009のcloseは送るが、読み残しを抱えたまま閉じるとTCPがRSTを送るので、サーバが読めるとは限らない
        bool ended = false;
        for(int i = 0; i < 3000 && !ended; i++){ ended = srv.count("end") > ends; usleep(500); }
        check(ended, "WebSocket: 閉じるとサーバの側も終わる");
    }
    {
        WsRun r;
        r.ws.begin(url.c_str());
        check(r.waitFor("text:hello"), "WebSocket: 4本目を開く");
        r.ws.sendText("drop", 4);
        check(r.waitFor("closed:1006::接続が切れた"), "WebSocket: closeの無い切断は1006");
    }
    {
        WsRun r;
        r.ws.setPingInterval(1000);
        r.ws.begin(url.c_str());
        check(r.waitFor("text:hello"), "WebSocket: pingの間隔つきで開く");
        for(int i = 0; i < 1200; i++) r.pump();
        check(WaitSaw(srv, "ping:"), "WebSocket: 何も届かない間はこちらからpingを送る");
        check(r.events.back() == "text:hello", "WebSocket: pongが来れば切らない");
        r.ws.abort();
        WebSocketClient::Event ev;
        check(!r.ws.poll(ev) && r.ws.state() == WebSocketClient::State::Idle, "WebSocket: abort() は Closed を出さない");
    }
    srv.stop();

    // 挨拶の失敗
    {
        FakeServers::FakeWsServer bad;
        bad.mode = FakeServers::FakeWsServer::Mode::WrongAccept;
        bad.start();
        WsRun r;
        r.ws.begin(("ws://127.0.0.1:" + std::to_string(bad.port) + "/").c_str());
        check(r.waitFor("closed:1006::サーバの応答の確認(Accept)が合わない"), "WebSocket: Acceptが違えば開かない");
        check(r.events.size() == 1, "WebSocket: 開かずに Closed だけ");
        bad.stop();
    }
    {
        FakeServers::FakeWsServer bad;
        bad.mode = FakeServers::FakeWsServer::Mode::Refuse;
        bad.start();
        WsRun r;
        r.ws.begin(("ws://127.0.0.1:" + std::to_string(bad.port) + "/").c_str());
        check(r.waitFor("closed:1006::サーバに断られた(認証)"), "WebSocket: 403 は認証で断られた");
        bad.stop();
    }
    {
        WsRun r;
        r.ws.begin("ws://127.0.0.1:1/");
        r.pump(5);
        check(r.events.size() == 1 && r.events[0].compare(0, 12, "closed:1006:") == 0, "WebSocket: 繋がらなければ Closed(1006)");
    }
    {
        // 受け取るが101を返さないサーバ → 10秒で諦める
        FakeServers::FakeSilent silent;
        silent.start();
        WsRun r;
        r.ws.begin(("ws://127.0.0.1:" + std::to_string(silent.port) + "/").c_str());
        r.pump(1);
        for(int i = 0; i < 12 && r.events.empty(); i++) r.pump(1), PicoHostClock::now += 1000;
        check(!r.events.empty() && r.events[0].find("応答が無い(開始の挨拶)") != std::string::npos,
              "WebSocket: 挨拶の返事が無ければ時間切れ");
        silent.stop();
    }
    {
        WebSocketClient ws;
        check(!ws.begin("http://example.com/") && !ws.begin("ws://") && !ws.begin(nullptr), "WebSocket: ws:// / wss:// 以外は断る");
    }
}

// ---------------- MQTTの往復 ----------------

struct MqttRun {
    MqttClient c;
    std::vector<std::string> events;
    void pump(int rounds = 1){
        for(int i = 0; i < rounds; i++){
            MqttClient::Event ev;
            while(c.poll(ev)){
                switch(ev.type){
                    case MqttClient::EventType::Connected:
                        events.push_back(std::string("connected:") + (ev.session_present ? "1" : "0"));
                        break;
                    case MqttClient::EventType::Message:
                        events.push_back("msg:" + std::string((const char*)ev.topic, ev.topic_len) + ":" +
                                         std::to_string(ev.qos) + ":" + std::string((const char*)ev.payload, ev.payload_len));
                        break;
                    case MqttClient::EventType::Subscribed:
                        events.push_back("suback:" + std::to_string(ev.packet_id) + ":" + std::to_string(ev.granted_qos));
                        break;
                    case MqttClient::EventType::Unsubscribed:
                        events.push_back("unsuback:" + std::to_string(ev.packet_id));
                        break;
                    case MqttClient::EventType::Published:
                        events.push_back("puback:" + std::to_string(ev.packet_id));
                        break;
                    case MqttClient::EventType::Closed:
                        events.push_back(std::string("closed:") + (ev.error ? ev.error : "-"));
                        break;
                }
            }
            Tick();
        }
    }
    bool waitFor(const std::string& want, int rounds = 3000){
        for(int i = 0; i < rounds; i++){
            for(const auto& e : events) if(e == want) return true;
            pump();
        }
        for(const auto& e : events) if(e == want) return true;
        return false;
    }
};

static void TestMqtt(){
    {
        Url u;
        check(MqttClient::ParseUrl(u, "mqtt://broker.local") && u.port == 1883 && !u.secure && u.host == "broker.local",
              "MQTT: mqtt:// の既定は1883");
        check(MqttClient::ParseUrl(u, "mqtts://b:9999/x") && u.port == 9999 && u.secure, "MQTT: mqtts:// とポート");
        check(MqttClient::ParseUrl(u, "mqtts://b") && u.port == 8883, "MQTT: mqtts:// の既定は8883");
        check(!MqttClient::ParseUrl(u, "http://b") && !MqttClient::ParseUrl(u, "mqtt://"), "MQTT: 他のschemeは断る");
    }

    FakeServers::FakeMqttBroker srv;
    check(srv.start(), "MQTT: 偽物のブローカを立てる");
    const std::string url = "mqtt://127.0.0.1:" + std::to_string(srv.port);
    {
        MqttRun r;
        MqttClient::Options o;
        o.client_id.assign("pico1");
        o.username.assign("u");
        o.password.assign("p");
        o.keepalive_s = 30;
        o.will_topic.assign("pico/status");
        o.will_payload.assign("offline");
        check(r.c.begin(url.c_str(), o), "MQTT: begin");
        check(r.c.publish("a", "x", 1) == -1, "MQTT: 繋がる前は送れない");
        check(r.waitFor("connected:0"), "MQTT: CONNACK で繋がる");
        check(WaitSaw(srv, "connect:pico1:u:p:ka30:clean:will=pico/status=offline"), "MQTT: CONNECT の中身(名前・認証・遺言)");
        const int32_t sid = r.c.subscribe("home/#", 1);
        check(sid > 0, "MQTT: 購読(識別子が返る)");
        check(r.waitFor("suback:" + std::to_string(sid) + ":1"), "MQTT: SUBACK(QoS 1 が認められた)");
        check(r.c.publish("home/light", "on", 2, 0) == 0, "MQTT: QoS 0 は識別子0");
        check(r.waitFor("msg:home/light:1:on"), "MQTT: 購読した話題に届く(ブローカはQoS 1で送る)");
        check(WaitSaw(srv, "puback:7"), "MQTT: 受けたQoS 1にPUBACKを返す");
        const int32_t pid = r.c.publish("home/temp", "21.5", 4, 1, true);
        check(pid > 0 && r.waitFor("puback:" + std::to_string(pid)), "MQTT: 送ったQoS 1のPUBACKで Published");
        check(WaitSaw(srv, "pub:home/temp:1:retain:21.5"), "MQTT: retain の印");
        const std::string big(3000, 'B');
        r.c.publish("home/big", big.data(), big.size(), 0);
        check(r.waitFor("msg:home/big:1:" + big), "MQTT: 2バイトの残りの長さの往復");
        const std::string toobig(5000, 'T');
        r.c.publish("home/too", toobig.data(), toobig.size(), 0);
        for(int i = 0; i < 200 && r.c.dropped() == 0; i++) r.pump();
        check(r.c.dropped() == 1, "MQTT: 溜め場所(4KiB)より大きいものは読み捨てる");
        check(WaitSaw(srv, "puback:7") && r.c.isConnected(), "MQTT: 読み捨てても繋がったまま");
        check(r.c.subscribe("bad/#/x") == -1 && r.c.publish("a/+", "x", 1) == -1, "MQTT: 形の違う話題は送らない");
        const int32_t uid = r.c.unsubscribe("home/#");
        check(uid > 0 && r.waitFor("unsuback:" + std::to_string(uid)), "MQTT: 購読をやめる");
        // keepalive: 30秒何も送らなければ PINGREQ
        for(int i = 0; i < 31; i++){ PicoHostClock::now += 1000; r.pump(); }
        check(WaitSaw(srv, "ping"), "MQTT: keepaliveの間に送るものが無ければ PINGREQ");
        r.pump(20);
        check(r.c.isConnected(), "MQTT: PINGRESP が来れば繋がったまま");
        r.c.disconnect();
        check(r.waitFor("closed:-"), "MQTT: disconnect() で Closed(正常)");
        check(WaitSaw(srv, "disconnect"), "MQTT: DISCONNECT が届く");
    }
    {
        MqttRun r;
        MqttClient::Options o;
        o.client_id.assign("pico2");
        o.username.assign("u");
        o.password.assign("wrong");
        r.c.begin(url.c_str(), o);
        check(r.waitFor("closed:ユーザー名かパスワードが違う"), "MQTT: CONNACK 4 で断られる");
    }
    {
        MqttRun r;
        MqttClient::Options o;
        o.keepalive_s = 2;
        r.c.begin(url.c_str(), o);
        check(r.waitFor("connected:0"), "MQTT: 名前なし(clean)で繋がる");
        check(WaitSaw(srv, "connect::::ka2:clean"), "MQTT: 空のクライアントID");
        // ブローカが黙る: 止めると返事が来ない
        srv.stop();
        for(int i = 0; i < 5 && r.events.back() == "connected:0"; i++){ PicoHostClock::now += 1000; r.pump(); }
        check(r.events.back().compare(0, 7, "closed:") == 0, "MQTT: ブローカが止まると Closed");
    }
    {
        MqttRun r;
        MqttClient::Options o;
        check(r.c.begin("mqtt://127.0.0.1:1", o), "MQTT: 繋がらない相手でも begin は通る");
        r.pump(3);
        check(r.events.size() == 1 && r.events[0].compare(0, 7, "closed:") == 0, "MQTT: 繋がらなければ Closed");
        MqttClient::Options keep;
        keep.clean_session = false;
        check(!r.c.begin("mqtt://127.0.0.1:1", keep), "MQTT: 名前の無い持ち越しのセッションは begin で断る");
    }
}

// ---------------- UDP ----------------

static void TestUdp(){
    FakeServers::FakeUdpEcho srv;
    check(srv.start(), "UDP: 偽物の相手を立てる");
    UdpSocket u;
    check(!u.sendTo("127.0.0.1", (uint16_t)srv.port, "x", 1), "UDP: begin の前は送れない");
    check(u.begin(0), "UDP: 空いている番号で待つ");
    check(u.sendTo("127.0.0.1", (uint16_t)srv.port, "hi", 2), "UDP: 送る");
    uint8_t buf[64];
    char from[UdpSocket::kMaxAddrText];
    uint16_t port = 0;
    int n = -1;
    for(int i = 0; i < 2000 && n < 0; i++){ n = u.receive(buf, sizeof(buf), from, port); usleep(500); }
    check(n == 7 && memcmp(buf, "echo:hi", 7) == 0, "UDP: 返事を受ける");
    check(strcmp(from, "127.0.0.1") == 0 && port == srv.port, "UDP: 送り元のアドレスと番号");
    check(u.receive(buf, sizeof(buf), from, port) == -1, "UDP: 次が無ければ -1");
    std::string big(UdpSocket::kMaxDatagram + 1, 'x');
    check(!u.sendTo("127.0.0.1", (uint16_t)srv.port, big.data(), big.size()), "UDP: 1472バイトを超えるものは送らない");
    u.sendTo("127.0.0.1", (uint16_t)srv.port, "0123456789", 10);
    n = -1;
    for(int i = 0; i < 2000 && n < 0; i++){ n = u.receive(buf, 4, from, port); usleep(500); }
    check(n == 4 && memcmp(buf, "echo", 4) == 0, "UDP: 溜め場所に入らない分は捨てる");
    u.close();
    check(!u.isOpen(), "UDP: 閉じる");
    srv.stop();
}

int main(){
    TestCodecs();
    TestWebSocket();
    TestMqtt();
    TestUdp();
    printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
