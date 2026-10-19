#include "services/Firmware_Functions.hpp"
#include "hal/Battery_Functions.hpp"
#include "devtools/DevTools_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "hal/Network_Functions.hpp"
#include "system/Notification_Functions.hpp"
#include "services/PowerMenu_Functions.hpp"
#include "services/Power_Functions.hpp"
#include "hal/Sound_Functions.hpp"
#include "firmware/Fw_Store.hpp"
#include "firmware/Fw_Version.hpp"
#include "storage/Flash_Area.hpp"
#include "sound/Music_Service.hpp"
#include "OS_Data.hpp"

#include <Arduino.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(PICOOS_PC) && !defined(__EMSCRIPTEN__)
    #include <errno.h>
    #include <fcntl.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <sys/ioctl.h>
    #include <sys/socket.h>
    #include <unistd.h>
#elif !defined(PICOOS_PC)
    #include <WiFi.h>
#endif

namespace {
    // ---------------- SDの更新ファイル ----------------
    class SdSource : public FwSource {
        public:
            bool open(const char* path){
                file_ = OSData::SD.open(path, O_RDONLY);
                return (bool)file_;
            }
            void close(){ if(file_) file_.close(); }
            int read(uint8_t* p, size_t n) override {
                if(!OSData::SD_usable || !file_) return -1;
                const int r = file_.read(p, n);
                if(r < 0) return -1;
                return r == 0 ? -1 : r; // 0は終わり
            }
        private:
            FsFile file_;
    };

    // ---------------- PCからのTCP接続 ----------------
    // 待ち受けは画面で[受け取る]を押している間だけ。同時に1本
    class NetLink : public FwSource {
        public:
            bool begin(FixedString<PICO_STR_LL>& err);
            void end();
            bool listening() const { return listening_; }
            // 新しい接続があれば受け取る(前の接続は閉じる)
            bool accept();
            void drop();
            int read(uint8_t* p, size_t n) override;
            void reply(const char* line) override;
            bool closed() override;
        private:
            bool listening_ = false;
            uint32_t last_data_ms_ = 0;
#if defined(PICOOS_PC) && !defined(__EMSCRIPTEN__)
            int server_fd_ = -1;
            int client_fd_ = -1;
#elif !defined(PICOOS_PC)
            WiFiServer* server_ = nullptr;
            WiFiClient client_;
#endif
    };

#if defined(PICOOS_PC) && !defined(__EMSCRIPTEN__)
    uint16_t PcPort(){
        const char* env = getenv("PICOOS_FW_PORT");
        const int v = env ? atoi(env) : 0;
        return (v > 0 && v < 65536) ? (uint16_t)v : FirmwareFunctions::kPort;
    }

