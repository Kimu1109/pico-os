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
    inline char ntpServer1[33] = "ntp.nict.jp";
    inline char ntpServer2[33] = "time.google.com";

    inline unsigned long timer = 0;

    void Setup();
    void Update();

    IconID GetWifiStateIconID();
    
    void ConnectWiFiAsync(const char* ssid, const char* password);
    Task* ScanAsync();
    
    inline void ScanResultClear(){
        WiFi.scanDelete();
    }
};