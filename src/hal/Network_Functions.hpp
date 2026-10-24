#pragma once
#include <WiFi.h>
#include "task/Task.hpp"
#include "gui/icons/icons_data.h"
#include "util/FixedString.hpp"
#include "consts.hpp"

class NetworkScan; // net/NetworkScan.hpp。ここではポインタ型でしか使わないので前方宣言で足りる

namespace NetworkFunctions {

    enum class NetStatus
    {
        SUCCESS,
        TIMEOUT,
        SSID_NOT_FOUND,
        FAILED,
        TRYING_CONNECT,
        OFF,            // Wi-FiをOFFにしている(WifiProfiles::IsEnabled()が偽)
        AP_SETUP,       // 本体がアクセスポイントになっている(AP設定モード。StartSetupAp())
    };

    //NOT TO WRITE! READONLY!
    inline NetStatus currentStatus = NetStatus::FAILED;
    inline FixedString<PICO_STR_M> currentSSID;
    inline FixedString<PICO_STR_L> currentPassword; // 再接続用に保持(SetupやConnectWiFiAsync経由で設定される)
    inline FixedString<PICO_STR_M> ntpServer1{"ntp.nict.jp"};
    inline FixedString<PICO_STR_M> ntpServer2{"time.google.com"};

    // 本体の名前(network.cfgの hostname)。DHCPで名乗り、mDNSで「<名前>.local」として応答する
    // (同じLANのPC・スマホから picoos.local でファームの送信等ができる)。英数字とハイフンの1〜63文字
    constexpr const char* kDefaultHostname = "picoos";
    inline FixedString<PICO_STR_L> hostname{kDefaultHostname}; // 63文字まで入る大きさ
    // 名前を変えて保存する(mDNSは名乗り直す。DHCPの名乗りは次の接続から)。正しくない名前ならfalse
    bool SetHostname(const char* name);

    inline unsigned long timer = 0;

    // --- 定期的な再接続交渉まわり ---
    // SUCCESS状態中、一定間隔でWiFi.status()を確認し、
    // 切断を検知したらConnectWiFiAsync()を呼び直してTRYING_CONNECTへ戻す。
    inline unsigned long healthCheckTimer = 0;
    constexpr unsigned long HEALTH_CHECK_INTERVAL = 5000; // ms、生存確認の間隔

    // --- 未接続のときの自動接続 ---
    // 接続に失敗した(TIMEOUT/SSID_NOT_FOUND/FAILED)まま RETRY_INTERVAL が過ぎたら、
    // 保存済みのネットワーク(net/Wifi_Profiles)へ「直近で接続できた順」に1つずつ試す。
    // 先頭(直近で接続したもの)から始め、失敗するたびに次へ進み、一巡したら先頭へ戻る。
    // 接続できたら(または手動で接続先を選んだら)また先頭から数え直す
    inline unsigned long retryTimer = 0;
    inline int retryRank = 0;  // 次に試す保存済みネットワークの番号(0 = 直近で接続したもの)
    constexpr unsigned long RETRY_INTERVAL = 30000; // ms

    void Setup();
    void Update();

    //接続中か。アイコンの選択と、ステータスバーのバツ印の出し分けに使う
    inline bool IsConnected(){ return currentStatus == NetStatus::SUCCESS; }

    // 電波強度アイコン。圏外でも「最弱の棒」を返す点に注意。
    // 圏外専用のアイコンは持たず、SDカードと同じく呼び出し側がバツ印を重ねる
    // (Statusbar::render を参照)。
    IconID GetWifiStateIconID();
    
    // Wi-Fiチップ(CYW43)の省電力モード。trueで積極的な省電力(WiFi.aggressiveLowPowerMode())、
    // falseで既定の省電力(WiFi.defaultLowPowerMode())へ戻す。接続は保たれるので
    // 生存確認や再接続はそのまま動くが、応答は数十〜数百ms遅れうる。
    // 再接続したあとも効いているよう、接続が確立するたびに今の設定を掛け直す。
    // 呼び出しはPowerFunctionsだけ(スリープ中だけtrue)
    void SetLowPower(bool enable);
    inline bool low_power = false;

