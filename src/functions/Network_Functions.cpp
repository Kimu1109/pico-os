#include "functions/Network_Functions.hpp"
#include "functions/Config_Functions.hpp"
#include "task/NetworkScan.hpp"
#include "storage/SD_Path.hpp"
#include "net/Wifi_Profiles.hpp"

IconID NetworkFunctions::GetWifiStateIconID(){
    //圏外でも最弱の棒を返す。バツ印は呼び出し側が重ねる
    if(!IsConnected()) return IconID::WifiSignal1;

    int32_t rssi = WiFi.RSSI();
    if (rssi >= -50) return IconID::WifiSignal4;
    if (rssi >= -65) return IconID::WifiSignal3;
    if (rssi >= -80) return IconID::WifiSignal2;
    return IconID::WifiSignal1;
}

namespace {
    // 接続を始めるだけ(手動か自動かの区別をしない内側の口)
    void StartConnect(const char* ssid, const char* password){
        LOG_SYS_MSG("Network Service: Connecting to Wi-Fi (%s).", ssid);
        NetworkFunctions::currentSSID.assign(ssid);
        NetworkFunctions::currentPassword.assign(password);
        WiFi.beginNoBlock(ssid, password);
        NetworkFunctions::timer = millis();
        NetworkFunctions::currentStatus = NetworkFunctions::NetStatus::TRYING_CONNECT;
    }

    // 保存済みのネットワークを「直近で接続できた順」に1つ試す
    void AutoConnectNext(){
        const int count = WifiProfiles::Count();
        if(count <= 0) return;
        const int idx = NetworkFunctions::retryRank % count;
        NetworkFunctions::retryRank = idx + 1;
        const WifiProfiles::Profile* p = WifiProfiles::At(idx);
        StartConnect(p->ssid.c_str(), p->password.c_str());
    }
}

void NetworkFunctions::Setup(){
    PICO_Config::ParseFile(PICO_Path::FILE::CFG::SYS_NETWORK_CFG,
        [&](const char* key, const char* value){
            if(strcmp(key, "ntp-server-1") == 0){
                ntpServer1.assign(value);
            }else if(strcmp(key, "ntp-server-2") == 0){
                ntpServer2.assign(value);
            }
        }
    );

    // 保存済みのネットワーク(無ければnetwork.cfgの旧形式 wifi-ssid / wifi-password から取り込む)
    WifiProfiles::Load();
    retryRank = 0;
    if(!WifiProfiles::IsEnabled()){
        currentStatus = NetStatus::OFF;
        LOG_SYS_MSG("Network Setup: Wi-FiはOFFです。");
    }else if(WifiProfiles::Count() > 0){
        AutoConnectNext(); // 直近で接続したネットワークから
    }else{
        LOG_SYS_WARN("Network Setup: 保存済みのWi-Fiネットワークがありません。");
    }
};

void NetworkFunctions::Update(){
    switch(currentStatus){
        case NetStatus::TRYING_CONNECT:
            if(WiFi.status() == WL_CONNECTED){
                LOG_SYS_OK("Succeeded to connect Wi-Fi!");
                currentStatus = NetStatus::SUCCESS;
                healthCheckTimer = millis();
                retryRank = 0;
                // 直近で接続したネットワークとして先頭へ(次の起動・次の自動接続はここから)
                WifiProfiles::MarkConnected(currentSSID.c_str());
                if(low_power) WiFi.lowPowerMode(); // 再接続でモードが既定へ戻っていても掛け直す
                // 初回接続・再接続どちらの経路でもここを通るので、
                // 再接続時にもNTPを即座に再同期させて時刻ドリフトを補正する。
                NTP.begin(ntpServer1.c_str(), ntpServer2.c_str());
                break;
            }

            if(millis() - timer > 1000 * 10){
                LOG_SYS_FAIL("Failed to connect Wi-Fi! (%s)", currentSSID.c_str());
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
                retryTimer = millis();
            }
            break;

        case NetStatus::SUCCESS:
            // 定期的にWi-Fiの生存確認を行い、切断を検知したら再接続交渉を行う。
            if(millis() - healthCheckTimer > HEALTH_CHECK_INTERVAL){
                healthCheckTimer = millis();
                if(WiFi.status() != WL_CONNECTED){
                    LOG_SYS_WARN("Wi-Fi disconnected. Trying to reconnect.");
                    StartConnect(currentSSID.c_str(), currentPassword.c_str());
                }
            }
            break;

        case NetStatus::OFF:
            break;

        // SSID_NOT_FOUND / FAILED / TIMEOUT: 未接続。少し置いてから保存済みのネットワークへ自動で繋ぎ直す
        default:
            if(WifiProfiles::IsEnabled() && WifiProfiles::Count() > 0
                && millis() - retryTimer >= RETRY_INTERVAL){
                AutoConnectNext();
            }
            break;
    };
};

