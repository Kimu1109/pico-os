// PCビルド用のWi-Fi代替。
//
// 実機はXPT2046と同じく本物のハードウェアを叩くが、PCには「pico-osが繋ぐべきWi-Fi」が
// 存在しない。かわりに次の2つで振る舞いを決める。
//
//   1. 疎通判定 (既定, state=auto)
//      母艦にインターネットへの経路があるかを見て、接続/切断を返す。
//      UDPソケットをconnect()するだけなのでパケットは一切飛ばない
//      (connect(2)は経路表を引くだけで、UDPではハンドシェイクが無い)。
//
//   2. 設定ファイル/環境変数による上書き
//      「切断」「電波が弱い」「SSIDが見つからない」といった状態を狙って再現できる。
//      UIの各状態を確認したいときはこちら。
//
// **母艦のWi-Fi設定は絶対に変更しない。** ConnectWiFiAsync()が来ても、実際にSSIDへ
// 繋ぎに行くことはしない(開発用エミュレータが母艦のネットワークを切り替えるのは事故のもと)。
//
// 設定は pc/sdcard/sys/network.cfg に `pc-` 始まりのキーで書く(実機側のパーサは
// 知らないキーを無視するので、同じファイルを実機と共有しても害はない)。
//
//   pc-wifi-state = auto | connected | disconnected | ssid-not-found | failed
//   pc-wifi-rssi  = -55            … 電波強度(dBm)。アイコンの本数はこれで決まる
//   pc-wifi-ssid  = picoos-pc      … WiFi.SSID()が返す名前
//   pc-wifi-scan  = Home:-42,Cafe:-70   … スキャンで返す一覧("SSID:RSSI"のカンマ区切り)
//
// 環境変数が設定ファイルより優先される(一時的に切り替えたいとき用):
//
//   PICOOS_WIFI_STATE=disconnected ./pc/build/picoos_pc
//
// Webビルドでは環境変数の代わりにURLのクエリで指定する(main_pc.cpp が setenv する):
//
//   index.html?wifi=disconnected&rssi=-85
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(__EMSCRIPTEN__)
    //ブラウザには生ソケットが無いので疎通判定はJS側(navigator.onLine)に任せる
    #include <emscripten.h>
#else
    #include <errno.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif

#include "Arduino.h"  // millis()
#include "SdFat.h"    // PicoOsSdHost::root (SDのルートから network.cfg を読むため)

enum {
    WL_IDLE_STATUS = 0,
    WL_NO_SSID_AVAIL,
    WL_SCAN_COMPLETED,
    WL_CONNECTED,
    WL_CONNECT_FAILED,
    WL_CONNECTION_LOST,
    WL_DISCONNECTED,
};

namespace PicoOsWifiHost {

    enum class Mode {
        Auto,          // 母艦の疎通で決める
        Connected,     // 常に接続
        Disconnected,  // 常に切断
        SsidNotFound,  // WL_NO_SSID_AVAIL を返す
        Failed,        // WL_CONNECT_FAILED を返す
    };

    struct Entry {
        std::string ssid;
        int32_t     rssi;
    };

    inline Mode        mode = Mode::Auto;
    inline int32_t     rssi = -55;
    inline std::string ssid = "picoos-pc";
    inline std::vector<Entry> scan_list;
    inline bool        loaded = false;
    // pc-wifi-ssid が明示されたか。されていなければ、実機と同じく
    // 接続要求されたSSID(cfgの wifi-ssid)をそのまま表示に使う
    inline bool        ssid_explicit = false;

    inline Mode ParseMode(const char* v, Mode fallback){
        if(!v) return fallback;
        if(strcmp(v, "auto") == 0)            return Mode::Auto;
        if(strcmp(v, "connected") == 0)       return Mode::Connected;
        if(strcmp(v, "disconnected") == 0)    return Mode::Disconnected;
        if(strcmp(v, "ssid-not-found") == 0)  return Mode::SsidNotFound;
        if(strcmp(v, "failed") == 0)          return Mode::Failed;
        printf("[PC] pc-wifi-state の値が不明です: %s (autoとして扱います)\n", v);
        return fallback;
    }

