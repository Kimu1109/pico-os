#pragma once

// ---------------------------------------------------------------------------
// PCビルド / ホストテスト共用のTLSクライアント(OpenSSL)。
//
// 実機(arduino-pico)の BearSSL::WiFiClientSecure / BearSSL::X509List と同じ呼び方に
// 揃えてあるので、src/側(net/Http_Transport)はPCと実機で同じコードのまま動く。
//
// 実機のBearSSLに合わせて**TLS 1.2に固定**している。PCのOpenSSLだけTLS 1.3で
// 繋がってしまうと、「PCでは読めるのに実機では繋がらないサーバ」に気づけないため。
// 信頼する証明書も実機と同じ(net/Tls_Roots が渡すもの)だけを使い、OSの証明書ストアは見ない。
//
// 実機と違うところ:
//   - setX509Time() は無視する(OpenSSLはOSの時計で有効期限を見る)
//   - setBufferSizes() は無視する(OpenSSLが自分で持つ)
// ---------------------------------------------------------------------------

#include "WiFiClient_PC.h"

#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <vector>

namespace BearSSL {

class X509List {
public:
    X509List() = default;
    explicit X509List(const char* pem){ append(pem); }
    ~X509List(){
        for(X509* x : certs_) X509_free(x);
    }

    X509List(const X509List&) = delete;
    X509List& operator=(const X509List&) = delete;

    // PEM(複数の証明書を連結したものでもよい)を足す。1枚も読めなければfalse
    bool append(const char* pem){
        if(!pem) return false;
        BIO* bio = BIO_new_mem_buf(pem, -1);
        if(!bio) return false;

        int added = 0;
        while(X509* x = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr)){
            certs_.push_back(x);
            added++;
        }
        ERR_clear_error(); //末尾で読むものが無くなったときのエラーは正常
        BIO_free(bio);
        return added > 0;
    }

    size_t getCount() const { return certs_.size(); }
    const std::vector<X509*>& certs() const { return certs_; }

private:
    std::vector<X509*> certs_;
};

class WiFiClientSecure : public WiFiClientPC {
public:
    WiFiClientSecure() = default;
    ~WiFiClientSecure() override { WiFiClientSecure::stop(); }

    void setTrustAnchors(const X509List* ta){ ta_ = ta; insecure_ = false; }
    void setInsecure(){ insecure_ = true; }
    void setX509Time(time_t){}
    void setBufferSizes(int, int){}

    // 実機と同じく、直近のTLSの失敗理由。0なら失敗していない(TCPで繋がらなかった等)
    int getLastSSLError(char* dest = nullptr, size_t len = 0){
        if(dest && len > 0) snprintf(dest, len, "%s", err_text_);
        return err_code_;
    }

