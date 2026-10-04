#include "ssh/Ssh_Client.hpp"
#include "ssh/Ssh_Util.hpp"

#include "Arduino.h"
#include "monocypher.h"
#include "monocypher-ed25519.h"

#include <cstdio>
#include <cstring>

// メッセージ番号(RFC 4250)
namespace {
    constexpr uint8_t MSG_DISCONNECT = 1;
    constexpr uint8_t MSG_IGNORE = 2;
    constexpr uint8_t MSG_UNIMPLEMENTED = 3;
    constexpr uint8_t MSG_DEBUG = 4;
    constexpr uint8_t MSG_SERVICE_REQUEST = 5;
    constexpr uint8_t MSG_SERVICE_ACCEPT = 6;
    constexpr uint8_t MSG_EXT_INFO = 7;
    constexpr uint8_t MSG_KEXINIT = 20;
    constexpr uint8_t MSG_NEWKEYS = 21;
    constexpr uint8_t MSG_KEX_ECDH_INIT = 30;
    constexpr uint8_t MSG_KEX_ECDH_REPLY = 31;
    constexpr uint8_t MSG_USERAUTH_REQUEST = 50;
    constexpr uint8_t MSG_USERAUTH_FAILURE = 51;
    constexpr uint8_t MSG_USERAUTH_SUCCESS = 52;
    constexpr uint8_t MSG_USERAUTH_BANNER = 53;
    constexpr uint8_t MSG_USERAUTH_INFO_REQUEST = 60;
    constexpr uint8_t MSG_USERAUTH_INFO_RESPONSE = 61;
    constexpr uint8_t MSG_GLOBAL_REQUEST = 80;
    constexpr uint8_t MSG_REQUEST_SUCCESS = 81;
    constexpr uint8_t MSG_REQUEST_FAILURE = 82;
    constexpr uint8_t MSG_CHANNEL_OPEN = 90;
    constexpr uint8_t MSG_CHANNEL_OPEN_CONFIRMATION = 91;
    constexpr uint8_t MSG_CHANNEL_OPEN_FAILURE = 92;
    constexpr uint8_t MSG_CHANNEL_WINDOW_ADJUST = 93;
    constexpr uint8_t MSG_CHANNEL_DATA = 94;
    constexpr uint8_t MSG_CHANNEL_EXTENDED_DATA = 95;
    constexpr uint8_t MSG_CHANNEL_EOF = 96;
    constexpr uint8_t MSG_CHANNEL_CLOSE = 97;
    constexpr uint8_t MSG_CHANNEL_REQUEST = 98;
    constexpr uint8_t MSG_CHANNEL_SUCCESS = 99;
    constexpr uint8_t MSG_CHANNEL_FAILURE = 100;

    const char kClientVersion[] = "SSH-2.0-picoos_1.0";
    const char kKexAlgs[] = "curve25519-sha256,curve25519-sha256@libssh.org,kex-strict-c-v00@openssh.com";
    const char kCipher[] = "chacha20-poly1305@openssh.com";

    uint32_t Be32(const uint8_t* p){
        return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
    }
    void PutBe32(uint8_t* p, uint32_t v){
        p[0] = (uint8_t)(v >> 24);
        p[1] = (uint8_t)(v >> 16);
        p[2] = (uint8_t)(v >> 8);
        p[3] = (uint8_t)v;
    }
    void Nonce(uint32_t seq, uint8_t out[8]){
        memset(out, 0, 4);
        PutBe32(out + 4, seq);
    }

    // 受信したペイロードを先頭から読む
    struct Reader {
        const uint8_t* p;
        const uint8_t* end;
        bool ok = true;

        Reader(const uint8_t* data, size_t n) : p(data), end(data + n) {}

        uint8_t byte(){
            if(p >= end){ ok = false; return 0; }
            return *p++;
        }
        uint32_t u32(){
            if(end - p < 4){ ok = false; return 0; }
            const uint32_t v = Be32(p);
            p += 4;
            return v;
        }
        // string。無ければ長さ0で ok=false
        const uint8_t* str(uint32_t& n){
            n = u32();
            if(!ok || (uint32_t)(end - p) < n){
                ok = false;
                n = 0;
                return p;
            }
            const uint8_t* s = p;
            p += n;
            return s;
        }
    };

    bool StrEq(const uint8_t* s, uint32_t n, const char* lit){
        return n == strlen(lit) && memcmp(s, lit, n) == 0;
    }

    // カンマ区切りの名前の並びに name がちょうど含まれるか
    bool ListHas(const uint8_t* s, uint32_t n, const char* name){
        const size_t len = strlen(name);
        uint32_t i = 0;
        while(i <= n){
            uint32_t j = i;
            while(j < n && s[j] != ',') j++;
            if(j - i == len && memcmp(s + i, name, len) == 0) return true;
            i = j + 1;
        }
        return false;
    }
    bool ListHas(const char* list, const char* name){
        return ListHas((const uint8_t*)list, (uint32_t)strlen(list), name);
    }
}

SshClient::~SshClient(){
    this->closeSocket();
    crypto_wipe(this->id_secret_, sizeof(this->id_secret_));
    crypto_wipe(this->eph_secret_, sizeof(this->eph_secret_));
    crypto_wipe(&this->tx_keys_, sizeof(this->tx_keys_));
    crypto_wipe(&this->rx_keys_, sizeof(this->rx_keys_));
    crypto_wipe(this->next_tx_, sizeof(this->next_tx_));
    crypto_wipe(this->next_rx_, sizeof(this->next_rx_));
}

void SshClient::setIdentity(const uint8_t secret[64], const uint8_t pub[32]){
    memcpy(this->id_secret_, secret, 64);
    memcpy(this->id_pub_, pub, 32);
    this->have_identity_ = true;
}

void SshClient::setTerminalSize(int cols, int rows, int width_px, int height_px){
    if(cols == this->term_cols_ && rows == this->term_rows_) return;
    this->term_cols_ = cols;
    this->term_rows_ = rows;
    this->term_wpx_ = width_px;
    this->term_hpx_ = height_px;
    if(this->state_ == State::Open && !this->kex_in_progress_) this->sendWindowChange();
}

