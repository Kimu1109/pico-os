// PCビルド用のWi-Fi代替。
//
// PCではWi-Fiを扱わないので「接続されていない/スキャン結果0件」として振る舞う。
// Network_Functionsはこの状態でも破綻せず、ステータスバーが切断アイコンを出すだけになる。
#pragma once

#include <cstdint>

enum {
    WL_IDLE_STATUS = 0,
    WL_NO_SSID_AVAIL,
    WL_SCAN_COMPLETED,
    WL_CONNECTED,
    WL_CONNECT_FAILED,
    WL_CONNECTION_LOST,
    WL_DISCONNECTED,
};

class WiFiClassPC {
public:
    void beginNoBlock(const char*, const char*){}
    void begin(const char*, const char*){}
    void disconnect(){}
    int  status(){ return WL_DISCONNECTED; }

    //スキャンは常に0件。-1(実行中)を返し続けると待ち続けるので0を返す
    int  scanNetworks(bool = false){ return 0; }
    int  scanComplete(){ return 0; }
    void scanDelete(){}

    const char* SSID(){ return ""; }
    const char* SSID(int){ return ""; }
    int32_t RSSI(){ return 0; }
    int32_t RSSI(int){ return 0; }
};
inline WiFiClassPC WiFi;

// arduino-picoが提供しているNTPオブジェクトの代替。
// PCでは時刻同期を行わない(TimeFunctionsは未同期のまま動く)
class NTPClassPC {
public:
    void begin(const char* = nullptr, const char* = nullptr){}
    void end(){}
    bool waitSet(unsigned long = 0){ return false; }
    bool running(){ return false; }
};
inline NTPClassPC NTP;