    int connect(const char* host, uint16_t port) override {
        stop();
        err_code_ = 0;
        err_text_[0] = '\0';

        if(WiFiClientPC::connect(host, port) != 1) return 0;

        ctx_ = SSL_CTX_new(TLS_client_method());
        if(!ctx_) return failWith(-1, "SSL_CTX_newに失敗");
        SSL_CTX_set_min_proto_version(ctx_, TLS1_2_VERSION);
        SSL_CTX_set_max_proto_version(ctx_, TLS1_2_VERSION);

        if(insecure_){
            SSL_CTX_set_verify(ctx_, SSL_VERIFY_NONE, nullptr);
        }else{
            X509_STORE* store = SSL_CTX_get_cert_store(ctx_);
            if(ta_){
                for(X509* x : ta_->certs()) X509_STORE_add_cert(store, x);
            }
            SSL_CTX_set_verify(ctx_, SSL_VERIFY_PEER, nullptr);
        }

        ssl_ = SSL_new(ctx_);
        if(!ssl_) return failWith(-1, "SSL_newに失敗");
        SSL_set_fd(ssl_, fd());
        SSL_set_tlsext_host_name(ssl_, host); //SNI
        if(!insecure_) SSL_set1_host(ssl_, host); //証明書の名前と突き合わせる

        //fdは非ブロッキングなので、読み書きを待ちながらハンドシェイクを進める
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs());
        while(true){
            const int r = SSL_connect(ssl_);
            if(r == 1) break;

            const int e = SSL_get_error(ssl_, r);
            if(std::chrono::steady_clock::now() > deadline) return failWith(-1, "ハンドシェイクが時間切れ");
            if(e == SSL_ERROR_WANT_READ){
                if(!waitReadable(fd())) return failWith(-1, "ハンドシェイクが時間切れ");
                continue;
            }
            if(e == SSL_ERROR_WANT_WRITE){
                if(!waitWritable(fd())) return failWith(-1, "ハンドシェイクが時間切れ");
                continue;
            }

            const long verify = SSL_get_verify_result(ssl_);
            if(verify != X509_V_OK) return failWith((int)verify, X509_verify_cert_error_string(verify));

            char buf[160];
            ERR_error_string_n(ERR_get_error(), buf, sizeof(buf));
            return failWith(-1, buf);
        }
        return 1;
    }

    int available() override {
        if(!ssl_) return 0;
        fill();
        return (int)(buf_len_ - buf_pos_);
    }

    int read(uint8_t* buf, size_t size) override {
        if(!ssl_ || !buf || size == 0) return 0;
        fill();
        const size_t avail = buf_len_ - buf_pos_;
        const size_t n = (size < avail) ? size : avail;
        memcpy(buf, buf_ + buf_pos_, n);
        buf_pos_ += n;
        return (int)n;
    }

    bool connected() override {
        if(!ssl_) return false;
        fill();
        if(buf_len_ > buf_pos_) return true; //読み残しがあるうちは接続中として扱う(実機と同じ)
        return !peer_closed_;
    }

    size_t write(const uint8_t* buf, size_t size) override {
        if(!ssl_ || !buf) return 0;
        size_t sent = 0;
        while(sent < size){
            const int n = SSL_write(ssl_, buf + sent, (int)(size - sent));
            if(n > 0){ sent += (size_t)n; continue; }
            const int e = SSL_get_error(ssl_, n);
            if(e == SSL_ERROR_WANT_WRITE && waitWritable(fd())) continue;
            if(e == SSL_ERROR_WANT_READ && waitReadable(fd())) continue;
            break;
        }
        return sent;
    }

    void stop() override {
        if(ssl_){
            SSL_shutdown(ssl_); //相手の返事は待たない
            SSL_free(ssl_);
            ssl_ = nullptr;
        }
        if(ctx_){
            SSL_CTX_free(ctx_);
            ctx_ = nullptr;
        }
        buf_pos_ = buf_len_ = 0;
        peer_closed_ = false;
        WiFiClientPC::stop();
    }

private:
    // 復号済みの本文を1回ぶん溜めておく。available()で「読める量」を答えるため
    // (OpenSSLのSSL_pending()はレコードを読み始めるまで0を返す)
    void fill(){
        if(buf_pos_ < buf_len_ || peer_closed_) return;
        buf_pos_ = buf_len_ = 0;

        const int n = SSL_read(ssl_, buf_, (int)sizeof(buf_));
        if(n > 0){
            buf_len_ = (size_t)n;
            return;
        }
        const int e = SSL_get_error(ssl_, n);
        if(e != SSL_ERROR_WANT_READ && e != SSL_ERROR_WANT_WRITE){
            peer_closed_ = true; //close_notify / 相手が切った / 壊れた
        }
        ERR_clear_error();
    }

    int failWith(int code, const char* text){
        err_code_ = code;
        snprintf(err_text_, sizeof(err_text_), "%s", text ? text : "");
        stop();
        return 0;
    }

    const X509List* ta_ = nullptr;
    bool insecure_ = false;

    SSL_CTX* ctx_ = nullptr;
    SSL* ssl_ = nullptr;

    uint8_t buf_[4096];
    size_t buf_pos_ = 0;
    size_t buf_len_ = 0;
    bool peer_closed_ = false;

    int err_code_ = 0;
    char err_text_[160] = {};
};

} // namespace BearSSL

// 実機の WiFiClientSecure.h と同じく、名前空間を開いて置く
using namespace BearSSL;
