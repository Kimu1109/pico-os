#include "services/WifiSetup_Functions.hpp"
#include "hal/Network_Functions.hpp"
#include "net/Setup_Portal.hpp"
#include "net/Wifi_Profiles.hpp"
#include "crypto/Secret_Aead.hpp"
#include "system/Log_Functions.hpp"
#include "util/Net_Addr.hpp"
#include "system/I18n_Functions.hpp"

#include <Arduino.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(PICOOS_PC) && !defined(__EMSCRIPTEN__)
    #include <errno.h>
    #include <fcntl.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
#elif !defined(PICOOS_PC)
    #include <WiFi.h>
    #include <WiFiUdp.h>
#endif

namespace {
    using WifiSetupFunctions::State;

    State state = State::Idle;
    FixedString<PICO_STR_M> ap_ssid;
    char ap_password[12] = "";
    FixedString<PICO_STR_M> page_url;
    FixedString<PICO_STR_M> saved_ssid;

    SetupPortal::SaveResult SaveNetwork(const char* ssid, const char* password){
        switch(WifiProfiles::Put(ssid, password)){
            case WifiProfiles::PutResult::Added:
            case WifiProfiles::PutResult::Updated: return SetupPortal::SaveResult::Saved;
            case WifiProfiles::PutResult::Full:    return SetupPortal::SaveResult::Full;
            default:                               return SetupPortal::SaveResult::Invalid;
        }
    }

    // ---- 待ち受け(実機は WiFiServer / WiFiUDP、PCは生のソケット) ----
    // 持ち物はまとめて1つにして Start() で new する(常に持つと約4.5KBになるため)
    struct SetupServer {
        SetupPortal::Request request;
        SetupPortal::Response body;
        uint32_t client_since = 0;
        bool has_client = false;
#if defined(PICOOS_PC) && !defined(__EMSCRIPTEN__)
        int listen_fd = -1;
        int client_fd = -1;
#elif !defined(PICOOS_PC)
        WiFiServer http{WifiSetupFunctions::kHttpPort};
        WiFiClient client;
        WiFiUDP dns;
        uint8_t dns_buf[SetupPortal::kMaxDnsPacket];
        uint8_t dns_out[SetupPortal::kMaxDnsPacket + 16];
        uint32_t ap_ip = 0;
#endif
        bool begin(FixedString<PICO_STR_LL>& err);
        void end();
        void poll();
        // 接続から読む(読めたバイト数、0=まだ、-1=閉じた)/全部書く
        int  read(uint8_t* p, size_t n);
        void write(const void* p, size_t n);
        bool accept();
        void drop();
    };
    SetupServer* server = nullptr;

#if defined(PICOOS_PC) && !defined(__EMSCRIPTEN__)
    uint16_t PcPort(){
        const char* env = getenv("PICOOS_SETUP_PORT");
        const int v = env ? atoi(env) : 0;
        return (v > 0 && v < 65536) ? (uint16_t)v : 8081;
    }
    bool SetupServer::begin(FixedString<PICO_STR_LL>& err){
        listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if(listen_fd < 0){ err.assign(TR("待ち受けを始められません")); return false; }
        const int one = 1;
        ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 母艦の外からは開けないようにする
        addr.sin_port = htons(PcPort());
        if(::bind(listen_fd, (sockaddr*)&addr, sizeof(addr)) != 0 || ::listen(listen_fd, 2) != 0){
            ::close(listen_fd);
            listen_fd = -1;
            err.assign(TR("待ち受けの番号(ポート)が使えません"));
            return false;
        }
        ::fcntl(listen_fd, F_SETFL, ::fcntl(listen_fd, F_GETFL, 0) | O_NONBLOCK);
        char url[40];
        snprintf(url, sizeof(url), "http://127.0.0.1:%u/", (unsigned)PcPort());
        page_url.assign(url);
        return true;
    }
    void SetupServer::end(){
        drop();
        if(listen_fd >= 0) ::close(listen_fd);
        listen_fd = -1;
    }
    bool SetupServer::accept(){
        const int fd = ::accept(listen_fd, nullptr, nullptr);
        if(fd < 0) return false;
        ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
        client_fd = fd;
        return true;
    }
    void SetupServer::drop(){
        if(client_fd >= 0) ::close(client_fd);
        client_fd = -1;
    }
    int SetupServer::read(uint8_t* p, size_t n){
        const ssize_t r = ::recv(client_fd, p, n, MSG_DONTWAIT);
        if(r > 0) return (int)r;
        if(r == 0) return -1;
        return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
    }
    void SetupServer::write(const void* p, size_t n){
        // 応答は数KBなので、ブロックして送り切る(相手はLANの中)
        const int flags = ::fcntl(client_fd, F_GETFL, 0);
        ::fcntl(client_fd, F_SETFL, flags & ~O_NONBLOCK);
        ::send(client_fd, p, n, MSG_NOSIGNAL);
        ::fcntl(client_fd, F_SETFL, flags);
    }
    void SetupServer::poll(){}
#elif !defined(PICOOS_PC)
    bool SetupServer::begin(FixedString<PICO_STR_LL>& err){
        char ip[16];
        NetworkFunctions::SetupApAddress(ip, sizeof(ip));
        if(!NetAddr::Parse(ip, ap_ip)){ err.assign(TR("アクセスポイントのアドレスが分かりません")); return false; }
        http.begin();
        if(!dns.begin(WifiSetupFunctions::kDnsPort)){
            // DNSが無くても、URLを打てばページは開ける
            LOG_SYS_WARN("Wi-Fi設定: DNSの待ち受けを始められませんでした");
        }
        char url[40];
        snprintf(url, sizeof(url), "http://%s/", ip);
        page_url.assign(url);
        return true;
    }
    void SetupServer::end(){
        drop();
        http.stop();
        dns.stop();
    }
    bool SetupServer::accept(){
        WiFiClient c = http.accept();
        if(!c) return false;
        client = c;
        return true;
    }
    void SetupServer::drop(){ if(client) client.stop(); }
    int SetupServer::read(uint8_t* p, size_t n){
        const int avail = client.available();
        if(avail <= 0) return client.connected() ? 0 : -1;
        const int r = client.read(p, (size_t)avail < n ? (size_t)avail : n);
        return r < 0 ? -1 : r;
    }
    void SetupServer::write(const void* p, size_t n){
        const uint8_t* b = (const uint8_t*)p;
        while(n > 0){
            const size_t w = client.write(b, n);
            if(w == 0) break;
            b += w;
            n -= w;
        }
        client.flush();
    }
    void SetupServer::poll(){
        // DNS: 1フレームに1つ答える
        const int n = dns.parsePacket();
        if(n <= 0) return;
        const int r = dns.read(dns_buf, sizeof(dns_buf));
        if(r <= 0) return;
        const size_t len = SetupPortal::BuildDnsReply(dns_buf, (size_t)r, ap_ip, dns_out, sizeof(dns_out));
        if(len == 0) return;
        dns.beginPacket(dns.remoteIP(), dns.remotePort());
        dns.write(dns_out, len);
        dns.endPacket();
    }
#else // Web
    bool SetupServer::begin(FixedString<PICO_STR_LL>& err){ err.assign(TR("Web版では使えません")); return false; }
    void SetupServer::end(){}
    bool SetupServer::accept(){ return false; }
    void SetupServer::drop(){}
    int SetupServer::read(uint8_t*, size_t){ return -1; }
    void SetupServer::write(const void*, size_t){}
    void SetupServer::poll(){}
#endif