    // "Home:-42,Cafe:-70" 形式をほどく。RSSIを省いた "Home,Cafe" も許す
    inline void ParseScanList(const char* v){
        scan_list.clear();
        if(!v) return;
        std::string s(v);
        size_t pos = 0;
        while(pos <= s.size()){
            size_t comma = s.find(',', pos);
            if(comma == std::string::npos) comma = s.size();
            std::string item = s.substr(pos, comma - pos);
            pos = comma + 1;

            //前後の空白を落とす
            size_t b = item.find_first_not_of(" \t");
            size_t e = item.find_last_not_of(" \t");
            if(b == std::string::npos) continue;
            item = item.substr(b, e - b + 1);
            if(item.empty()) continue;

            size_t colon = item.rfind(':');
            if(colon == std::string::npos){
                scan_list.push_back({item, -60});
            }else{
                scan_list.push_back({item.substr(0, colon), (int32_t)atoi(item.c_str() + colon + 1)});
            }
        }
    }

    // network.cfg を素のfopenで読む。
    // PICO_Config::ParseFile は OSData::SD 経由なので、compat層から呼ぶと
    // 依存が循環する。書式(key=value / 行頭#がコメント)は同じなので自前で読む。
    inline void LoadConfigOnce(){
        if(loaded) return;
        loaded = true;

        //スキャン一覧の既定値(設定が無くてもスキャンUIが空にならないように)
        scan_list = { {"picoos-pc", -45}, {"guest-network", -68}, {"far-away-ap", -84} };

        const std::string path = PicoOsSdHost::root + "/sys/network.cfg";
        if(FILE* fp = fopen(path.c_str(), "rb")){
            char line[256];
            while(fgets(line, sizeof(line), fp)){
                char* p = line;
                while(*p == ' ' || *p == '\t') p++;
                if(*p == '#' || *p == '\r' || *p == '\n' || *p == '\0') continue;

                char* eq = strchr(p, '=');
                if(!eq) continue;
                *eq = '\0';
                char* key = p;
                char* val = eq + 1;

                //キーの末尾とvalの前後をトリム
                for(char* t = eq - 1; t >= key && (*t == ' ' || *t == '\t'); t--) *t = '\0';
                while(*val == ' ' || *val == '\t') val++;
                for(size_t n = strlen(val); n > 0; n--){
                    char c = val[n - 1];
                    if(c == '\r' || c == '\n' || c == ' ' || c == '\t') val[n - 1] = '\0';
                    else break;
                }

                if     (strcmp(key, "pc-wifi-state") == 0) mode = ParseMode(val, mode);
                else if(strcmp(key, "pc-wifi-rssi")  == 0) rssi = (int32_t)atoi(val);
                else if(strcmp(key, "pc-wifi-ssid")  == 0){ ssid = val; ssid_explicit = true; }
                else if(strcmp(key, "pc-wifi-scan")  == 0) ParseScanList(val);
            }
            fclose(fp);
        }

        //環境変数が設定ファイルより優先
        if(const char* v = getenv("PICOOS_WIFI_STATE")) mode = ParseMode(v, mode);
        if(const char* v = getenv("PICOOS_WIFI_RSSI"))  rssi = (int32_t)atoi(v);
        if(const char* v = getenv("PICOOS_WIFI_SSID")){ ssid = v; ssid_explicit = true; }
        if(const char* v = getenv("PICOOS_WIFI_SCAN"))  ParseScanList(v);
    }

    // 母艦にインターネットへの経路があるか。
    // UDPソケットをconnect()すると経路表が引かれるだけで、パケットは飛ばない。
    // 経路が無ければ ENETUNREACH / EHOSTUNREACH で即座に失敗する。
    //
    // Webビルドではソケットが使えないので、ブラウザの navigator.onLine を見る。
    // 「LANに繋がっているか」しか分からない大雑把な値だが、UIの接続/切断表示を
    // 確かめるには十分(厳密に試したいなら ?wifi=disconnected で固定する)。
    inline bool HasRouteToInternet(){
#if defined(__EMSCRIPTEN__)
        return EM_ASM_INT({ return (navigator.onLine === false) ? 0 : 1; }) != 0;
#else
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if(fd < 0) return false;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port   = htons(53);
        addr.sin_addr.s_addr = htonl(0x08080808); // 8.8.8.8(宛先として引くだけ)

        const bool ok = (::connect(fd, (sockaddr*)&addr, sizeof(addr)) == 0);
        ::close(fd);
        return ok;
#endif
    }