    // 指定したネットワークへ繋ぐ(手動の接続。Wi-FiがOFFならONにする)。
    // 保存済みかどうかは問わない(保存は呼び出し側がWifiProfiles::Put()で行う)
    void ConnectWiFiAsync(const char* ssid, const char* password);
    // 保存済みの index 番目(0 = 直近で接続したもの)へ繋ぐ。範囲外ならfalse
    bool ConnectProfile(int index);
    // 保存済みの index 番目を削除する。今それに繋いでいる(繋ごうとしている)なら切断し、
    // 少し置いてから残りの保存済みネットワークへ自動接続する
    bool RemoveProfile(int index);

    // Wi-FiのON/OFF。OFFにすると切断して自動接続も止める(wifi.cfgの enabled に保存)。
    // ONにすると直近で接続したネットワークから自動接続を始める。
    // 無線チップ自体は止めない(電池の残量表示がVBUSの検出に無線チップのGPIOを使うため。Battery_Functions参照)
    void SetEnabled(bool enabled);
    bool IsEnabled();

    // ---- インターネットへ出られるか(キャプティブポータルの判定。net/Captive_Portal) ----
    // 繋がるたびに少し置いてから判定し、ポータル/出られないときは1分ごとに判定し直す
    // (外で同意を済ませた・回線が戻った、を拾う)。ポータルなら1回だけ通知を出す(タップで設定アプリ)
    enum class Internet : uint8_t {
        Unknown,     // 未接続・まだ判定していない・判定を止めている(captive-probe-url = off)
        Checking,
        Online,
        Portal,      // ログイン/同意のページが要る(portalUrl)
        NoInternet,  // Wi-Fiには繋がったが外へ出られない
    };
    inline Internet internet = Internet::Unknown;
    inline FixedString<PICO_STR_LL> portalUrl;
    // 判定に使うURL(network.cfgの captive-probe-url。204を返すもの。"off"で判定しない)
    inline FixedString<PICO_STR_LL> probeUrl;
    // 今すぐ判定し直す(設定アプリのログインの後など)。未接続なら何もしない
    void RecheckInternet();

    // ---- AP設定モード(services/WifiSetup_Functions が使う) ----
    // 本体をアクセスポイントにする(WPA2。パスワードは8〜63文字)。今の接続は切り、自動接続も止める
    // (AP_SETUP の間)。arduino-picoはAPのとき 192.168.42.1 を名乗り、繋いだ端末へDHCPで配る
    bool StartSetupAp(const char* ssid, const char* password);
    // APを止め、保存済みのネットワークへの自動接続へ戻す(Wi-FiがOFFだったならOFFへ)
    void StopSetupAp();
    // APのアドレス("192.168.42.1")と、今繋いでいる端末の数
    void SetupApAddress(char* out, size_t cap);
    int  SetupApClients();

    // 今のIPアドレス・ゲートウェイ・DNSを "a.b.c.d" で書く(未接続なら空)。設定アプリの表示用
    void CurrentAddress(char* ip, size_t ip_cap);
    void CurrentGateway(char* gw, size_t gw_cap);
    void CurrentDns(char* dns, size_t dns_cap);
    // 保存済みのIPの設定を変えたとき、今そのネットワークに繋いでいるなら繋ぎ直して反映する
    void ReapplyIpConfig(const char* ssid);
    // 戻り値の所有権は呼び出し側に移る(HttpGet/HttpRequestと同じ流儀。
    // PICO_Task::Add()には乗らない)。呼び出し側は毎フレーム update() を呼び、
    // getStatus()がPROCESSING以外になったら結果を読み、自分でdeleteすること
    NetworkScan* ScanAsync();
    
    inline void ScanResultClear(){
        WiFi.scanDelete();
    }
};
