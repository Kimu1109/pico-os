#pragma once
// socket_test / lua_socket_test が使う、127.0.0.1 に立てる小さな相手(別スレッド)。
//
//   FakeTcpEcho  … 受けたものをそのまま返すTCP。"bye\n" を受けたら返してから閉じる
//   FakeSilent   … 受けるだけで何も答えないTCP
//   FakeUdpEcho  … 受けたデータグラムを先頭に "echo:" を付けて返す
//   FakeWsServer … WebSocketの開始の挨拶に答え、テキスト/バイナリをそのまま返す(8KiBまで)。
//                  "ping-me" → こちらからpingを送る / "close-me" → 1000 "bye" で閉じる /
//                  "big" → 2000バイトを2つの枠に分けて送る / "huge" → 70000バイトを送る(上限超え)
//   FakeMqttBroker … CONNECT(username "u" なら password "p" だけ通す)・SUBSCRIBE(最大QoS 1)・
//                  PUBLISH(QoS 1にはPUBACK。購読と合えばQoS 1で送り返す)・PINGREQ・DISCONNECT
//
// どれも1本ずつ接続を受ける(同時の接続は順番待ち)。stop() で止める。
// 受けた中身は log へ文字列で残す(テストが後で確かめる)。

#include "net/Ws_Frame.hpp"

