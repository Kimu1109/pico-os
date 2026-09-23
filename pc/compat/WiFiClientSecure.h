#pragma once

// arduino-pico の <WiFiClientSecure.h>(BearSSL)の代わり。
// ネイティブはOpenSSLで本当にTLSを喋る(WiFiClientSecure_PC.h)。
// Webビルドはそもそも生のソケットが無い(WiFiClient_PC.hの説明参照)ので、
// 同じ呼び方ができるだけの「常に繋がらない」スタブにしてある。
#include "WiFi.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>

#if defined(__EMSCRIPTEN__)

namespace BearSSL {

class X509List {
public:
    X509List() = default;
    explicit X509List(const char*){}
    bool append(const char*){ return true; }
    size_t getCount() const { return 0; }
};

class WiFiClientSecure : public WiFiClientPC {
public:
    void setTrustAnchors(const X509List*){}
    void setInsecure(){}
    void setX509Time(time_t){}
    void setBufferSizes(int, int){}
    int getLastSSLError(char* dest = nullptr, size_t len = 0){
        if(dest && len > 0) snprintf(dest, len, "%s", "Webビルドはhttps非対応");
        return -1;
    }
    int connect(const char*, uint16_t) override { return 0; }
};

} // namespace BearSSL

using namespace BearSSL;

#else
#include "WiFiClientSecure_PC.h"
#endif
