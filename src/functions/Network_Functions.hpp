#pragma once
#include <WiFi.h>
#include "task/Task.hpp"

namespace NetworkFunctions {

    enum class NetStatus
    {
        SUCCESS,
        TIMEOUT,
        SSID_NOT_FOUND,
        FAILED
    };

    //NOT TO WRITE! READONLY!
    inline NetStatus currentStatus = NetStatus::FAILED;
    inline String currentSSID = "";

    void Setup();
    
    Task* ConnectWiFiAsync(const char* ssid, const char* password);
    Task* ScanAsync();
    inline void ScanResultClear(){
        WiFi.scanDelete();
    }
};