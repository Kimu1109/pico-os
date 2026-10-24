// PCビルド用のmDNS(arduino-picoのSimpleMDNS)の代替。
// 母艦のmDNS(avahi等)と名前がぶつからないよう、PCでは実際には応答しない。名乗った名前を覚えるだけ
#pragma once

#include <cstdio>
#include <cstring>

class SimpleMDNS {
public:
    bool begin(const char* hostname, unsigned int = 60){
        if(running_) return false;
        snprintf(hostname_, sizeof(hostname_), "%s", hostname ? hostname : "");
        running_ = true;
        printf("[PC] mDNS: %s.local として名乗ったことにします(実際には応答しません)\n", hostname_);
        return true;
    }
    void end(){ running_ = false; }
    void update(){}
    bool running() const { return running_; }
    const char* hostname() const { return hostname_; }
private:
    bool running_ = false;
    char hostname_[64] = {0};
};
inline SimpleMDNS MDNS;
