#pragma once
#include <WiFi.h>
#include "task/Task.hpp"
#include "gui/icons/icons_data.h"

namespace NetworkFunctions {

    enum class NetStatus
    {
        SUCCESS,
        TIMEOUT,
        SSID_NOT_FOUND,
        FAILED,
        TRYING_CONNECT,
    };

    //NOT TO WRITE! READONLY!
    inline NetStatus currentStatus = NetStatus::FAILED;
    inline char currentSSID[33] = "";
    inline char currentPassword[65] = ""; // 再接続用に保持(SetupやConnectWiFiAsync経由で設定される)
    inline char ntpServer1[33] = "ntp.nict.jp";
    inline char ntpServer2[33] = "time.google.com";

    inline unsigned long timer = 0;

    // --- 定期的な再接続交渉まわり ---
    // SUCCESS状態中、一定間隔でWiFi.status()を確認し、
    // 切断を検知したらConnectWiFiAsync()を呼び直してTRYING_CONNECTへ戻す。
    inline unsigned long healthCheckTimer = 0;
    constexpr unsigned long HEALTH_CHECK_INTERVAL = 5000; // ms、生存確認の間隔

    void Setup();
    void Update();

    IconID GetWifiStateIconID();
    
    void ConnectWiFiAsync(const char* ssid, const char* password);
    Task* ScanAsync();
    
    inline void ScanResultClear(){
        WiFi.scanDelete();
    }
};