    bool NetLink::begin(FixedString<PICO_STR_LL>& err){
        if(listening_) return true;
        server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if(server_fd_ < 0){ err.assign("待ち受けを始められません"); return false; }
        const int one = 1;
        ::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(PcPort());
        if(::bind(server_fd_, (sockaddr*)&addr, sizeof(addr)) != 0 || ::listen(server_fd_, 1) != 0){
            ::close(server_fd_);
            server_fd_ = -1;
            err.assign("待ち受けの番号(ポート)が使えません");
            return false;
        }
        ::fcntl(server_fd_, F_SETFL, ::fcntl(server_fd_, F_GETFL, 0) | O_NONBLOCK);
        listening_ = true;
        return true;
    }
    void NetLink::end(){
        drop();
        if(server_fd_ >= 0) ::close(server_fd_);
        server_fd_ = -1;
        listening_ = false;
    }
    bool NetLink::accept(){
        if(server_fd_ < 0) return false;
        const int fd = ::accept(server_fd_, nullptr, nullptr);
        if(fd < 0) return false;
        drop();
        ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
        const int one = 1;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        client_fd_ = fd;
        last_data_ms_ = millis();
        return true;
    }
    void NetLink::drop(){
        if(client_fd_ >= 0) ::close(client_fd_);
        client_fd_ = -1;
    }
    int NetLink::read(uint8_t* p, size_t n){
        if(client_fd_ < 0) return -1;
        const ssize_t r = ::recv(client_fd_, p, n, MSG_DONTWAIT);
        if(r > 0){ last_data_ms_ = millis(); return (int)r; }
        if(r == 0) return -1;
        if(errno != EAGAIN && errno != EWOULDBLOCK) return -1;
        if(millis() - last_data_ms_ > FirmwareFunctions::kNetIdleTimeoutMs) return -1;
        return 0;
    }
    void NetLink::reply(const char* line){
        if(client_fd_ < 0) return;
        ::send(client_fd_, line, strlen(line), MSG_NOSIGNAL);
        ::send(client_fd_, "\n", 1, MSG_NOSIGNAL);
    }
    bool NetLink::closed(){
        if(client_fd_ < 0) return true;
        char probe;
        const ssize_t r = ::recv(client_fd_, &probe, 1, MSG_PEEK | MSG_DONTWAIT);
        if(r == 0) return true;
        return r < 0 && errno != EAGAIN && errno != EWOULDBLOCK;
    }
#elif !defined(PICOOS_PC)
    bool NetLink::begin(FixedString<PICO_STR_LL>& err){
        if(listening_) return true;
        if(!NetworkFunctions::IsConnected()){ err.assign("Wi-Fiに繋がっていません"); return false; }
        server_ = new WiFiServer(FirmwareFunctions::kPort);
        if(!server_){ err.assign("メモリが足りません"); return false; }
        server_->begin();
        listening_ = true;
        return true;
    }
    void NetLink::end(){
        drop();
        if(server_){ server_->stop(); delete server_; server_ = nullptr; }
        listening_ = false;
    }
    bool NetLink::accept(){
        if(!server_) return false;
        WiFiClient c = server_->accept();
        if(!c) return false;
        drop();
        client_ = c;
        client_.setNoDelay(true);
        last_data_ms_ = millis();
        return true;
    }
    void NetLink::drop(){ if(client_) client_.stop(); }
    int NetLink::read(uint8_t* p, size_t n){
        if(!client_) return -1;
        const int avail = client_.available();
        if(avail <= 0){
            if(!client_.connected()) return -1;
            if(millis() - last_data_ms_ > FirmwareFunctions::kNetIdleTimeoutMs) return -1;
            return 0;
        }
        const int r = client_.read(p, (size_t)avail < n ? (size_t)avail : n);
        if(r > 0) last_data_ms_ = millis();
        return r < 0 ? -1 : r;
    }
    void NetLink::reply(const char* line){
        if(!client_) return;
        client_.write((const uint8_t*)line, strlen(line));
        client_.write((const uint8_t*)"\n", 1);
    }
    bool NetLink::closed(){ return !client_ || (!client_.connected() && client_.available() <= 0); }
#else // Web(生のソケットが無い)
    bool NetLink::begin(FixedString<PICO_STR_LL>& err){ err.assign("Web版では受け取れません"); return false; }
    void NetLink::end(){}
    bool NetLink::accept(){ return false; }
    void NetLink::drop(){}
    int NetLink::read(uint8_t*, size_t){ return -1; }
    void NetLink::reply(const char*){}
    bool NetLink::closed(){ return true; }
#endif

    // ---------------- 状態 ----------------
    FwUpdate* job = nullptr;      // 更新の間だけ new(約4.6KB)
    SdSource sd_source;
    NetLink net;
    bool job_is_net = false;
    bool reboot_pending = false;  // 準備ができて再起動を待っている(この後は取り消せない)
    bool reboot_requested = false;
    uint32_t done_ms = 0;
    bool prepared = false;        // 書き込みの前の後片付け(音を止める)を済ませたか

    void EndSource(){
        if(job_is_net) net.drop();
        else sd_source.close();
    }

    void FreeJob(){
        EndSource();
        delete job;
        job = nullptr;
        prepared = false;
    }