    void Serve(){
        if(!server->has_client){
            if(!server->accept()) return;
            server->has_client = true;
            server->client_since = millis();
            server->request.reset();
        }
        uint8_t buf[256];
        while(true){
            const int r = server->read(buf, sizeof(buf));
            if(r < 0){ // 何も送らずに閉じた
                server->drop();
                server->has_client = false;
                return;
            }
            if(r == 0) break;
            if(server->request.feed(buf, (size_t)r) != SetupPortal::Request::State::Reading) break;
        }
        const auto st = server->request.state();
        if(st == SetupPortal::Request::State::Reading){
            if(millis() - server->client_since < WifiSetupFunctions::kRequestTimeoutMs) return;
            server->drop();
            server->has_client = false;
            return;
        }
        FixedString<PICO_STR_M> saved;
        const char* name = NetworkFunctions::hostname.c_str();
        const int status = SetupPortal::Respond(server->request, &SaveNetwork, name, server->body, saved);
        char head[200];
        SetupPortal::FormatHeader(status, server->body.length(), head, sizeof(head));
        server->write(head, strlen(head));
        server->write(server->body.c_str(), server->body.length());
        server->drop();
        server->has_client = false;
        if(!saved.empty()){
            saved_ssid.assign(saved.c_str());
            state = State::Saved;
            LOG_SYS_OK("Wi-Fi設定: スマホから「%s」を受け取りました", saved_ssid.c_str());
        }
    }
}

bool WifiSetupFunctions::Start(FixedString<PICO_STR_LL>& err){
    if(state != State::Idle) return true;
    saved_ssid.clear();

    // APの名前と、毎回ちがう8桁の数字のパスワード(WPA2は8文字以上)
    char name[PICO_STR_M];
    snprintf(name, sizeof(name), "%s-setup", NetworkFunctions::hostname.c_str());
    ap_ssid.assign(name);
    uint8_t rnd[8];
    SecretAead::Random(rnd, sizeof(rnd));
    for(int i = 0; i < 8; i++) ap_password[i] = (char)('0' + rnd[i] % 10);
    ap_password[8] = '\0';

    if(!NetworkFunctions::StartSetupAp(ap_ssid.c_str(), ap_password)){
        err.assign(TR("アクセスポイントを始められませんでした"));
        return false;
    }
    server = new SetupServer();
    if(!server->begin(err)){
        delete server;
        server = nullptr;
        NetworkFunctions::StopSetupAp();
        return false;
    }
    state = State::Running;
    LOG_SYS_MSG("Wi-Fi設定: %s で待っています", page_url.c_str());
    return true;
}

void WifiSetupFunctions::Update(){
    if(state != State::Running || !server) return;
    server->poll();
    Serve();
}

void WifiSetupFunctions::Stop(){
    if(state == State::Idle) return;
    if(server){
        server->end();
        delete server;
        server = nullptr;
    }
    const bool saved = (state == State::Saved);
    state = State::Idle;
    const int idx = saved ? WifiProfiles::Find(saved_ssid.c_str()) : -1;
    if(idx >= 0){
        NetworkFunctions::ConnectProfile(idx); // APを止めてから繋ぐ
    }else{
        NetworkFunctions::StopSetupAp();
    }
}

WifiSetupFunctions::State WifiSetupFunctions::GetState(){ return state; }
const char* WifiSetupFunctions::ApSsid(){ return ap_ssid.c_str(); }
const char* WifiSetupFunctions::ApPassword(){ return ap_password; }
const char* WifiSetupFunctions::PageUrl(){ return page_url.c_str(); }
const char* WifiSetupFunctions::SavedSsid(){ return saved_ssid.c_str(); }
int WifiSetupFunctions::Clients(){ return NetworkFunctions::SetupApClients(); }
