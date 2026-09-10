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
    FixedString<PICO_STR_M> ssid;
    FixedString<PICO_STR_L> password;
    bool is_ok = PICO_Config::ParseFile(PICO_Path::FILE::CFG::SYS_NETWORK_CFG,
        [&](const char* key, const char* value){
            if(strcmp(key, "wifi-ssid") == 0){
                ssid.assign(value);
            }else if(strcmp(key, "wifi-password") == 0){
                password.assign(value);
            }else if(strcmp(key, "ntp-server-1") == 0){
                ntpServer1.assign(value);
            }else if(strcmp(key, "ntp-server-2") == 0){
                ntpServer2.assign(value);
            }
        }
    );
    if(is_ok){
        if(!ssid.empty() && !password.empty()){
            ConnectWiFiAsync(ssid.c_str(), password.c_str());
        }else{
            LOG_SYS_WARN("Network Setup: To connect Wi-Fi, SSID & Password is essential.");
        }
    }
};

void NetworkFunctions::Update(){
    switch(currentStatus){
        case NetStatus::TRYING_CONNECT:
            if(WiFi.status() == WL_CONNECTED){
                LOG_SYS_OK("Succeeded to connect Wi-Fi!");
                currentStatus = NetStatus::SUCCESS;
                healthCheckTimer = millis();
                // 初回接続・再接続どちらの経路でもここを通るので、
                // 再接続時にもNTPを即座に再同期させて時刻ドリフトを補正する。
                NTP.begin(ntpServer1.c_str(), ntpServer2.c_str());
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

        case NetStatus::SUCCESS:
            // 定期的にWi-Fiの生存確認を行い、切断を検知したら再接続交渉を行う。
            if(millis() - healthCheckTimer > HEALTH_CHECK_INTERVAL){
                healthCheckTimer = millis();
                if(WiFi.status() != WL_CONNECTED){
                    LOG_SYS_WARN("Wi-Fi disconnected. Trying to reconnect.");
                    ConnectWiFiAsync(currentSSID.c_str(), currentPassword.c_str());
                }
            }
            break;

        // SSID_NOT_FOUND / FAILED / TIMEOUT は現状放置(自動リトライしない)。
        // 必要になったら、ここに一定間隔でのConnectWiFiAsync再試行を追加する。
        default:
            break;
    };
};

void NetworkFunctions::ConnectWiFiAsync(const char* ssid, const char* password){
    LOG_SYS_MSG("Network Service: Connecting to Wi-Fi.");
    currentSSID.assign(ssid);
    currentPassword.assign(password);
    WiFi.beginNoBlock(ssid, password);
    timer = millis();
    currentStatus = NetStatus::TRYING_CONNECT;
};

Task* NetworkFunctions::ScanAsync(){
    NetworkScan* task = new NetworkScan();
    PICO_Task::Add(task);
    return task;
};