// ---------------------------------------------------------------- 出入り口

void SshClient::output(const uint8_t* data, size_t len){
    if(this->out_fn_ && len > 0) this->out_fn_(this->out_ctx_, data, len);
}

void SshClient::notice(const char* text){
    const char* head = "\x1b[93m";
    const char* tail = "\x1b[0m\r\n";
    this->output((const uint8_t*)head, strlen(head));
    this->output((const uint8_t*)text, strlen(text));
    this->output((const uint8_t*)tail, strlen(tail));
}

void SshClient::closeSocket(){
    if(this->stream_) this->stream_->stop();
    else this->sock_.stop();
    this->rx_len_ = 0;
}

int SshClient::ioAvailable(){
    return this->stream_ ? this->stream_->available() : this->sock_.available();
}

int SshClient::ioRead(uint8_t* buf, size_t n){
    return this->stream_ ? this->stream_->read(buf, n) : this->sock_.read(buf, n);
}

bool SshClient::ioWrite(const uint8_t* buf, size_t n){
    if(this->stream_) return this->stream_->write(buf, n);
    return this->sock_.write(buf, n) == n;
}

bool SshClient::ioConnected(){
    return this->stream_ ? this->stream_->connected() : (bool)this->sock_.connected();
}

bool SshClient::sendHello(){
    char hello[32];
    const int n = snprintf(hello, sizeof(hello), "%s\r\n", kClientVersion);
    if(!this->ioWrite((const uint8_t*)hello, (size_t)n)){
        this->fail("送信に失敗しました");
        return false;
    }
    this->state_ = State::Handshake;
    this->phase_start_ = millis();
    this->sendKexInit(); //相手の挨拶を待たずに出してよい(RFC 4253 7.1)
    return this->state_ != State::Closed;
}

bool SshClient::connectVia(SshStream* stream, const char* user){
    if(this->state_ != State::Idle || !stream) return false;
    this->user_.assign(user ? user : "");
    this->stream_ = stream;
    return this->sendHello();
}

void SshClient::setForward(const char* host, uint16_t port){
    this->fwd_host_.assign(host ? host : "");
    this->fwd_port_ = port;
}

void SshClient::consume(size_t n){
    if(!this->manual_window_) return;
    this->consumed_ += (uint32_t)n;
    this->adjustWindow();
}

void SshClient::adjustWindow(){
    //鍵の交換し直しの間は送れないので、終わったとき(kexDone())にもう一度呼ぶ
    if(this->state_ != State::Open || this->kex_in_progress_) return;
    if(this->consumed_ < this->window_ / 2) return;
    this->begin(MSG_CHANNEL_WINDOW_ADJUST);
    this->putU32(this->remote_id_);
    this->putU32(this->consumed_);
    this->sendPacket();
    this->consumed_ = 0;
}

void SshClient::fail(const char* text){
    if(this->state_ == State::Closed) return;
    this->err_.assign(text);
    this->closeSocket();
    this->state_ = State::Closed;
    crypto_wipe(this->eph_secret_, sizeof(this->eph_secret_));
    crypto_wipe(this->next_tx_, sizeof(this->next_tx_));
    crypto_wipe(this->next_rx_, sizeof(this->next_rx_));
}

bool SshClient::connect(const char* host, uint16_t port, const char* user, unsigned long timeout_ms){
    if(this->state_ != State::Idle) return false;
    this->user_.assign(user ? user : "");

    // 名前の解決(DNS)と接続を分けて、どちらで失敗したか分かるようにする
    IPAddress ip;
    if(WiFi.hostByName(host, ip, (uint32_t)timeout_ms) != 1){
        this->err_.assign("名前を解決できませんでした: ");
        this->err_.append(host);
        this->state_ = State::Closed;
        return false;
    }

    this->sock_.setTimeout(timeout_ms);
    if(this->sock_.connect(ip, port) != 1){
        this->err_.assign("接続できませんでした(到達しない/ポートが閉じている)");
        this->state_ = State::Closed;
        return false;
    }
    this->sock_.setNoDelay(true);
    return this->sendHello();
}

void SshClient::disconnect(){
    if(this->state_ == State::Idle || this->state_ == State::Closed) return;
    if(this->tx_keys_.on && !this->kex_in_progress_){
        this->begin(MSG_DISCONNECT);
        this->putU32(11); // SSH_DISCONNECT_BY_APPLICATION
        this->putCStr("bye");
        this->putCStr("");
        this->sendPacket();
    }
    this->fail("切断しました");
}

// ---------------------------------------------------------------- パケットを組む

void SshClient::begin(uint8_t msg){
    this->tx_pos_ = 5;
    this->tx_overflow_ = false;
    this->putByte(msg);
}

void SshClient::putBytes(const void* p, size_t n){
    // 詰め物(最大15) + 認証タグ(16)ぶんは空けておく
    if(this->tx_pos_ + n + 32 > kTxCap){
        this->tx_overflow_ = true;
        return;
    }
    memcpy(this->tx_ + this->tx_pos_, p, n);
    this->tx_pos_ += n;
}

void SshClient::putByte(uint8_t v){ this->putBytes(&v, 1); }

void SshClient::putU32(uint32_t v){
    uint8_t b[4];
    PutBe32(b, v);
    this->putBytes(b, 4);
}

void SshClient::putString(const void* p, size_t n){
    this->putU32((uint32_t)n);
    this->putBytes(p, n);
}

void SshClient::putCStr(const char* s){ this->putString(s, strlen(s)); }

void SshClient::putMpint(const uint8_t* be, size_t n){
    while(n > 0 && be[0] == 0){ be++; n--; }
    if(n > 0 && (be[0] & 0x80)){
        this->putU32((uint32_t)n + 1);
        this->putByte(0);
    }else{
        this->putU32((uint32_t)n);
    }
    this->putBytes(be, n);
}