void NetworkFunctions::SetEnabled(bool enabled){
    if(enabled == WifiProfiles::IsEnabled() && (enabled == (currentStatus != NetStatus::OFF))) return;
    WifiProfiles::SetEnabled(enabled);
    retryRank = 0;
    if(!enabled){
        WiFi.disconnect();
        currentSSID.clear();
        currentPassword.clear();
        currentStatus = NetStatus::OFF;
        LOG_SYS_MSG("Network Service: Wi-FiをOFFにしました。");
    }else{
        currentStatus = NetStatus::FAILED;
        retryTimer = millis();
        LOG_SYS_MSG("Network Service: Wi-FiをONにしました。");
        AutoConnectNext();
    }
}

bool NetworkFunctions::RemoveProfile(int index){
    const WifiProfiles::Profile* p = WifiProfiles::At(index);
    if(!p) return false;
    const bool current = (p->ssid == currentSSID.c_str())
        && (currentStatus == NetStatus::SUCCESS || currentStatus == NetStatus::TRYING_CONNECT);
    if(!WifiProfiles::Remove(index)) return false;
    if(current){
        WiFi.disconnect();
        currentSSID.clear();
        currentPassword.clear();
        currentStatus = NetStatus::FAILED;
        retryTimer = millis();
        retryRank = 0;
    }
    return true;
}

bool NetworkFunctions::IsEnabled(){ return WifiProfiles::IsEnabled(); }

bool NetworkFunctions::ConnectProfile(int index){
    const WifiProfiles::Profile* p = WifiProfiles::At(index);
    if(!p) return false;
    ConnectWiFiAsync(p->ssid.c_str(), p->password.c_str());
    return true;
}

void NetworkFunctions::SetLowPower(bool enable){
    low_power = enable;
    if(enable) WiFi.lowPowerMode();
    else       WiFi.defaultLowPowerMode();
}

void NetworkFunctions::ConnectWiFiAsync(const char* ssid, const char* password){
    // 手動で選んだ接続先。OFFならONにし、自動接続の順番も先頭から数え直す
    // (ここで失敗したら、次の自動接続は直近で接続できたネットワークから)
    if(!WifiProfiles::IsEnabled()) WifiProfiles::SetEnabled(true);
    retryRank = 0;
    StartConnect(ssid, password);
};

NetworkScan* NetworkFunctions::ScanAsync(){
    // HttpGet/HttpRequestと同じ「呼び出し側が生ポインタとして持ち、自分のonUpdate()から
    // 毎フレームupdate()を呼び、終わったら自分でdeleteする」流儀にしてある。
    // PICO_Task::Add()には乗せない — 乗せると、status()がPROCESSING以外になった
    // その場でPICO_Task::Update()自身が即座にdeleteしてしまうため、呼び出し側が
    // 次のフレームのonUpdate()で結果を読もうとした時点で既に解放済みになる
    // (呼び出し側のonUpdate()は毎フレームPICO_Task::Update()より先に走るので、
    // 完了を検知できるのは早くても次のフレームであり、その時点では手遅れ)
    return new NetworkScan();
};