    FwUpdate::Env MakeEnv(bool always_confirm){
        FwUpdate::Env env;
        env.current_serial = FwVersion::Serial();
        env.current_git = FwVersion::Git();
        env.allow_unsigned = FirmwareFunctions::AllowUnsigned();
        env.always_confirm = always_confirm;
        return env;
    }

    // 前の(終わった)更新を捨てて新しく作る。TCPの接続は accept() が既に入れ替えているので閉じない
    bool NewJob(){
        sd_source.close();
        delete job;
        job = nullptr;
        prepared = false;
        job = new FwUpdate();
        return job != nullptr;
    }

    // "serial=12 git=abc" を読む
    bool ParseNote(const char* note, uint32_t& serial, char* git, size_t git_cap){
        const char* s = strstr(note, "serial=");
        const char* g = strstr(note, "git=");
        if(!s || !g) return false;
        serial = (uint32_t)strtoul(s + 7, nullptr, 10);
        g += 4;
        size_t n = 0;
        while(g[n] && g[n] != ' ' && g[n] != '\n' && n + 1 < git_cap){ git[n] = g[n]; n++; }
        git[n] = '\0';
        return true;
    }

    void Notify(const char* title, const char* body){
        NotificationFunctions::Content c;
        NotificationFunctions::Sanitize(c.title, title);
        NotificationFunctions::Sanitize(c.body, body);
        c.sound = false;
        NotificationFunctions::Post(c);
    }
}

void FirmwareFunctions::Setup(){
    LOG_SYS_MSG("ファーム: %s %s", FwVersion::Text(), FwVersion::Date());
    LOG_SYS_DEBUG("%s", FwVersion::Marker());

    // 置き場をゲームボーイのROMが間借りしている(storage/Flash_Area)。マウントに失敗すると
    // フォーマットされてROMが消えるので触らない(この場合、前回の更新の控えも残っていない)
    if(const FlashArea* area = DefaultFlashArea()){
        if(area->holdsRawData()){
            LOG_SYS_MSG("ファームの更新の置き場: ゲームボーイのROMを置いているので片付けません");
            return;
        }
    }

    FwStore& store = DefaultFwStore();
    if(!store.mount()){
        LOG_SYS_WARN("ファームの更新の置き場(内蔵フラッシュのLittleFS)がありません");
        return;
    }
    char note[96] = {0};
    const bool has_note = store.readNote(note, sizeof(note));
    store.clear();
    store.unmount();
    if(!has_note) return;

    uint32_t serial = 0;
    char git[24] = {0};
    if(!ParseNote(note, serial, git, sizeof(git))) return;
    char body[96];
    if(serial == FwVersion::Serial() && strcmp(git, FwVersion::Git()) == 0){
        snprintf(body, sizeof(body), "#%lu (%s)", (unsigned long)serial, git);
        LOG_SYS_OK("ファームを %s に更新しました", body);
        Notify("ファームを更新しました", body);
    }else{
#if defined(PICOOS_PC)
        snprintf(body, sizeof(body), "PCビルドは本体を書き換えません(#%lu %s)", (unsigned long)serial, git);
#else
        snprintf(body, sizeof(body), "今は %s のままです(#%lu %s を書けませんでした)", FwVersion::Text(), (unsigned long)serial, git);
#endif
        LOG_SYS_WARN("ファームの更新: %s", body);
        Notify("ファームを更新できませんでした", body);
    }
}

bool FirmwareFunctions::AllowUnsigned(){ return DevToolsFunctions::FwAllowUnsigned(); }

bool FirmwareFunctions::PowerOk(FixedString<PICO_STR_LL>& err){
    if(BatteryFunctions::HasSample() && !BatteryFunctions::IsExternallyPowered() &&
       BatteryFunctions::GetPercent() < kMinBatteryPercent){
        char text[96];
        snprintf(text, sizeof(text), "電池の残りが少ないです(%d%%)。USBに繋ぐか充電してください", BatteryFunctions::GetPercent());
        err.assign(text);
        return false;
    }
    return true;
}