bool SshClient::sendPacket(){
    if(this->tx_overflow_){
        this->fail("送信するパケットが大きすぎます");
        return false;
    }
    const size_t payload = this->tx_pos_ - 5;
    // 暗号化の前は長さの欄も含めて8の倍数、chacha20-poly1305では長さの欄を除いて8の倍数
    const size_t aligned = this->tx_keys_.on ? (1 + payload) : (5 + payload);
    size_t pad = 8 - (aligned % 8);
    if(pad < 4) pad += 8;
    SshUtil::Random(this->tx_ + this->tx_pos_, pad);
    const uint32_t packet_len = (uint32_t)(1 + payload + pad);
    PutBe32(this->tx_, packet_len);
    this->tx_[4] = (uint8_t)pad;
    size_t total = 4 + packet_len;

    if(this->tx_keys_.on){
        uint8_t nonce[8];
        Nonce(this->tx_seq_, nonce);
        uint8_t poly_key[32] = {0};
        crypto_chacha20_djb(poly_key, poly_key, sizeof(poly_key), this->tx_keys_.main, nonce, 0);
        crypto_chacha20_djb(this->tx_, this->tx_, 4, this->tx_keys_.header, nonce, 0);
        crypto_chacha20_djb(this->tx_ + 4, this->tx_ + 4, packet_len, this->tx_keys_.main, nonce, 1);
        crypto_poly1305(this->tx_ + total, this->tx_, total, poly_key);
        crypto_wipe(poly_key, sizeof(poly_key));
        total += 16;
    }
    this->tx_seq_++;

    if(!this->ioWrite(this->tx_, total)){
        this->fail("送信に失敗しました");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- 受信

void SshClient::update(){
    if(this->state_ == State::Idle || this->state_ == State::Closed) return;

    this->pumpSocket();
    if(this->state_ == State::Closed) return;

    if(!this->got_version_ && !this->parseVersion()){
        // まだ挨拶の行が揃っていない
    }
    while(this->got_version_ && this->state_ != State::Closed && this->parsePacket()){}
    if(this->state_ == State::Closed) return;

    if(!this->ioConnected() && this->ioAvailable() <= 0){
        this->fail("接続が切れました");
        return;
    }

    const bool waiting_user = (this->state_ == State::HostKeyCheck || this->state_ == State::NeedPassword);
    if(!waiting_user && this->state_ != State::Open &&
       (unsigned long)(millis() - this->phase_start_) > kHandshakeTimeoutMs){
        this->fail("サーバから応答がありません(時間切れ)");
    }
}

void SshClient::pumpSocket(){
    while(this->rx_len_ < kRxCap){
        const int avail = this->ioAvailable();
        if(avail <= 0) break;
        size_t want = (size_t)avail;
        if(want > kRxCap - this->rx_len_) want = kRxCap - this->rx_len_;
        const int n = this->ioRead(this->rx_ + this->rx_len_, want);
        if(n <= 0) break;
        this->rx_len_ += (size_t)n;
    }
}

bool SshClient::parseVersion(){
    while(true){
        size_t nl = 0;
        while(nl < this->rx_len_ && this->rx_[nl] != '\n') nl++;
        if(nl >= this->rx_len_){
            if(this->rx_len_ > 1024) this->fail("SSHサーバではないようです");
            return false;
        }
        size_t len = nl;
        if(len > 0 && this->rx_[len - 1] == '\r') len--;
        const bool is_version = len >= 4 && memcmp(this->rx_, "SSH-", 4) == 0;
        if(is_version){
            if(!(len >= 8 && memcmp(this->rx_, "SSH-2.0-", 8) == 0) &&
               !(len >= 9 && memcmp(this->rx_, "SSH-1.99-", 9) == 0)){
                this->fail("SSH2に対応していないサーバです");
                return false;
            }
            if(len > 253) len = 253;
            this->v_s_.assign((const char*)this->rx_, len);
        }
        memmove(this->rx_, this->rx_ + nl + 1, this->rx_len_ - nl - 1);
        this->rx_len_ -= nl + 1;
        if(is_version){
            this->got_version_ = true;
            return true;
        }
        // 挨拶の前の行(RFC 4253 4.2)は読み捨てる
    }
}

bool SshClient::parsePacket(){
    if(this->rx_len_ < 4) return false;

    uint8_t nonce[8];
    Nonce(this->rx_seq_, nonce);

    uint32_t packet_len;
    if(this->rx_keys_.on){
        uint8_t len_buf[4];
        crypto_chacha20_djb(len_buf, this->rx_, 4, this->rx_keys_.header, nonce, 0);
        packet_len = Be32(len_buf);
    }else{
        packet_len = Be32(this->rx_);
    }
    const size_t tag = this->rx_keys_.on ? 16 : 0;
    if(packet_len < 5 || packet_len > kRxCap - 4 - tag){
        this->fail("サーバから大きすぎるパケットが届きました");
        return false;
    }
    const size_t need = 4 + packet_len + tag;
    if(this->rx_len_ < need) return false;

    if(this->rx_keys_.on){
        uint8_t poly_key[32] = {0};
        crypto_chacha20_djb(poly_key, poly_key, sizeof(poly_key), this->rx_keys_.main, nonce, 0);
        uint8_t mac[16];
        crypto_poly1305(mac, this->rx_, 4 + packet_len, poly_key);
        crypto_wipe(poly_key, sizeof(poly_key));
        if(crypto_verify16(mac, this->rx_ + 4 + packet_len) != 0){
            this->fail("受信したデータの改ざんを検出しました");
            return false;
        }
        crypto_chacha20_djb(this->rx_ + 4, this->rx_ + 4, packet_len, this->rx_keys_.main, nonce, 1);
    }

    const uint8_t pad = this->rx_[4];
    if((uint32_t)pad + 1 > packet_len){
        this->fail("壊れたパケットが届きました");
        return false;
    }
    const uint32_t seq = this->rx_seq_++;
    this->handle(this->rx_ + 5, packet_len - pad - 1, seq);

    if(this->state_ == State::Closed) return false;
    memmove(this->rx_, this->rx_ + need, this->rx_len_ - need);
    this->rx_len_ -= need;
    return true;
}

void SshClient::handle(const uint8_t* p, size_t n, uint32_t seq){
    if(n == 0){
        this->fail("空のパケットが届きました");
        return;
    }
    const uint8_t msg = p[0];
    Reader r(p + 1, n - 1);

    // kex-strict: 最初の鍵交換の間は、鍵交換のメッセージ以外を受け付けない(Terrapin対策)
    if(this->strict_kex_ && !this->first_kex_done_ &&
       msg != MSG_KEXINIT && msg != MSG_KEX_ECDH_REPLY && msg != MSG_NEWKEYS && msg != MSG_DISCONNECT){
        this->fail("鍵交換の途中で想定外のメッセージが届きました");
        return;
    }

    switch(msg){
        case MSG_DISCONNECT: {
            r.u32();
            uint32_t dn;
            const uint8_t* d = r.str(dn);
            char buf[PICO_STR_L];
            if(dn > 60) dn = 60;
            snprintf(buf, sizeof(buf), "サーバが切断しました: %.*s", (int)dn, (const char*)d);
            this->fail(buf);
            return;
        }
        case MSG_IGNORE:
        case MSG_DEBUG:
        case MSG_UNIMPLEMENTED:
        case MSG_EXT_INFO:
        case MSG_REQUEST_SUCCESS:
        case MSG_REQUEST_FAILURE:
        case MSG_CHANNEL_EOF:
            return;

        case MSG_KEXINIT:
            this->onKexInit(p, n);
            // kex-strict: 最初の KEXINIT は最初のパケットでなければならない
            if(this->strict_kex_ && !this->first_kex_done_ && seq != 0){
                this->fail("鍵交換の順序が正しくありません");
            }
            return;
        case MSG_KEX_ECDH_REPLY:
            this->onKexReply(p + 1, n - 1);
            return;
        case MSG_NEWKEYS:
            this->onNewKeys();
            return;

        case MSG_SERVICE_ACCEPT:
            this->sendAuthNone();
            return;
        case MSG_USERAUTH_FAILURE:
            this->onAuthFailure(p + 1, n - 1);
            return;
        case MSG_USERAUTH_SUCCESS:
            this->auth_method_ = AuthMethod::None;
            this->openChannel();
            return;
        case MSG_USERAUTH_BANNER: {
            uint32_t bn;
            const uint8_t* b = r.str(bn);
            // 改行を端末向け(CR LF)に直して出す
            size_t start = 0;
            for(size_t i = 0; i < bn; i++){
                if(b[i] == '\n'){
                    this->output(b + start, i - start);
                    this->output((const uint8_t*)"\r\n", 2);
                    start = i + 1;
                }
            }
            this->output(b + start, bn - start);
            return;
        }
        case MSG_USERAUTH_INFO_REQUEST: // PASSWD_CHANGEREQ / PK_OK と同じ番号
            if(this->auth_method_ == AuthMethod::Keyboard){
                this->onInfoRequest(p + 1, n - 1);
            }else if(this->auth_method_ == AuthMethod::Password){
                this->fail("パスワードの変更を求められました(未対応)");
            }
            return;

        case MSG_GLOBAL_REQUEST: {
            uint32_t nn;
            r.str(nn);
            const bool want_reply = r.byte() != 0;
            if(want_reply){
                this->begin(MSG_REQUEST_FAILURE);
                this->sendPacket();
            }
            return;
        }
        case MSG_CHANNEL_OPEN: {
            uint32_t tn;
            r.str(tn);
            const uint32_t sender = r.u32();
            this->begin(MSG_CHANNEL_OPEN_FAILURE);
            this->putU32(sender);
            this->putU32(1); // SSH_OPEN_ADMINISTRATIVELY_PROHIBITED
            this->putCStr("");
            this->putCStr("");
            this->sendPacket();
            return;
        }
        case MSG_CHANNEL_OPEN_CONFIRMATION:
            this->onChannelOpenConfirm(p + 1, n - 1);
            return;
        case MSG_CHANNEL_OPEN_FAILURE:
            this->onChannelOpenFailure(p + 1, n - 1);
            return;
        case MSG_CHANNEL_WINDOW_ADJUST: {
            r.u32();
            const uint32_t add = r.u32();
            if(r.ok){
                const uint64_t w = (uint64_t)this->remote_window_ + add;
                this->remote_window_ = w > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)w;
                this->flushPending();
            }
            return;
        }
        case MSG_CHANNEL_DATA: {
            r.u32();
            uint32_t dn;
            const uint8_t* d = r.str(dn);
            if(r.ok) this->onChannelData(d, dn);
            return;
        }
        case MSG_CHANNEL_EXTENDED_DATA: {
            r.u32();
            r.u32();
            uint32_t dn;
            const uint8_t* d = r.str(dn);
            if(r.ok) this->onChannelData(d, dn);
            return;
        }
        case MSG_CHANNEL_CLOSE: {
            if(!this->close_sent_){
                this->begin(MSG_CHANNEL_CLOSE);
                this->putU32(this->remote_id_);
                this->sendPacket();
                this->close_sent_ = true;
            }
            char buf[PICO_STR_L];
            if(this->exit_status_ >= 0) snprintf(buf, sizeof(buf), "接続を閉じました(終了コード %d)", this->exit_status_);
            else snprintf(buf, sizeof(buf), "接続を閉じました");
            this->fail(buf);
            return;
        }
        case MSG_CHANNEL_REQUEST: {
            r.u32();
            uint32_t tn;
            const uint8_t* t = r.str(tn);
            const bool want_reply = r.byte() != 0;
            if(r.ok && StrEq(t, tn, "exit-status")){
                const uint32_t st = r.u32();
                if(r.ok) this->exit_status_ = (int)st;
            }
            if(want_reply){
                this->begin(MSG_CHANNEL_FAILURE);
                this->putU32(this->remote_id_);
                this->sendPacket();
            }
            return;
        }
        case MSG_CHANNEL_SUCCESS:
            this->onChannelReply(true);
            return;
        case MSG_CHANNEL_FAILURE:
            this->onChannelReply(false);
            return;

        default:
            this->begin(MSG_UNIMPLEMENTED);
            this->putU32(seq);
            this->sendPacket();
            return;
    }
}

// ---------------------------------------------------------------- 鍵交換

void SshClient::sendKexInit(){
    uint8_t cookie[16];
    SshUtil::Random(cookie, sizeof(cookie));

    this->begin(MSG_KEXINIT);
    this->putBytes(cookie, sizeof(cookie));
    this->putCStr(this->first_kex_done_ ? "curve25519-sha256,curve25519-sha256@libssh.org" : kKexAlgs);
    this->putCStr("ssh-ed25519");
    this->putCStr(kCipher);
    this->putCStr(kCipher);
    this->putCStr("hmac-sha2-256"); // AEADなので実際には使われない
    this->putCStr("hmac-sha2-256");
    this->putCStr("none");
    this->putCStr("none");
    this->putCStr("");
    this->putCStr("");
    this->putByte(0); // first_kex_packet_follows
    this->putU32(0);

    //交換ハッシュに使うので、暗号化される前に控えておく
    this->kexinit_c_len_ = this->tx_pos_ - 5;
    if(this->kexinit_c_len_ > sizeof(this->kexinit_c_)){
        this->fail("鍵交換の準備に失敗しました");
        return;
    }
    memcpy(this->kexinit_c_, this->tx_ + 5, this->kexinit_c_len_);

    this->kex_in_progress_ = true;
    this->kexinit_sent_ = true;
    this->sendPacket();
}

void SshClient::onKexInit(const uint8_t* p, size_t n){
    if(this->kex_in_progress_ && this->kex_reply_done_){
        this->fail("鍵交換の途中で鍵交換を求められました");
        return;
    }
    if(!this->kexinit_sent_){
        //サーバからの鍵交換のやり直し
        this->sendKexInit();
        if(this->state_ == State::Closed) return;
    }

    Reader r(p + 1, n - 1);
    for(int i = 0; i < 16; i++) r.byte();
    uint32_t len[10];
    const uint8_t* lists[10];
    for(int i = 0; i < 10; i++) lists[i] = r.str(len[i]);
    if(!r.ok){
        this->fail("鍵交換のメッセージが壊れています");
        return;
    }

    char buf[PICO_STR_LL];
    if(!ListHas(lists[0], len[0], "curve25519-sha256") && !ListHas(lists[0], len[0], "curve25519-sha256@libssh.org")){
        this->fail("サーバが curve25519-sha256 の鍵交換に対応していません");
        return;
    }
    if(!ListHas(lists[1], len[1], "ssh-ed25519")){
        this->fail("サーバに ssh-ed25519 のホスト鍵がありません");
        return;
    }
    if(!ListHas(lists[2], len[2], kCipher) || !ListHas(lists[3], len[3], kCipher)){
        snprintf(buf, sizeof(buf), "サーバが %s に対応していません", kCipher);
        this->fail(buf);
        return;
    }
    if(!ListHas(lists[6], len[6], "none") || !ListHas(lists[7], len[7], "none")){
        this->fail("サーバが圧縮なしの通信に対応していません");
        return;
    }
    if(!this->first_kex_done_ && ListHas(lists[0], len[0], "kex-strict-s-v00@openssh.com")){
        this->strict_kex_ = true;
    }

    //交換ハッシュ H = SHA256(V_C || V_S || I_C || I_S || K_S || Q_C || Q_S || K)
    auto hash_string = [this](const void* d, size_t dn){
        uint8_t b[4];
        PutBe32(b, (uint32_t)dn);
        this->hash_.update(b, 4);
        this->hash_.update(d, dn);
    };
    this->hash_.reset();
    hash_string(kClientVersion, strlen(kClientVersion));
    hash_string(this->v_s_.c_str(), this->v_s_.length());
    hash_string(this->kexinit_c_, this->kexinit_c_len_);
    hash_string(p, n);

    SshUtil::Random(this->eph_secret_, sizeof(this->eph_secret_));
    crypto_x25519_public_key(this->eph_pub_, this->eph_secret_);

    this->kex_reply_done_ = false;
    this->newkeys_sent_ = false;
    this->newkeys_recv_ = false;

    this->begin(MSG_KEX_ECDH_INIT);
    this->putString(this->eph_pub_, 32);
    this->sendPacket();
}

void SshClient::onKexReply(const uint8_t* p, size_t n){
    if(!this->kex_in_progress_ || this->kex_reply_done_){
        this->fail("想定外の鍵交換の応答が届きました");
        return;
    }
    Reader r(p, n);
    uint32_t ks_n, qs_n, sig_n;
    const uint8_t* ks = r.str(ks_n);
    const uint8_t* qs = r.str(qs_n);
    const uint8_t* sig = r.str(sig_n);
    if(!r.ok || qs_n != 32){
        this->fail("鍵交換の応答が壊れています");
        return;
    }

    Reader kr(ks, ks_n);
    uint32_t type_n, pub_n;
    const uint8_t* type = kr.str(type_n);
    const uint8_t* pub = kr.str(pub_n);
    if(!kr.ok || !StrEq(type, type_n, "ssh-ed25519") || pub_n != 32){
        this->fail("ホスト鍵の形式が ssh-ed25519 ではありません");
        return;
    }

    uint8_t shared[32];
    crypto_x25519(shared, this->eph_secret_, qs);
    crypto_wipe(this->eph_secret_, sizeof(this->eph_secret_));
    uint8_t zero_or = 0;
    for(int i = 0; i < 32; i++) zero_or |= shared[i];
    if(zero_or == 0){
        this->fail("鍵交換に失敗しました(不正な公開値)");
        return;
    }

    // K は mpint として(RFC 8731: X25519の出力をそのまま大きい方から並んだ数とみなす)
    uint8_t kmp[4 + 33];
    size_t off = 0;
    while(off < 32 && shared[off] == 0) off++;
    size_t klen = 32 - off;
    size_t kmp_len;
    if(shared[off] & 0x80){
        PutBe32(kmp, (uint32_t)klen + 1);
        kmp[4] = 0;
        memcpy(kmp + 5, shared + off, klen);
        kmp_len = 5 + klen;
    }else{
        PutBe32(kmp, (uint32_t)klen);
        memcpy(kmp + 4, shared + off, klen);
        kmp_len = 4 + klen;
    }
    crypto_wipe(shared, sizeof(shared));

    auto hash_string = [this](const void* d, size_t dn){
        uint8_t b[4];
        PutBe32(b, (uint32_t)dn);
        this->hash_.update(b, 4);
        this->hash_.update(d, dn);
    };
    hash_string(ks, ks_n);
    hash_string(this->eph_pub_, 32);
    hash_string(qs, 32);
    this->hash_.update(kmp, kmp_len);
    uint8_t h[32];
    this->hash_.finish(h);

    Reader sr(sig, sig_n);
    uint32_t st_n, sv_n;
    const uint8_t* st = sr.str(st_n);
    const uint8_t* sv = sr.str(sv_n);
    if(!sr.ok || !StrEq(st, st_n, "ssh-ed25519") || sv_n != 64 ||
       crypto_ed25519_check(sv, pub, h, sizeof(h)) != 0){
        crypto_wipe(kmp, sizeof(kmp));
        this->fail("ホスト鍵の署名が正しくありません");
        return;
    }

    if(!this->first_kex_done_) memcpy(this->session_id_, h, 32);
    this->deriveKeys(kmp, kmp_len, h);
    crypto_wipe(kmp, sizeof(kmp));
    this->kex_reply_done_ = true;

    if(!this->first_kex_done_){
        memcpy(this->host_pub_, pub, 32);
        this->have_host_pub_ = true;
        this->state_ = State::HostKeyCheck; //利用者の判断を待つ(acceptHostKey())
        return;
    }
    if(memcmp(this->host_pub_, pub, 32) != 0){
        this->fail("鍵の交換し直しでホスト鍵が変わりました");
        return;
    }
    this->sendNewKeys();
}

void SshClient::deriveKeys(const uint8_t* k_mpint, size_t k_len, const uint8_t h[32]){
    // RFC 4253 7.2: K1 = HASH(K || H || X || session_id), K2 = HASH(K || H || K1)
    auto derive = [&](char letter, uint8_t out[64]){
        Sha256 s;
        s.update(k_mpint, k_len);
        s.update(h, 32);
        s.update(&letter, 1);
        s.update(this->session_id_, 32);
        s.finish(out);
        Sha256 s2;
        s2.update(k_mpint, k_len);
        s2.update(h, 32);
        s2.update(out, 32);
        s2.finish(out + 32);
    };
    derive('C', this->next_tx_);
    derive('D', this->next_rx_);
}

void SshClient::acceptHostKey(bool accept){
    if(this->state_ != State::HostKeyCheck) return;
    if(!accept){
        this->fail("ホスト鍵を信頼しなかったため中止しました");
        return;
    }
    this->state_ = State::Handshake;
    this->phase_start_ = millis();
    this->sendNewKeys();
}

void SshClient::sendNewKeys(){
    this->begin(MSG_NEWKEYS);
    if(!this->sendPacket()) return;
    // chacha20-poly1305@openssh.com: 64バイトの前半がペイロード用、後半が長さの欄用
    memcpy(this->tx_keys_.main, this->next_tx_, 32);
    memcpy(this->tx_keys_.header, this->next_tx_ + 32, 32);
    this->tx_keys_.on = true;
    crypto_wipe(this->next_tx_, sizeof(this->next_tx_));
    if(this->strict_kex_) this->tx_seq_ = 0;
    this->newkeys_sent_ = true;
    if(this->newkeys_recv_) this->kexDone();
}

void SshClient::onNewKeys(){
    if(!this->kex_in_progress_ || !this->kex_reply_done_ || this->newkeys_recv_){
        this->fail("想定外の NEWKEYS が届きました");
        return;
    }
    memcpy(this->rx_keys_.main, this->next_rx_, 32);
    memcpy(this->rx_keys_.header, this->next_rx_ + 32, 32);
    this->rx_keys_.on = true;
    crypto_wipe(this->next_rx_, sizeof(this->next_rx_));
    if(this->strict_kex_) this->rx_seq_ = 0;
    this->newkeys_recv_ = true;
    if(this->newkeys_sent_) this->kexDone();
}

void SshClient::kexDone(){
    this->kex_in_progress_ = false;
    this->kexinit_sent_ = false;
    this->kex_reply_done_ = false;
    if(!this->first_kex_done_){
        this->first_kex_done_ = true;
        this->state_ = State::Auth;
        this->phase_start_ = millis();
        this->begin(MSG_SERVICE_REQUEST);
        this->putCStr("ssh-userauth");
        this->sendPacket();
        return;
    }
    this->flushPending();
    this->adjustWindow();
}

// ---------------------------------------------------------------- 認証

void SshClient::sendAuthNone(){
    this->auth_method_ = AuthMethod::None;
    this->begin(MSG_USERAUTH_REQUEST);
    this->putCStr(this->user_.c_str());
    this->putCStr("ssh-connection");
    this->putCStr("none");
    this->sendPacket();
}

void SshClient::onAuthFailure(const uint8_t* p, size_t n){
    Reader r(p, n);
    uint32_t mn;
    const uint8_t* m = r.str(mn);
    FixedString<PICO_STR_256B> methods;
    methods.assign((const char*)m, mn);

    switch(this->auth_method_){
        case AuthMethod::PublicKey: this->notice("公開鍵は受け付けられませんでした"); break;
        case AuthMethod::Password: this->notice("パスワードが違います"); break;
        case AuthMethod::Keyboard: this->notice("認証に失敗しました"); break;
        default: break;
    }
    this->nextAuth(methods.c_str());
}

void SshClient::nextAuth(const char* methods){
    this->phase_start_ = millis();
    if(ListHas(methods, "publickey") && this->have_identity_ && !this->tried_pubkey_){
        this->sendAuthPublicKey();
        return;
    }
    if(ListHas(methods, "password") && this->password_attempts_ < 3){
        this->auth_method_ = AuthMethod::Password;
        this->prompt_.assign("パスワード: ");
        this->prompt_echo_ = false;
        this->state_ = State::NeedPassword;
        return;
    }
    if(ListHas(methods, "keyboard-interactive") && this->keyboard_attempts_ < 3){
        this->sendAuthKeyboard();
        return;
    }
    char buf[PICO_STR_LL];
    snprintf(buf, sizeof(buf), "認証に失敗しました(サーバが受け付ける方式: %s)", methods);
    this->fail(buf);
}

void SshClient::sendAuthPublicKey(){
    this->tried_pubkey_ = true;
    this->auth_method_ = AuthMethod::PublicKey;

    uint8_t blob[SshUtil::kEd25519BlobBytes];
    SshUtil::Ed25519Blob(this->id_pub_, blob);

    this->begin(MSG_USERAUTH_REQUEST);
    this->putCStr(this->user_.c_str());
    this->putCStr("ssh-connection");
    this->putCStr("publickey");
    this->putByte(1);
    this->putCStr("ssh-ed25519");
    this->putString(blob, sizeof(blob));
    if(this->tx_overflow_){
        this->sendPacket(); // 失敗として扱われる
        return;
    }

    // 署名するのは string(session_id) + ここまでのペイロード(RFC 4252 7)
    uint8_t msg[36 + 256];
    const size_t body = this->tx_pos_ - 5;
    if(body > sizeof(msg) - 36){
        this->fail("ユーザー名が長すぎます");
        return;
    }
    PutBe32(msg, 32);
    memcpy(msg + 4, this->session_id_, 32);
    memcpy(msg + 36, this->tx_ + 5, body);
    uint8_t sig[64];
    crypto_ed25519_sign(sig, this->id_secret_, msg, 36 + body);

    this->putU32(4 + 11 + 4 + 64);
    this->putCStr("ssh-ed25519");
    this->putString(sig, sizeof(sig));
    this->sendPacket();
}

void SshClient::sendAuthPassword(const char* pw){
    this->auth_method_ = AuthMethod::Password;
    this->begin(MSG_USERAUTH_REQUEST);
    this->putCStr(this->user_.c_str());
    this->putCStr("ssh-connection");
    this->putCStr("password");
    this->putByte(0);
    this->putCStr(pw);
    this->sendPacket(); //送信バッファは暗号化で上書きされる(平文は残らない)
}

void SshClient::sendAuthKeyboard(){
    this->keyboard_attempts_++;
    this->auth_method_ = AuthMethod::Keyboard;
    this->begin(MSG_USERAUTH_REQUEST);
    this->putCStr(this->user_.c_str());
    this->putCStr("ssh-connection");
    this->putCStr("keyboard-interactive");
    this->putCStr("");
    this->putCStr("");
    this->sendPacket();
}

void SshClient::onInfoRequest(const uint8_t* p, size_t n){
    Reader r(p, n);
    uint32_t name_n, inst_n, lang_n;
    const uint8_t* name = r.str(name_n);
    const uint8_t* inst = r.str(inst_n);
    r.str(lang_n);
    const uint32_t count = r.u32();
    if(!r.ok || count > (uint32_t)kMaxPrompts){
        this->fail("認証の問い合わせを読めませんでした");
        return;
    }
    for(uint32_t i = 0; i < count; i++){
        uint32_t pn;
        const uint8_t* pr = r.str(pn);
        this->prompt_echos_[i] = r.byte() != 0;
        this->prompts_[i].assign((const char*)pr, pn);
    }
    if(!r.ok){
        this->fail("認証の問い合わせを読めませんでした");
        return;
    }

    if(name_n > 0){
        FixedString<PICO_STR_L> s;
        s.assign((const char*)name, name_n);
        this->notice(s.c_str());
    }
    if(inst_n > 0){
        FixedString<PICO_STR_LL> s;
        s.assign((const char*)inst, inst_n);
        this->notice(s.c_str());
    }

    this->prompt_count_ = (int)count;
    this->prompt_index_ = 0;
    if(count == 0){
        this->begin(MSG_USERAUTH_INFO_RESPONSE);
        this->putU32(0);
        this->sendPacket();
        return;
    }
    this->askPrompt();
}

void SshClient::askPrompt(){
    this->prompt_ = this->prompts_[this->prompt_index_];
    this->prompt_echo_ = this->prompt_echos_[this->prompt_index_];
    this->state_ = State::NeedPassword;
}

void SshClient::providePassword(const char* text){
    if(this->state_ != State::NeedPassword) return;
    this->phase_start_ = millis();
    this->state_ = State::Auth;

    if(this->auth_method_ == AuthMethod::Keyboard){
        this->answers_[this->prompt_index_].assign(text);
        this->prompt_index_++;
        if(this->prompt_index_ < this->prompt_count_){
            this->askPrompt();
            return;
        }
        this->begin(MSG_USERAUTH_INFO_RESPONSE);
        this->putU32((uint32_t)this->prompt_count_);
        for(int i = 0; i < this->prompt_count_; i++) this->putCStr(this->answers_[i].c_str());
        for(int i = 0; i < kMaxPrompts; i++){
            crypto_wipe((void*)this->answers_[i].c_str(), this->answers_[i].length());
            this->answers_[i].clear();
        }
        this->sendPacket();
        return;
    }
    this->password_attempts_++;
    this->sendAuthPassword(text);
}

// ---------------------------------------------------------------- チャネル

void SshClient::openChannel(){
    this->state_ = State::Opening;
    this->phase_start_ = millis();
    this->begin(MSG_CHANNEL_OPEN);
    if(this->fwd_host_.empty()){
        this->putCStr("session");
        this->putU32(0);
        this->putU32(this->window_);
        this->putU32(kMaxPacket);
    }else{
        //踏み台: 相手の先の host:port へのTCPの通り道(RFC 4254 7.2)
        this->putCStr("direct-tcpip");
        this->putU32(0);
        this->putU32(this->window_);
        this->putU32(kMaxPacket);
        this->putCStr(this->fwd_host_.c_str());
        this->putU32(this->fwd_port_);
        this->putCStr("127.0.0.1");
        this->putU32(0);
    }
    this->sendPacket();
}

void SshClient::onChannelOpenFailure(const uint8_t* p, size_t n){
    char buf[PICO_STR_LL];
    if(this->fwd_host_.empty()){
        this->fail("シェルを開けませんでした(チャネルを拒否されました)");
        return;
    }
    Reader r(p, n);
    r.u32();
    r.u32(); // 理由の番号
    uint32_t dn;
    const uint8_t* d = r.str(dn);
    if(!r.ok) dn = 0;
    if(dn > 80) dn = 80;
    snprintf(buf, sizeof(buf), "踏み台から %s:%u へ繋げませんでした(%.*s)",
             this->fwd_host_.c_str(), (unsigned)this->fwd_port_, (int)dn, (const char*)d);
    this->fail(buf);
}

void SshClient::onChannelOpenConfirm(const uint8_t* p, size_t n){
    Reader r(p, n);
    r.u32();
    this->remote_id_ = r.u32();
    this->remote_window_ = r.u32();
    this->remote_max_ = r.u32();
    if(!r.ok){
        this->fail("チャネルの応答が壊れています");
        return;
    }
    if(!this->fwd_host_.empty()){
        //踏み台の通り道はこれで開通(端末もシェルも要らない)
        this->state_ = State::Open;
        this->flushPending();
        return;
    }

    // 端末モード: VERASE(3)=0x7F, IUTF8(42)=1, 終わり(0)
    static const uint8_t modes[] = { 3, 0, 0, 0, 127, 42, 0, 0, 0, 1, 0 };
    this->begin(MSG_CHANNEL_REQUEST);
    this->putU32(this->remote_id_);
    this->putCStr("pty-req");
    this->putByte(1);
    this->putCStr("xterm-256color");
    this->putU32((uint32_t)this->term_cols_);
    this->putU32((uint32_t)this->term_rows_);
    this->putU32((uint32_t)this->term_wpx_);
    this->putU32((uint32_t)this->term_hpx_);
    this->putString(modes, sizeof(modes));
    if(!this->sendPacket()) return;

    this->begin(MSG_CHANNEL_REQUEST);
    this->putU32(this->remote_id_);
    this->putCStr("shell");
    this->putByte(1);
    if(!this->sendPacket()) return;

    this->replies_expected_ = 2;
    this->replies_seen_ = 0;
}

void SshClient::onChannelReply(bool ok){
    if(this->replies_seen_ >= this->replies_expected_) return;
    this->replies_seen_++;
    if(this->replies_seen_ == 1){
        if(!ok) this->notice("端末(pty)を確保できませんでした");
        return;
    }
    if(!ok){
        this->fail("シェルを開けませんでした");
        return;
    }
    this->state_ = State::Open;
    this->flushPending();
}

void SshClient::onChannelData(const uint8_t* data, size_t n){
    if(this->chan_fn_) this->chan_fn_(this->chan_ctx_, data, n);
    else this->output(data, n);
    if(this->manual_window_) return; // 渡し先が consume() で知らせる
    this->consumed_ += (uint32_t)n;
    this->adjustWindow();
}

void SshClient::sendWindowChange(){
    this->begin(MSG_CHANNEL_REQUEST);
    this->putU32(this->remote_id_);
    this->putCStr("window-change");
    this->putByte(0);
    this->putU32((uint32_t)this->term_cols_);
    this->putU32((uint32_t)this->term_rows_);
    this->putU32((uint32_t)this->term_wpx_);
    this->putU32((uint32_t)this->term_hpx_);
    this->sendPacket();
}

void SshClient::flushPending(){
    if(this->kex_in_progress_ || this->state_ != State::Open) return;
    size_t off = 0;
    while(off < this->pending_len_){
        size_t allow = this->pending_len_ - off;
        if(allow > this->remote_window_) allow = this->remote_window_;
        if(allow > this->remote_max_) allow = this->remote_max_;
        if(allow > 512) allow = 512;
        if(allow == 0) break; //相手の窓が空くまで残しておく(WINDOW_ADJUSTでまた呼ばれる)
        this->begin(MSG_CHANNEL_DATA);
        this->putU32(this->remote_id_);
        this->putString(this->pending_out_ + off, allow);
        if(!this->sendPacket()) return;
        this->remote_window_ -= (uint32_t)allow;
        off += allow;
    }
    memmove(this->pending_out_, this->pending_out_ + off, this->pending_len_ - off);
    this->pending_len_ -= off;
}

bool SshClient::send(const uint8_t* data, size_t len){
    if(this->state_ != State::Open) return false;
    if(this->pending_len_ + len > sizeof(this->pending_out_)) return false;
    memcpy(this->pending_out_ + this->pending_len_, data, len);
    this->pending_len_ += len;
    this->flushPending();
    return this->state_ != State::Closed;
}

bool SshClient::send(const char* s){
    return this->send((const uint8_t*)s, strlen(s));
}

// ---------------------------------------------------------------- 踏み台の通り道

void SshTunnel::attach(SshClient* jump){
    this->jump_ = jump;
    this->head_ = 0;
    this->count_ = 0;
    this->overflow_ = false;
    jump->setChannelSink(&SshTunnel::OnData, this);
    jump->setWindow((uint32_t)kCap, true);
}

void SshTunnel::OnData(void* ctx, const uint8_t* data, size_t len){
    SshTunnel* t = static_cast<SshTunnel*>(ctx);
    //受信窓を kCap にしてあるので本来は溢れない。溢れたら通り道ごと壊れたとみなす
    if(t->count_ + len > kCap){
        t->overflow_ = true;
        return;
    }
    size_t tail = (t->head_ + t->count_) % kCap;
    for(size_t i = 0; i < len; i++){
        t->buf_[tail] = data[i];
        tail = (tail + 1) % kCap;
    }
    t->count_ += len;
}

int SshTunnel::read(uint8_t* buf, size_t n){
    if(n > this->count_) n = this->count_;
    for(size_t i = 0; i < n; i++){
        buf[i] = this->buf_[this->head_];
        this->head_ = (this->head_ + 1) % kCap;
    }
    this->count_ -= n;
    if(n > 0 && this->jump_) this->jump_->consume(n);
    return (int)n;
}

bool SshTunnel::write(const uint8_t* buf, size_t n){
    return this->jump_ && this->jump_->send(buf, n);
}

bool SshTunnel::connected(){
    if(this->overflow_) return false;
    return this->jump_ && this->jump_->isOpen();
}

void SshTunnel::stop(){
    //中のSSHが終わったら踏み台も用済み
    if(this->jump_) this->jump_->disconnect();
}
