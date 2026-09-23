#pragma once

#include "util/Url.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

#include "WiFi.h"
#include "WiFiClientSecure.h"

#include <cstddef>
#include <cstdint>

// HTTPの下の「繋ぐ」部分。URLが http:// なら素のTCP、https:// ならTLSで繋ぐ。
//
// HttpGet(文書/カレンダー)と HttpRequest(Luaのpico.http_request)の両方が使う。
// どちらもここを通すので、schemeを見て分岐するのはこのクラスだけになる
// (Url型がschemeを閉じ込めているのはこのため)。
//
// TLSは実機では arduino-pico 同梱の BearSSL(WiFiClientSecure)、PCではOpenSSL
// (pc/compat/WiFiClientSecure_PC.h)で、どちらも同じ呼び方で使える。
//
// **TLSの道具一式は繋いでいる間だけ確保する。** BearSSLは受信バッファ16KB +
// 専用スタック6.4KB + 信頼の起点(ルート1枚あたり約1.5KB)等で、合わせて約40KBを使う。
// 常駐させると使わないアプリにまで負担させるので、connect()で確保して close()で返す。
//
// **TLSのハンドシェイクは connect() の中で同期的に進む。** 素のTCPの接続と同じく、
// 終わるまで画面が止まる(実機で1〜2秒程度の見込み。相手が黙っていれば最大15秒)。
class HttpTransport {
    public:
        enum class Error : uint8_t {
            None,
            Connect,     // TCPで繋がらない(名前が引けない/到達しない)
            ClockNotSet, // 時計が合っていない。証明書の有効期限が確かめられない
            Tls,         // 証明書が信頼できない/名前が違う/ハンドシェイクの失敗
            NoMemory,    // TLSの道具一式を確保できない
        };

        HttpTransport() = default;
        ~HttpTransport(){ close(); }

        HttpTransport(const HttpTransport&) = delete;
        HttpTransport& operator=(const HttpTransport&) = delete;

        // 繋ぐ。成功でtrue。失敗の理由は error()/errorText()
        bool connect(const Url& url, unsigned long timeout_ms);

        int available();
        int read(uint8_t* buf, size_t size);
        size_t write(const uint8_t* buf, size_t size);
        bool connected();

        // 切断し、TLSの道具一式も返す
        void close();

        Error error() const { return err_; }
        // 画面へそのまま出せる短い日本語(TLSの失敗ならライブラリの理由も付く)
        const char* errorText() const { return err_text_.c_str(); }

        // これより前の時計は「合っていない」とみなす(NTP同期前は1970年から始まる)
        static constexpr uint32_t kMinValidEpoch = 1577836800u; // 2020-01-01

    private:
        WiFiClient plain_;
        WiFiClientSecure* tls_ = nullptr;
        X509List* roots_ = nullptr;
        WiFiClient* active_ = nullptr;

        Error err_ = Error::None;
        FixedString<PICO_STR_LL> err_text_;

        bool fail(Error e, const char* text);
};

namespace TlsRoots {
    // 信頼するルート証明書を out へ足す。焼き込みのもの(net/Tls_Roots_Data.hpp)に加え、
    // SDの /sys/tls/ca.pem があればそれも足す(自己署名の自前サーバ等)。足した枚数を返す
    int AppendTo(X509List& out);
}
