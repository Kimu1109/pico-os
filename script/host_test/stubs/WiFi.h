// ホストテスト用のWi-Fi代替。
//
// 状態まわり(接続/スキャン)はここでは要らないので、Network_Functions.hpp の
// インライン関数が参照する最小限(scanDelete)だけを置いている。
// **TCPクライアントだけはPCビルドと同じ実装を使う** — 通信経路をテストで
// 確かめる意味が無くなるため、別物を用意することはしない。
#pragma once

#include "../../../pc/compat/WiFiClient_PC.h"

struct HostWiFiStub {
    void scanDelete(){}
    // 省電力モード(記録するだけ。0=高性能, 1=既定, 2=積極的な省電力)
    int power_mode = 1;
    void lowPowerMode(){ power_mode = 2; }
    void defaultLowPowerMode(){ power_mode = 1; }
    void noLowPowerMode(){ power_mode = 0; }
};
inline HostWiFiStub WiFi;
