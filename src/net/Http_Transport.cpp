#include "net/Http_Transport.hpp"
#include "net/Tls_Roots_Data.hpp"

#include "OS_Data.hpp"
#include "functions/Log_Functions.hpp"
#include "storage/SD_Path.hpp"

#include <cstdlib>
#include <ctime>
#include <new>

namespace {
    // /sys/tls/ca.pem の上限。ルート1枚のPEMは2KB前後なので十分
    constexpr size_t kMaxExtraPemBytes = 16 * 1024;
}

int TlsRoots::AppendTo(X509List& out){
    int added = 0;
    if(out.append(TlsRootsData::kPem)) added += TlsRootsData::kCount;

    if(!OSData::SD_usable) return added;
    FsFile f = OSData::SD.open(PICO_Path::FILE::TLS_EXTRA_CA_PEM, O_RDONLY);
    if(!f) return added;

    const size_t size = (size_t)f.size();
    if(size == 0 || size > kMaxExtraPemBytes){
        LOG_SYS_WARN("TLS: %s が大きすぎるか空なので読みません (%u B)",
                     PICO_Path::FILE::TLS_EXTRA_CA_PEM, (unsigned)size);
        f.close();
        return added;
    }

    //X509List::append()はNUL終端のPEM文字列を丸ごと受け取るので、一度だけRAMへ載せる
    //(接続の間だけの一時確保。読み終えたらすぐ返す)
    char* buf = (char*)malloc(size + 1);
    if(buf){
        const int got = f.read(buf, size);
        buf[(got > 0) ? got : 0] = '\0';
        if(got > 0 && out.append(buf)){
            added++;
        }else{
            LOG_SYS_WARN("TLS: %s を証明書として読めません", PICO_Path::FILE::TLS_EXTRA_CA_PEM);
        }
        free(buf);
    }
    f.close();
    return added;
}

bool HttpTransport::fail(Error e, const char* text){
    err_ = e;
    err_text_.assign(text ? text : "");
    close();
    return false;
}

bool HttpTransport::connect(const Url& url, unsigned long timeout_ms){
    close();
    err_ = Error::None;
    err_text_.clear();

    if(!url.secure){
        plain_.setTimeout(timeout_ms);
        if(plain_.connect(url.host.c_str(), url.port) != 1){
            return fail(Error::Connect, "接続できない");
        }
        active_ = &plain_;
        return true;
    }

    //証明書の有効期限は時計で確かめる。NTP同期前(1970年)のまま繋ぐと、
    //実機では全ての証明書が「まだ有効でない」として弾かれ、理由が分かりにくい
    const time_t now = time(nullptr);
    if((uint32_t)now < kMinValidEpoch){
        return fail(Error::ClockNotSet, "時計が合っていない(NTP同期前)");
    }

    roots_ = new (std::nothrow) X509List();
    tls_ = new (std::nothrow) WiFiClientSecure();
    if(!roots_ || !tls_) return fail(Error::NoMemory, "TLSの準備に必要なメモリが無い");

    if(TlsRoots::AppendTo(*roots_) == 0){
        return fail(Error::Tls, "信頼するルート証明書を読めない");
    }
    tls_->setTrustAnchors(roots_);
    tls_->setX509Time(now);
    tls_->setTimeout(timeout_ms);

    if(tls_->connect(url.host.c_str(), url.port) != 1){
        char why[96];
        why[0] = '\0';
        const int code = tls_->getLastSSLError(why, sizeof(why));
        if(code == 0){
            //TLSまで行っていない = TCPで繋がらなかった
            return fail(Error::Connect, "接続できない");
        }
        LOG_SYS_WARN("TLS: %s:%u へ繋げません (%d: %s)", url.host.c_str(), (unsigned)url.port, code, why);
        FixedString<PICO_STR_LL> text("証明書を確認できない: ");
        text.append(why);
        return fail(Error::Tls, text.c_str());
    }

    active_ = tls_;
    return true;
}

int HttpTransport::available(){
    return active_ ? active_->available() : 0;
}

int HttpTransport::read(uint8_t* buf, size_t size){
    return active_ ? active_->read(buf, size) : 0;
}

size_t HttpTransport::write(const uint8_t* buf, size_t size){
    return active_ ? active_->write(buf, size) : 0;
}

bool HttpTransport::connected(){
    return active_ ? active_->connected() : false;
}

void HttpTransport::close(){
    plain_.stop();
    if(tls_){
        tls_->stop();
        delete tls_;
        tls_ = nullptr;
    }
    //信頼の起点はTLSの接続が参照しているので、接続を壊した後で返す
    if(roots_){
        delete roots_;
        roots_ = nullptr;
    }
    active_ = nullptr;
}