#include <arpa/inet.h>
#include <atomic>
#include <cstring>
#include <mutex>
#include <netinet/in.h>
#include <string>
#include <sys/select.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace FakeServers {

    inline bool WaitReadable(int fd, const std::atomic<bool>& stop){
        while(!stop){
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(fd, &fds);
            timeval tv{0, 20000};
            if(select(fd + 1, &fds, nullptr, nullptr, &tv) > 0) return true;
        }
        return false;
    }

    // n バイトそろうまで読む(止められたか切れたら false)
    inline bool ReadAll(int fd, void* buf, size_t n, const std::atomic<bool>& stop){
        uint8_t* p = (uint8_t*)buf;
        size_t got = 0;
        while(got < n){
            if(!WaitReadable(fd, stop)) return false;
            const ssize_t r = recv(fd, p + got, n - got, 0);
            if(r <= 0) return false;
            got += (size_t)r;
        }
        return true;
    }

    inline void SendAll(int fd, const void* buf, size_t n){
        const uint8_t* p = (const uint8_t*)buf;
        while(n > 0){
            const ssize_t r = send(fd, p, n, MSG_NOSIGNAL);
            if(r <= 0) return;
            p += r;
            n -= (size_t)r;
        }
    }

    class Base {
        public:
            int port = 0;
            std::atomic<bool> stop_flag{false};
            std::mutex mu;
            std::vector<std::string> log;

            void note(const std::string& s){ std::lock_guard<std::mutex> lk(mu); log.push_back(s); }
            bool saw(const std::string& s){
                std::lock_guard<std::mutex> lk(mu);
                for(const auto& l : log) if(l == s) return true;
                return false;
            }
            int count(const std::string& s){
                std::lock_guard<std::mutex> lk(mu);
                int n = 0;
                for(const auto& l : log) if(l == s) n++;
                return n;
            }
            void stop(){
                stop_flag = true;
                if(th_.joinable()) th_.join();
                if(fd_ >= 0) close(fd_);
                fd_ = -1;
            }
            virtual ~Base(){ stop(); }

        protected:
            int fd_ = -1;
            std::thread th_;

            bool listenOn(int type){
                fd_ = socket(AF_INET, type, 0);
                if(fd_ < 0) return false;
                int one = 1;
                setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
                sockaddr_in addr{};
                addr.sin_family = AF_INET;
                addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                if(bind(fd_, (sockaddr*)&addr, sizeof(addr)) != 0) return false;
                if(type == SOCK_STREAM && listen(fd_, 4) != 0) return false;
                socklen_t len = sizeof(addr);
                getsockname(fd_, (sockaddr*)&addr, &len);
                port = ntohs(addr.sin_port);
                return true;
            }

            // TCPの接続を1本ずつ受けて serve() へ渡す
            bool startTcp(){
                if(!listenOn(SOCK_STREAM)) return false;
                th_ = std::thread([this]{
                    while(!stop_flag){
                        if(!WaitReadable(fd_, stop_flag)) break;
                        const int c = accept(fd_, nullptr, nullptr);
                        if(c < 0) continue;
                        serve(c);
                        close(c);
                    }
                });
                return true;
            }
            virtual void serve(int){}
    };

    class FakeTcpEcho : public Base {
        public:
            bool start(){ return startTcp(); }
        protected:
            void serve(int c) override {
                note("accept");
                std::string seen;
                char buf[512];
                while(WaitReadable(c, stop_flag)){
                    const ssize_t n = recv(c, buf, sizeof(buf), 0);
                    if(n <= 0) break;
                    SendAll(c, buf, (size_t)n);
                    seen.append(buf, (size_t)n);
                    if(seen.find("bye\n") != std::string::npos) break;
                }
                note("closed");
            }
    };

    // 受けるだけで何も答えない(時間切れの確認用)
    class FakeSilent : public Base {
        public:
            bool start(){ return startTcp(); }
        protected:
            void serve(int c) override {
                char buf[512];
                while(WaitReadable(c, stop_flag)){
                    if(recv(c, buf, sizeof(buf), 0) <= 0) break;
                }
            }
    };

    class FakeUdpEcho : public Base {
        public:
            bool start(){
                if(!listenOn(SOCK_DGRAM)) return false;
                th_ = std::thread([this]{
                    while(!stop_flag){
                        if(!WaitReadable(fd_, stop_flag)) break;
                        char buf[2048];
                        sockaddr_in from{};
                        socklen_t flen = sizeof(from);
                        const ssize_t n = recvfrom(fd_, buf, sizeof(buf), 0, (sockaddr*)&from, &flen);
                        if(n < 0) continue;
                        note(std::string(buf, (size_t)n));
                        std::string out = "echo:" + std::string(buf, (size_t)n);
                        sendto(fd_, out.data(), out.size(), 0, (sockaddr*)&from, flen);
                    }
                });
                return true;
            }
    };

    class FakeWsServer : public Base {
        public:
            // 開始の挨拶への答え方(試験用に壊せる)
            enum class Mode { Normal, WrongAccept, Refuse };
            Mode mode = Mode::Normal;
            std::string last_request;
            bool start(){ return startTcp(); }

        protected:
            static void sendFrame(int c, uint8_t op, const void* data, size_t len, bool fin = true){
                uint8_t head[WsFrame::kMaxHeaderBytes];
                const size_t hn = WsFrame::EncodeHeader(head, (WsFrame::Opcode)op, fin, len, nullptr);
                SendAll(c, head, hn);
                if(len) SendAll(c, data, len);
            }

            void serve(int c) override {
                //開始の挨拶
                std::string req;
                char buf[1024];
                while(req.find("\r\n\r\n") == std::string::npos){
                    if(!WaitReadable(c, stop_flag)) return;
                    const ssize_t n = recv(c, buf, sizeof(buf), 0);
                    if(n <= 0) return;
                    req.append(buf, (size_t)n);
                }
                { std::lock_guard<std::mutex> lk(mu); last_request = req; }
                if(mode == Mode::Refuse){
                    const char* r = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n";
                    SendAll(c, r, strlen(r));
                    return;
                }
                const size_t kp = req.find("Sec-WebSocket-Key: ");
                if(kp == std::string::npos) return;
                const std::string key = req.substr(kp + 19, WsFrame::kKeyChars);
                char accept[WsFrame::kAcceptChars + 1];
                WsFrame::AcceptFor(key.c_str(), accept);
                if(mode == Mode::WrongAccept) accept[0] = (accept[0] == 'A') ? 'B' : 'A';
                std::string res = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                                  "Sec-WebSocket-Accept: ";
                res += accept;
                res += "\r\n";
                if(req.find("Sec-WebSocket-Protocol: chat") != std::string::npos) res += "Sec-WebSocket-Protocol: chat\r\n";
                res += "\r\n";
                //挨拶の返事と最初のメッセージを同じ送信に載せる(クライアントが残りを取りこぼさないか)
                uint8_t head[WsFrame::kMaxHeaderBytes];
                const char* hello = "hello";
                const size_t hn = WsFrame::EncodeHeader(head, WsFrame::Text, true, 5, nullptr);
                res.append((const char*)head, hn);
                res.append(hello, 5);
                SendAll(c, res.data(), res.size());
                note("open");

                while(!stop_flag){
                    uint8_t h[2];
                    if(!ReadAll(c, h, 2, stop_flag)) break;
                    const uint8_t op = h[0] & 0x0F;
                    const bool masked = (h[1] & 0x80) != 0;
                    uint64_t len = h[1] & 0x7F;
                    if(len == 126){
                        uint8_t e[2];
                        if(!ReadAll(c, e, 2, stop_flag)) break;
                        len = ((uint64_t)e[0] << 8) | e[1];
                    }else if(len == 127){
                        uint8_t e[8];
                        if(!ReadAll(c, e, 8, stop_flag)) break;
                        len = 0;
                        for(int i = 0; i < 8; i++) len = (len << 8) | e[i];
                    }
                    if(!masked){ note("unmasked"); break; }
                    uint8_t mask[4];
                    if(!ReadAll(c, mask, 4, stop_flag)) break;
                    std::string payload((size_t)len, '\0');
                    if(len && !ReadAll(c, &payload[0], (size_t)len, stop_flag)) break;
                    WsFrame::ApplyMask((uint8_t*)&payload[0], (size_t)len, mask);

                    if(op == WsFrame::Close){
                        const unsigned code = payload.size() >= 2 ? (((uint8_t)payload[0] << 8) | (uint8_t)payload[1]) : 0;
                        note("close:" + std::to_string(code) + ":" + (payload.size() > 2 ? payload.substr(2) : ""));
                        sendFrame(c, WsFrame::Close, payload.data(), payload.size() >= 2 ? 2 : 0);
                        break;
                    }
                    if(op == WsFrame::Pong){ note("pong:" + payload); continue; }
                    if(op == WsFrame::Ping){ note("ping:" + payload); sendFrame(c, WsFrame::Pong, payload.data(), payload.size()); continue; }
                    note(std::string(op == WsFrame::Binary ? "bin:" : "text:") + payload);
                    if(payload == "ping-me"){
                        sendFrame(c, WsFrame::Ping, "x", 1);
                        sendFrame(c, WsFrame::Text, "pinged", 6);
                    }else if(payload == "close-me"){
                        const uint8_t body[] = {0x03, 0xE8, 'b', 'y', 'e'};
                        sendFrame(c, WsFrame::Close, body, sizeof(body));
                        //返事のcloseを待ってから閉じる
                        uint8_t rh[2];
                        if(ReadAll(c, rh, 2, stop_flag) && (rh[0] & 0x0F) == WsFrame::Close) note("close-echo");
                        break;
                    }else if(payload == "big"){
                        std::string a(1000, 'a'), b(1000, 'b');
                        sendFrame(c, WsFrame::Text, a.data(), a.size(), false);
                        sendFrame(c, WsFrame::Ping, "mid", 3); //分割の途中に制御の枠を挟む
                        sendFrame(c, WsFrame::Continuation, b.data(), b.size(), true);
                    }else if(payload == "huge"){
                        std::string a(70000, 'z');
                        sendFrame(c, WsFrame::Text, a.data(), a.size());
                    }else if(payload == "drop"){
                        break; //closeを送らずに切る
                    }else if(payload.size() <= 8192){ //クライアントの既定の上限(8KiB)を超えるものは返さない
                        sendFrame(c, op, payload.data(), payload.size());
                    }
                }
                note("end");
            }
    };

    class FakeMqttBroker : public Base {
        public:
            bool start(){ return startTcp(); }
            // CONNACKの返事を変える(0 = 受け入れる)
            uint8_t connack_code = 0;

        protected:
            std::vector<std::string> subs_;

            // 段(/区切り)ごとに比べる。+ は1段、# は残り全部
            static bool Match(const std::string& filter, const std::string& topic){
                size_t f = 0, t = 0;
                while(true){
                    const size_t fe = filter.find('/', f);
                    const size_t te = topic.find('/', t);
                    const std::string fs = filter.substr(f, fe == std::string::npos ? std::string::npos : fe - f);
                    if(fs == "#") return true;
                    if(t > topic.size()) return false;
                    const std::string ts = topic.substr(t, te == std::string::npos ? std::string::npos : te - t);
                    if(fs != "+" && fs != ts) return false;
                    if(fe == std::string::npos || te == std::string::npos) return fe == te;
                    f = fe + 1;
                    t = te + 1;
                }
            }

            static std::string Str(const std::string& b, size_t& pos){
                if(pos + 2 > b.size()) return "";
                const size_t n = ((uint8_t)b[pos] << 8) | (uint8_t)b[pos + 1];
                pos += 2;
                std::string s = b.substr(pos, n);
                pos += n;
                return s;
            }

            static void Packet(int c, uint8_t first, const std::string& body){
                std::string out(1, (char)first);
                uint32_t len = (uint32_t)body.size();
                do{
                    uint8_t b = len % 128;
                    len /= 128;
                    if(len) b |= 0x80;
                    out.push_back((char)b);
                }while(len);
                out += body;
                SendAll(c, out.data(), out.size());
            }

            void serve(int c) override {
                subs_.clear();
                while(!stop_flag){
                    uint8_t h;
                    if(!ReadAll(c, &h, 1, stop_flag)) break;
                    uint32_t len = 0, mult = 1;
                    uint8_t b;
                    do{
                        if(!ReadAll(c, &b, 1, stop_flag)) { note("end"); return; }
                        len += (b & 0x7F) * mult;
                        mult *= 128;
                    }while(b & 0x80);
                    std::string body(len, '\0');
                    if(len && !ReadAll(c, &body[0], len, stop_flag)) break;
                    const uint8_t type = h >> 4;
                    size_t pos = 0;
                    if(type == 1){ //CONNECT
                        Str(body, pos);                  //"MQTT"
                        pos++;                           //level
                        const uint8_t flags = (uint8_t)body[pos++];
                        const unsigned ka = ((uint8_t)body[pos] << 8) | (uint8_t)body[pos + 1];
                        pos += 2;
                        const std::string cid = Str(body, pos);
                        std::string will;
                        if(flags & 0x04){ will = Str(body, pos); will += "=" + Str(body, pos); }
                        const std::string user = (flags & 0x80) ? Str(body, pos) : "";
                        const std::string pass = (flags & 0x40) ? Str(body, pos) : "";
                        note("connect:" + cid + ":" + user + ":" + pass + ":ka" + std::to_string(ka)
                             + ((flags & 0x02) ? ":clean" : "") + (will.empty() ? "" : ":will=" + will));
                        uint8_t code = connack_code;
                        if(user == "u" && pass != "p") code = 4;
                        Packet(c, 0x20, std::string("\x00", 1) + (char)code);
                        if(code != 0) break;
                    }else if(type == 8){ //SUBSCRIBE
                        const uint16_t id = (uint16_t)(((uint8_t)body[0] << 8) | (uint8_t)body[1]);
                        pos = 2;
                        const std::string topic = Str(body, pos);
                        const uint8_t qos = (uint8_t)body[pos];
                        subs_.push_back(topic);
                        note("sub:" + topic + ":" + std::to_string(qos));
                        std::string ack;
                        ack.push_back((char)(id >> 8));
                        ack.push_back((char)id);
                        ack.push_back((char)(qos > 1 ? 1 : qos));
                        Packet(c, 0x90, ack);
                    }else if(type == 10){ //UNSUBSCRIBE
                        const std::string id = body.substr(0, 2);
                        pos = 2;
                        const std::string topic = Str(body, pos);
                        for(size_t i = 0; i < subs_.size(); i++) if(subs_[i] == topic){ subs_.erase(subs_.begin() + (long)i); break; }
                        note("unsub:" + topic);
                        Packet(c, 0xB0, id);
                    }else if(type == 3){ //PUBLISH
                        const uint8_t qos = (h >> 1) & 3;
                        const std::string topic = Str(body, pos);
                        std::string id;
                        if(qos){ id = body.substr(pos, 2); pos += 2; }
                        const std::string payload = body.substr(pos);
                        note("pub:" + topic + ":" + std::to_string(qos) + ((h & 1) ? ":retain" : "") + ":" +
                             (payload.size() > 64 ? std::to_string(payload.size()) + "B" : payload));
                        if(qos == 1) Packet(c, 0x40, id);
                        for(const auto& f : subs_){
                            if(!Match(f, topic)) continue;
                            std::string out;
                            out.push_back((char)(topic.size() >> 8));
                            out.push_back((char)topic.size());
                            out += topic;
                            out.push_back(0);
                            out.push_back(7);   //パケット識別子7でQoS 1として送り返す
                            out += payload;
                            Packet(c, 0x32, out);
                            break;
                        }
                    }else if(type == 4){ //PUBACK
                        const unsigned id = ((uint8_t)body[0] << 8) | (uint8_t)body[1];
                        note("puback:" + std::to_string(id));
                    }else if(type == 12){ //PINGREQ
                        note("ping");
                        Packet(c, 0xD0, "");
                    }else if(type == 14){ //DISCONNECT
                        note("disconnect");
                        break;
                    }
                }
                note("end");
            }
    };
}
