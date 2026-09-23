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
};
inline HostWiFiStub WiFi;
