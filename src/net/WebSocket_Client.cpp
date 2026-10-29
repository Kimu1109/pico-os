#include "net/WebSocket_Client.hpp"

#include "crypto/Secret_Aead.hpp"
#include "system/Log_Functions.hpp"

#include <Arduino.h>
#include <cstdlib>
#include <cstring>
#include <new>
#include <strings.h>

namespace {
    // 応答ヘッダから name の値を探す(大文字小文字を区別しない)。見つかれば先頭と長さ
    bool FindHeader(const char* head, size_t len, const char* name, const char*& value, size_t& value_len){
        const size_t name_len = strlen(name);
        const char* p = head;
        const char* end = head + len;
        //1行目(ステータス行)は飛ばす
        const char* nl = (const char*)memchr(p, '\n', (size_t)(end - p));
        if(!nl) return false;
        p = nl + 1;
        while(p < end){
            nl = (const char*)memchr(p, '\n', (size_t)(end - p));
            const char* line_end = nl ? nl : end;
            const char* e = line_end;
            if(e > p && e[-1] == '\r') e--;
            if((size_t)(e - p) > name_len && p[name_len] == ':' && strncasecmp(p, name, name_len) == 0){
                const char* v = p + name_len + 1;
                while(v < e && (*v == ' ' || *v == '\t')) v++;
                const char* ve = e;
                while(ve > v && (ve[-1] == ' ' || ve[-1] == '\t')) ve--;
                value = v;
                value_len = (size_t)(ve - v);
                return true;
            }
            if(!nl) break;
            p = nl + 1;
        }
        return false;
    }

    // カンマ区切りの値のどれかが token か(Connection: keep-alive, Upgrade のような書き方がある)
    bool HasToken(const char* v, size_t len, const char* token){
        const size_t tlen = strlen(token);
        size_t i = 0;
        while(i < len){
            while(i < len && (v[i] == ' ' || v[i] == ',')) i++;
            size_t j = i;
            while(j < len && v[j] != ',') j++;
            size_t k = j;
            while(k > i && v[k - 1] == ' ') k--;
            if(k - i == tlen && strncasecmp(v + i, token, tlen) == 0) return true;
            i = j;
        }
        return false;
    }
}

bool WebSocketClient::begin(const char* url, size_t max_message){
    abort();
    if(!url) return false;

    //ws:// / wss:// を http:// / https:// に読み替えて分解する(繋ぎ方は同じ)
    FixedString<PICO_STR_512B> http_url;
    if(strncmp(url, "ws://", 5) == 0){
        if(!http_url.assign("http://") || !http_url.append(url + 5)) return false;
    }else if(strncmp(url, "wss://", 6) == 0){
        if(!http_url.assign("https://") || !http_url.append(url + 6)) return false;
    }else{
        return false;
    }
    if(!UrlTools::Parse(url_, http_url.c_str())) return false;

    if(max_message < kMinMaxMessage) max_message = kMinMaxMessage;
    if(max_message > kMaxMaxMessage) max_message = kMaxMaxMessage;
    msg_ = new (std::nothrow) uint8_t[max_message];
    if(!msg_) return false;
    msg_cap_ = max_message;

    uint8_t nonce[16];
    SecretAead::Random(nonce, sizeof(nonce));
    WsFrame::MakeKey(nonce, key_);

    accepted_protocol_.clear();
    hs_len_ = 0;
    rx_pos_ = rx_len_ = 0;
    ping_waiting_ = false;
    closed_pending_ = false;
    closed_error_ = nullptr;
    closed_reason_len_ = 0;
    state_ = State::Connecting;
    return true;
}

bool WebSocketClient::setProtocol(const char* protocol){
    protocol_.clear();
    if(!protocol || !*protocol) return true;
    for(const char* p = protocol; *p; p++){
        if((unsigned char)*p < 0x21 || *p == 0x7f) {
            if(*p != ' ') return false;
        }
    }
    return protocol_.assign(protocol);
}

