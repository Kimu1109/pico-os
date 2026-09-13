#pragma once
#include <WiFi.h>
#include "task/Task.hpp"
#include "gui/icons/icons_data.h"
#include "util/FixedString.hpp"
#include "consts.hpp"

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
    inline FixedString<PICO_STR_M> currentSSID;
    inline FixedString<PICO_STR_L> currentPassword; // 再接続用に保持(SetupやConnectWiFiAsync経由で設定される)
    inline FixedString<PICO_STR_M> ntpServer1{"ntp.nict.jp"};
    inline FixedString<PICO_STR_M> ntpServer2{"time.google.com"};

    inline unsigned long timer = 0;

    // --- 定期的な再接続交渉まわり ---
    // SUCCESS状態中、一定間隔でWiFi.status()を確認し、
    // 切断を検知したらConnectWiFiAsync()を呼び直してTRYING_CONNECTへ戻す。
    inline unsigned long healthCheckTimer = 0;
    constexpr unsigned long HEALTH_CHECK_INTERVAL = 5000; // ms、生存確認の間隔

    void Setup();
    void Update();

    //接続中か。アイコンの選択と、ステータスバーのバツ印の出し分けに使う
    inline bool IsConnected(){ return currentStatus == NetStatus::SUCCESS; }

    // 電波強度アイコン。圏外でも「最弱の棒」を返す点に注意。
    // 圏外専用のアイコンは持たず、SDカードと同じく呼び出し側がバツ印を重ねる
    // (Statusbar::render を参照)。
    IconID GetWifiStateIconID();
    
    void ConnectWiFiAsync(const char* ssid, const char* password);
    Task* ScanAsync();
    
    inline void ScanResultClear(){
        WiFi.scanDelete();
    }
};
