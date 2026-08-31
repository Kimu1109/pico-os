#include "functions/Network_Functions.hpp"
#include "functions/Task_Functions.hpp"
#include "functions/Config_Functions.hpp"
#include "task/NetworkScan.hpp"
#include "storage/SD_Path.hpp"

IconID NetworkFunctions::GetWifiStateIconID(){
    if(currentStatus == NetStatus::SUCCESS){
        int32_t rssi = WiFi.RSSI();
        if (rssi >= -50) return IconID::WifiSignal4;
        if (rssi >= -65) return IconID::WifiSignal3;
        if (rssi >= -80) return IconID::WifiSignal2;
        return IconID::WifiSignal1;
    }else{
        return IconID::WifiOff;
    }
}

void NetworkFunctions::Setup(){
    char ssid[33] = "";
    char password[65] = "";
    bool is_ok = PICO_Config::ParseFile(PICO_Path::FILE::SYS_NETWORK_CFG,
        [&](const char* key, const char* value){
            if(strcmp(key, "wifi-ssid") == 0){
                strncpy(ssid, value, sizeof(ssid) - 1);
            }else if(strcmp(key, "wifi-password") == 0){
                strncpy(password, value, sizeof(password) - 1);
            }else if(strcmp(key, "ntp-server-1") == 0){
                strncpy(ntpServer1, value, sizeof(ntpServer1) - 1);
            }else if(strcmp(key, "ntp-server-2") == 0){
                strncpy(ntpServer2, value, sizeof(ntpServer2) - 1);
            }
        }
    );
    if(is_ok){
        if(ssid[0] != '\0' && password[0] != '\0'){
            ConnectWiFiAsync(ssid, password);
        }
    }
};

void NetworkFunctions::Update(){
    switch(currentStatus){
        case NetStatus::TRYING_CONNECT:
            if(WiFi.status() == WL_CONNECTED){
                LOG_SYS_OK("Succeeded To connect Wi-Fi!");
                currentStatus = NetStatus::SUCCESS;
                NTP.begin(ntpServer1, ntpServer2);
                break;
            }

            if(millis() - timer > 1000 * 10){
                LOG_SYS_FAIL("Failed to connect Wi-Fi!");
                switch(WiFi.status()){
                    case WL_NO_SSID_AVAIL:
                        currentStatus = NetworkFunctions::NetStatus::SSID_NOT_FOUND;
                        break;
                    case WL_CONNECT_FAILED:
                        currentStatus = NetworkFunctions::NetStatus::FAILED;
                        break;
                    case WL_DISCONNECTED:
                    case WL_CONNECTION_LOST:
                    default:
                        currentStatus = NetworkFunctions::NetStatus::TIMEOUT;
                        break;
                }
            }
            break;
    };
};

void NetworkFunctions::ConnectWiFiAsync(const char* ssid, const char* password){
    strncpy(currentSSID, ssid, sizeof(currentSSID) - 1);
    WiFi.beginNoBlock(ssid, password);
    timer = millis();
    currentStatus = NetStatus::TRYING_CONNECT;
};

Task* NetworkFunctions::ScanAsync(){
    NetworkScan* task = new NetworkScan();
    PICO_Task::Add(task);
    return task;
};