    // 疎通判定は毎フレームやるほどのものではないので間隔を空ける
    inline bool          cached_route = false;
    inline bool          cached_valid = false;
    inline unsigned long cached_at = 0;
    constexpr unsigned long kRouteCacheMs = 2000;

    inline bool RouteCached(unsigned long now_ms){
        if(!cached_valid || now_ms - cached_at >= kRouteCacheMs){
            cached_route = HasRouteToInternet();
            cached_valid = true;
            cached_at = now_ms;
        }
        return cached_route;
    }
}

class WiFiClassPC {
public:
    // 実機は非同期に接続を開始する。PCでは母艦の設定を触らないので、
    // SSIDを覚えるだけにして状態は status() 側で決める
    void beginNoBlock(const char* ssid, const char*){
        PicoOsWifiHost::LoadConfigOnce();
        connect_requested_ = true;
        //pc-wifi-ssidで明示されていなければ、接続要求されたSSIDを表示に使う
        if(ssid && *ssid && !PicoOsWifiHost::ssid_explicit) PicoOsWifiHost::ssid = ssid;
    }
    void begin(const char* ssid, const char* pass){ beginNoBlock(ssid, pass); }

    void disconnect(){ connect_requested_ = false; }

    int status(){
        PicoOsWifiHost::LoadConfigOnce();
        if(!connect_requested_) return WL_DISCONNECTED;

        using M = PicoOsWifiHost::Mode;
        switch(PicoOsWifiHost::mode){
            case M::Connected:    return WL_CONNECTED;
            case M::Disconnected: return WL_DISCONNECTED;
            case M::SsidNotFound: return WL_NO_SSID_AVAIL;
            case M::Failed:       return WL_CONNECT_FAILED;
            case M::Auto:
            default:
                return PicoOsWifiHost::RouteCached(millis()) ? WL_CONNECTED : WL_DISCONNECTED;
        }
    }

    // ---- スキャン ----
    // 実機は非同期だが、PCでは一覧が即座に揃うので待ち時間を持たせない
    int scanNetworks(bool = false){
        PicoOsWifiHost::LoadConfigOnce();
        scan_done_ = true;
        return (int)PicoOsWifiHost::scan_list.size();
    }
    int scanComplete(){
        if(!scan_done_) return -2; // WIFI_SCAN_FAILED相当(scanNetworks前)
        return (int)PicoOsWifiHost::scan_list.size();
    }
    void scanDelete(){ scan_done_ = false; }

    // ---- 情報 ----
    const char* SSID(){
        PicoOsWifiHost::LoadConfigOnce();
        return PicoOsWifiHost::ssid.c_str();
    }
    const char* SSID(int i){
        PicoOsWifiHost::LoadConfigOnce();
        if(i < 0 || i >= (int)PicoOsWifiHost::scan_list.size()) return "";
        return PicoOsWifiHost::scan_list[i].ssid.c_str();
    }
    int32_t RSSI(){
        PicoOsWifiHost::LoadConfigOnce();
        return PicoOsWifiHost::rssi;
    }
    int32_t RSSI(int i){
        PicoOsWifiHost::LoadConfigOnce();
        if(i < 0 || i >= (int)PicoOsWifiHost::scan_list.size()) return 0;
        return PicoOsWifiHost::scan_list[i].rssi;
    }

private:
    bool connect_requested_ = false;
    bool scan_done_ = false;
};
inline WiFiClassPC WiFi;

// arduino-picoが提供しているNTPオブジェクトの代替。
// PCの時計はOSが合わせているので同期処理は要らない(TimeFunctionsが読む
// time(nullptr)が最初から正しい実時刻を返す)。呼ばれても何もしないが、
// 「同期済み」として振る舞う
class NTPClassPC {
public:
    void begin(const char* = nullptr, const char* = nullptr){}
    void end(){}
    bool waitSet(unsigned long = 0){ return true; }
    bool running(){ return true; }
};
inline NTPClassPC NTP;

// TCPクライアントは別ヘッダへ切り出してある。
// ホストテスト(script/host_test/stubs/WiFi.h)からも同じ実装を使いたいが、
// このファイルはSDのルート(PicoOsSdHost::root)から network.cfg を読むため
// SdFat.h に依存していて、そのまま取り込めないため。
#include "WiFiClient_PC.h"