bool FirmwareFunctions::StartFromSd(const char* path, FixedString<PICO_STR_LL>& err){
    if(Busy() || reboot_pending){ err.assign("別の更新を進めています"); return false; }
    if(!OSData::SD_usable){ err.assign("SDカードが使えません"); return false; }
    if(!PowerOk(err)) return false;
    if(!NewJob()){ err.assign("メモリが足りません"); return false; }
    job_is_net = false;
    if(!sd_source.open(path)){
        FreeJob();
        err.assign("更新ファイルを開けません");
        return false;
    }
    job->start(&sd_source, &DefaultFwStore(), MakeEnv(true));
    LOG_SYS_MSG("ファームの更新: %s を読みます", path);
    return true;
}

bool FirmwareFunctions::StartListening(FixedString<PICO_STR_LL>& err){
    if(!net.begin(err)) return false;
    LOG_SYS_MSG("ファームの更新: ポート%uで待ち受けます", (unsigned)kPort);
    return true;
}

void FirmwareFunctions::StopListening(){
    if(job && job_is_net && !job->finished()) job->cancel("待ち受けをやめました");
    if(job && job_is_net && job->state() != FwUpdate::State::Done) FreeJob();
    net.end();
}

bool FirmwareFunctions::Listening(){ return net.listening(); }

void FirmwareFunctions::ListenAddress(char* out, size_t cap){
#if defined(PICOOS_PC)
    snprintf(out, cap, "127.0.0.1:%u", (unsigned)kPort);
#else
    const IPAddress ip = WiFi.localIP();
    snprintf(out, cap, "%u.%u.%u.%u:%u", ip[0], ip[1], ip[2], ip[3], (unsigned)kPort);
#endif
}

FwUpdate* FirmwareFunctions::Job(){ return job; }
void FirmwareFunctions::Confirm(bool yes){ if(job) job->confirm(yes); }
bool FirmwareFunctions::Busy(){ return job && !job->finished(); }
bool FirmwareFunctions::RebootPending(){ return reboot_pending; }

void FirmwareFunctions::Cancel(){
    if(!job || reboot_pending) return;
    job->cancel();
    EndSource();
}

void FirmwareFunctions::Dismiss(){
    if(job && job->finished() && !reboot_pending) FreeJob();
}

void FirmwareFunctions::Update(){
    // 新しい接続(更新の途中・再起動待ちでなければ受け取る)
    if(net.listening() && !Busy() && !reboot_pending && net.accept()){
        if(NewJob()){
            job_is_net = true;
            FixedString<PICO_STR_LL> err;
            job->start(&net, &DefaultFwStore(), MakeEnv(false));
            if(!PowerOk(err)){
                job->cancel(err.c_str()); // "ERR <理由>" を返す
                net.drop();
            }else{
                LOG_SYS_MSG("ファームの更新: PCから受け取ります");
            }
        }else{
            net.drop();
        }
    }

    if(!job) return;
    PowerFunctions::KeepAwake();

    const FwUpdate::State before = job->state();
    if(job->state() == FwUpdate::State::Payload && !prepared){
        // フラッシュへの書き込みの間は2コア目が止まる(LittleFSが idleOtherCore() する)ので、
        // 鳴っている音を止めておく(ぶつぶつ切れるため)
        SoundFunctions::WavStop();
        SoundFunctions::MusicStop();
        SoundFunctions::StopAll();
        MusicService::Stop();
        prepared = true;
    }
    job->step(kStepBytes);

    if(job->state() != before){
        if(job->state() == FwUpdate::State::Failed){
            LOG_SYS_WARN("ファームの更新: %s", job->message());
            EndSource();
        }else if(job->state() == FwUpdate::State::Done){
            LOG_SYS_OK("ファームの更新: %s(%s)", job->message(), job->header().git);
            done_ms = millis();
            reboot_pending = true;
        }
    }

    if(reboot_pending && !reboot_requested && millis() - done_ms >= kRebootDelayMs){
        reboot_requested = true;
        EndSource();
        net.end();
        PowerMenuFunctions::RequestReboot();
    }
}