bool WebSocketClient::addHeader(const char* name, const char* value){
    if(!name || !*name || !value) return false;
    for(const char* p = name; *p; p++){
        const unsigned char c = (unsigned char)*p;
        const bool tok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
            || c == '-' || c == '_' || c == '.';
        if(!tok) return false;
    }
    for(const char* p = value; *p; p++){
        const unsigned char c = (unsigned char)*p;
        if((c < 0x20 && c != '\t') || c == 0x7f) return false;
    }
    //開始の挨拶そのものに要るものは差し替えさせない
    static const char* const kReserved[] = {
        "host", "upgrade", "connection", "sec-websocket-key", "sec-websocket-version",
        "sec-websocket-protocol", "sec-websocket-extensions", "content-length", "transfer-encoding",
    };
    for(const char* r : kReserved){
        if(strcasecmp(name, r) == 0) return false;
    }
    //途中まで足した半端な行を残さないよう、入りきるかを先に確かめる
    if(extra_headers_.length() + strlen(name) + strlen(value) + 4 >= kMaxExtraHeaderBytes) return false;
    extra_headers_.append(name);
    extra_headers_.append(": ");
    extra_headers_.append(value);
    extra_headers_.append("\r\n");
    return true;
}

bool WebSocketClient::sendHandshake(){
    FixedString<PICO_STR_256B> target;
    FixedString<PICO_STR_M> host;
    if(!UrlTools::RequestTarget(target, url_) || !UrlTools::HostHeader(host, url_)) return false;

    FixedString<PICO_STR_1KiB> req;
    bool ok = req.assign("GET ");
    ok = req.append(target) && ok;
    ok = req.append(" HTTP/1.1\r\nHost: ") && ok;
    ok = req.append(host) && ok;
    ok = req.append("\r\nUser-Agent: pico-os/1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                    "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: ") && ok;
    ok = req.append(key_) && ok;
    ok = req.append("\r\n") && ok;
    if(!protocol_.empty()){
        ok = req.append("Sec-WebSocket-Protocol: ") && ok;
        ok = req.append(protocol_) && ok;
        ok = req.append("\r\n") && ok;
    }
    ok = req.append(extra_headers_) && ok;
    ok = req.append("\r\n") && ok;
    if(!ok) return false;
    return conn_.write((const uint8_t*)req.c_str(), req.length()) == req.length();
}

bool WebSocketClient::checkHandshake(size_t header_len){
    const char* head = (const char*)msg_;
    //"HTTP/1.1 101 ..."
    if(header_len < 12 || strncmp(head, "HTTP/1.", 7) != 0 || strncmp(head + 8, " 101", 4) != 0){
        int status = 0;
        if(header_len >= 12) status = atoi(head + 9);
        LOG_SYS_WARN("WebSocket: %s が切り替えを断りました (HTTP %d)", url_.host.c_str(), status);
        closed_error_ = (status == 401 || status == 403) ? "サーバに断られた(認証)" : "サーバがWebSocketに切り替えない";
        return false;
    }
    const char* v = nullptr;
    size_t vlen = 0;
    if(!FindHeader(head, header_len, "Upgrade", v, vlen) || !HasToken(v, vlen, "websocket")
       || !FindHeader(head, header_len, "Connection", v, vlen) || !HasToken(v, vlen, "upgrade")){
        closed_error_ = "サーバの応答がWebSocketの形でない";
        return false;
    }
    char expect[WsFrame::kAcceptChars + 1];
    WsFrame::AcceptFor(key_, expect);
    if(!FindHeader(head, header_len, "Sec-WebSocket-Accept", v, vlen)
       || vlen != WsFrame::kAcceptChars || memcmp(v, expect, vlen) != 0){
        closed_error_ = "サーバの応答の確認(Accept)が合わない";
        return false;
    }
    //拡張は申し出ていないので、勝手に付けられたら話せない
    if(FindHeader(head, header_len, "Sec-WebSocket-Extensions", v, vlen) && vlen > 0){
        closed_error_ = "申し出ていない拡張をサーバが使おうとした";
        return false;
    }
    if(FindHeader(head, header_len, "Sec-WebSocket-Protocol", v, vlen)){
        accepted_protocol_.assign(v, vlen);
    }
    return true;
}

int WebSocketClient::readHandshake(){
    while(true){
        const int avail = conn_.available();
        if(avail <= 0) break;
        size_t room = msg_cap_ - hs_len_;
        if(room > kReadPerPoll) room = kReadPerPoll;
        if(room == 0){
            closed_error_ = "サーバの応答ヘッダが大きすぎる";
            return -1;
        }
        const int n = conn_.read(msg_ + hs_len_, room);
        if(n <= 0) break;
        const size_t from = hs_len_ >= 3 ? hs_len_ - 3 : 0;
        hs_len_ += (size_t)n;
        //ヘッダの終わり(空行)を探す
        for(size_t i = from; i + 4 <= hs_len_; i++){
            if(memcmp(msg_ + i, "\r\n\r\n", 4) == 0){
                const size_t header_len = i + 4;
                if(!checkHandshake(header_len)) return -1;
                //同じ受信でWebSocketの枠まで届いていたら残しておく(1回の読みは kReadPerPoll 以下なので rx_ に入る)
                const size_t rest = hs_len_ - header_len;
                memcpy(rx_, msg_ + header_len, rest);
                rx_pos_ = 0;
                rx_len_ = rest;
                return 1;
            }
        }
        if((size_t)n < room) break;
    }
    if(!conn_.connected() && conn_.available() <= 0){
        closed_error_ = "開始の挨拶の途中で切れた";
        return -1;
    }
    return 0;
}

bool WebSocketClient::sendFrame(WsFrame::Opcode op, const void* data, size_t len){
    if(state_ != State::Open && !(op == WsFrame::Close && state_ == State::Closing)) return false;
    if((op & 0x08) && len > WsFrame::kMaxControlPayload) return false;
    uint8_t mask[4];
    SecretAead::Random(mask, sizeof(mask));
    uint8_t head[WsFrame::kMaxHeaderBytes];
    const size_t hn = WsFrame::EncodeHeader(head, op, true, len, mask);
    if(conn_.write(head, hn) != hn){
        finish(WsFrame::kCloseAbnormal, "送れない(切れた)");
        return false;
    }
    //マスクは送る直前に小分けでかける(呼び出し側の本文は書き換えない)
    const uint8_t* src = (const uint8_t*)data;
    uint8_t chunk[256];
    size_t off = 0;
    while(off < len){
        size_t n = len - off;
        if(n > sizeof(chunk)) n = sizeof(chunk);
        memcpy(chunk, src + off, n);
        WsFrame::ApplyMask(chunk, n, mask, off);
        if(conn_.write(chunk, n) != n){
            finish(WsFrame::kCloseAbnormal, "送れない(切れた)");
            return false;
        }
        off += n;
    }
    return true;
}

bool WebSocketClient::sendClose(uint16_t code, const uint8_t* reason, size_t reason_len){
    uint8_t body[WsFrame::kMaxControlPayload];
    size_t n = 0;
    if(code != 0 && code != WsFrame::kCloseNoStatus && code != WsFrame::kCloseAbnormal){
        body[n++] = (uint8_t)(code >> 8);
        body[n++] = (uint8_t)code;
        if(reason_len > sizeof(body) - n) reason_len = sizeof(body) - n;
        if(reason && reason_len) memcpy(body + n, reason, reason_len);
        n += reason_len;
    }
    return sendFrame(WsFrame::Close, body, n);
}

void WebSocketClient::close(uint16_t code, const char* reason){
    switch(state_){
        case State::Open:
            state_ = State::Closing;
            started_ms_ = millis();
            closed_code_ = code;
            //送れなければ sendFrame() が切って Closed を用意する
            sendClose(code, (const uint8_t*)reason, reason ? strlen(reason) : 0);
            return;
        case State::Connecting:
        case State::Handshake:
            finish(code, nullptr);
            return;
        default:
            return;
    }
}

void WebSocketClient::releaseBuffer(){
    delete[] msg_;
    msg_ = nullptr;
    msg_cap_ = 0;
    parser_.reset(nullptr, 0);
}

void WebSocketClient::abort(){
    conn_.close();
    releaseBuffer();
    state_ = State::Idle;
    closed_pending_ = false;
}

void WebSocketClient::finish(uint16_t code, const char* error, const uint8_t* reason, size_t reason_len){
    if(state_ == State::Closed || state_ == State::Idle) return;
    conn_.close();
    closed_pending_ = true;
    closed_code_ = code;
    closed_error_ = error;
    if(reason_len > sizeof(closed_reason_)) reason_len = sizeof(closed_reason_);
    if(reason && reason_len) memcpy(closed_reason_, reason, reason_len);
    closed_reason_len_ = reason_len;
    state_ = State::Closed;
}

bool WebSocketClient::poll(Event& ev){
    ev = Event{};
    if(closed_pending_){
        closed_pending_ = false;
        //理由はメッセージの溜め場所と別に持っているので、溜め場所はここで返してよい
        releaseBuffer();
        ev.type = EventType::Closed;
        ev.code = closed_code_;
        ev.error = closed_error_;
        ev.data = closed_reason_;
        ev.len = closed_reason_len_;
        return true;
    }

    switch(state_){
        case State::Connecting:
            if(!conn_.connect(url_, kConnectTimeoutMs)){
                LOG_SYS_WARN("WebSocket: %s:%u へ繋げません (%s)", url_.host.c_str(), (unsigned)url_.port, conn_.errorText());
                //errorText() は conn_ が持つ文字列。次の connect() まで変わらない
                finish(WsFrame::kCloseAbnormal, conn_.errorText());
                return poll(ev);
            }
            if(!sendHandshake()){
                finish(WsFrame::kCloseAbnormal, "開始の挨拶を送れない");
                return poll(ev);
            }
            state_ = State::Handshake;
            started_ms_ = millis();
            return false;

        case State::Handshake: {
            const int r = readHandshake();
            if(r < 0){
                const char* why = closed_error_;
                finish(WsFrame::kCloseAbnormal, why);
                return poll(ev);
            }
            if(r == 0){
                if(millis() - started_ms_ >= kHandshakeTimeoutMs){
                    finish(WsFrame::kCloseAbnormal, "サーバの応答が無い(開始の挨拶)");
                    return poll(ev);
                }
                return false;
            }
            parser_.reset(msg_, msg_cap_);
            state_ = State::Open;
            last_rx_ms_ = last_ping_ms_ = millis();
            ev.type = EventType::Open;
            return true;
        }

        case State::Open:
        case State::Closing:
            return pollOpen(ev);

        default:
            return false;
    }
}

bool WebSocketClient::pollOpen(Event& ev){
    bool read_done = false;
    while(true){
        if(rx_pos_ < rx_len_){
            size_t used = 0;
            const WsFrame::Parser::Result r = parser_.feed(rx_ + rx_pos_, rx_len_ - rx_pos_, used);
            rx_pos_ += used;
            switch(r){
                case WsFrame::Parser::Result::Message:
                    if(state_ == State::Closing) continue; //閉じかけの間に届いたものは渡さない
                    ev.type = EventType::Message;
                    ev.data = parser_.data();
                    ev.len = parser_.size();
                    ev.binary = parser_.binary();
                    return true;
                case WsFrame::Parser::Result::Ping:
                    if(state_ == State::Open && !sendFrame(WsFrame::Pong, parser_.control(), parser_.controlSize())){
                        return poll(ev);
                    }
                    continue;
                case WsFrame::Parser::Result::Pong:
                    ping_waiting_ = false;
                    continue;
                case WsFrame::Parser::Result::Close: {
                    const uint16_t code = parser_.closeCode();
                    if(state_ == State::Open){
                        //相手から閉じた: 同じ番号で返事をしてから切る
                        state_ = State::Closing;
                        sendClose(code, nullptr, 0);
                    }
                    finish(code, nullptr, parser_.control(), parser_.controlSize());
                    return poll(ev);
                }
                case WsFrame::Parser::Result::Error:
                    LOG_SYS_WARN("WebSocket: %s (%s)", parser_.errorText(), url_.host.c_str());
                    if(state_ == State::Open){
                        state_ = State::Closing;
                        sendClose(parser_.errorCode(), nullptr, 0);
                    }
                    finish(parser_.errorCode(), parser_.errorText());
                    return poll(ev);
                case WsFrame::Parser::Result::NeedMore:
                    break;
            }
        }
        if(state_ != State::Open && state_ != State::Closing) return poll(ev);
        //1回の poll() で読むのは1回まで(画面を止めない)
        if(read_done) break;
        read_done = true;
        rx_pos_ = rx_len_ = 0;
        if(conn_.available() > 0){
            const int n = conn_.read(rx_, sizeof(rx_));
            if(n > 0){
                rx_len_ = (size_t)n;
                last_rx_ms_ = millis();
                ping_waiting_ = false; //何か届いていれば生きている
                continue;
            }
        }
        if(!conn_.connected()){
            finish(WsFrame::kCloseAbnormal, state_ == State::Closing ? nullptr : "接続が切れた");
            return poll(ev);
        }
        break;
    }

    const unsigned long now = millis();
    if(state_ == State::Closing){
        if(now - started_ms_ >= kCloseWaitMs){
            finish(closed_code_, nullptr);
            return poll(ev);
        }
        return false;
    }
    if(ping_interval_ms_ > 0){
        if(ping_waiting_ && now - last_ping_ms_ >= ping_interval_ms_){
            state_ = State::Closing;
            finish(WsFrame::kCloseAbnormal, "サーバの応答が無い(pingに答えない)");
            return poll(ev);
        }
        if(!ping_waiting_ && now - last_rx_ms_ >= ping_interval_ms_ && now - last_ping_ms_ >= ping_interval_ms_){
            last_ping_ms_ = now;
            ping_waiting_ = true;
            if(!sendPing()) return poll(ev);
        }
    }
    return false;
}
