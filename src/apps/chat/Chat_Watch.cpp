#include "apps/chat/Chat_Watch.hpp"

#include "system/Background_Functions.hpp"
#include "system/Config_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "hal/Network_Functions.hpp"
#include "system/Notification_Functions.hpp"
#include "system/Time_Functions.hpp"
#include "net/Http_Request.hpp"
#include "storage/SD_Path.hpp"
#include "crypto/Secret_Cipher.hpp"
#include "util/Url.hpp"
#include "OS_Data.hpp"
#include "Arduino.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <new>

bool (*ChatWatch::foreground_query)() = nullptr;

namespace {
    // 失敗したら間を空ける(間隔より短くはしない)
    constexpr uint32_t kRetryMs = 60000;
    // 通信に掛けてよい時間(HttpRequest の10秒の打ち切りより少し長く)
    constexpr uint32_t kGiveUpMs = 20000;

    bool Reached(uint32_t now, uint32_t at){ return (int32_t)(now - at) >= 0; }

    // 部屋ごとに最後に知らせた発言id
    struct Seen { uint32_t room = 0; uint32_t last_id = 0; };
    Seen seen[ChatWatch::kMaxRooms];
    int seen_count = 0;

    // 取りに行く間だけ持つもの
    struct Job : public ChatProto::LineSink {
        HttpRequest req;
        ChatProto::Room rooms[ChatWatch::kMaxRooms];
        int count = 0;
        FixedString<PICO_STR_LL> auth;
        uint32_t started_ms = 0;
        void onLine(char* line, size_t) override {
            if(req.response().statusCode() < 200 || req.response().statusCode() >= 300) return;
            if(count >= ChatWatch::kMaxRooms) return;
            ChatProto::Room r;
            if(ChatProto::ParseRoom(line, r)) rooms[count++] = r;
        }
    };

    Seen* FindSeen(uint32_t room){
        for(int i = 0; i < seen_count; i++) if(seen[i].room == room) return &seen[i];
        return nullptr;
    }

    // chat.cfg を読んで URL とヘッダを作る。使えなければ理由を why へ
    bool LoadConfig(Url& url, FixedString<PICO_STR_LL>& auth, uint32_t& interval_ms, const char*& why){
        interval_ms = ChatWatch::kDefaultIntervalSec * 1000;
        if(!OSData::SD_usable){ why = "SDカードがありません"; return false; }
        if(!OSData::SD.exists(PICO_Path::FILE::CFG::SYS_CHAT_CFG)){ why = "チャットが設定されていません"; return false; }
        char server[PICO_Config::kConfigMaxValueLen] = "";
        char token[PICO_Config::kConfigMaxValueLen] = "";
        int sec = (int)ChatWatch::kDefaultIntervalSec;
        PICO_Config::ParseFile(PICO_Path::FILE::CFG::SYS_CHAT_CFG, [&](const char* key, const char* value){
            if(strcmp(key, "server") == 0){
                strncpy(server, value, sizeof(server) - 1);
            }else if(strcmp(key, "token") == 0){
                char dec[PICO_Config::kConfigMaxValueLen];
                if(PICO_Secret::Decrypt("chat-token", value, dec, sizeof(dec))) strncpy(token, dec, sizeof(token) - 1);
            }else if(strcmp(key, "notify-poll-sec") == 0){
                int v = 0;
                if(PICO_Config::ConfigValue::AsInt(value, v) && v > 0) sec = v;
            }
        });
        if(sec < (int)ChatWatch::kMinIntervalSec) sec = (int)ChatWatch::kMinIntervalSec;
        interval_ms = (uint32_t)sec * 1000;
        if(!server[0] || !token[0]){ why = "chat.cfg に server と token がありません"; return false; }
        if(!UrlTools::Parse(url, server)){ why = "chat.cfg の server が URL ではありません"; return false; }
        for(const char* p = token; *p; p++){
            const char c = *p;
            const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
            if(!ok){ why = "chat.cfg の token に使えない文字があります"; return false; }
        }
        //末尾の / を落としてから "/api/v1/rooms" を足す(ChatClient と同じ)
        while(url.path.length() > 0 && url.path.c_str()[url.path.length() - 1] == '/'){
            FixedString<PICO_STR_LL> trimmed;
            trimmed.assign(url.path.c_str(), url.path.length() - 1);
            url.path = trimmed;
        }
        url.query.clear();
        if(!url.path.append("/api/v1/rooms")){ why = "URLが長すぎます"; return false; }
        if(!auth.assign("Authorization: Bearer ") || !auth.append(token)){ why = "token が長すぎます"; return false; }
        return true;
    }

    class Service : public BackgroundService {
        public:
            const char* key() const override { return "chat"; }
            const char* name() const override { return "チャットの未読"; }
            IconID icon() const override { return IconID::Messages; }
            bool toggleable() const override { return true; }
            bool enabled() const override { return enabled_; }
            void setEnabled(bool on) override {
                enabled_ = on;
                if(!on) dropJob();
                next_ms_ = millis();
            }
            const char* appName() const override { return "チャット"; }
            // 入っていて、設定がある間は動いている扱い
            bool active() const override { return enabled_ && configured_; }
            bool runNow() override {
                if(!enabled_ || job_) return false;
                next_ms_ = millis();
                return true;
            }

            void status(FixedString<PICO_STR_LL>& out) const override {
                out.clear();
                if(!enabled_){ out.assign("切っています"); return; }
                if(!configured_ && why_[0]){ out.assign(why_); return; }
                if(job_){ out.assign("確認しています…"); return; }
                if(last_error_[0]){ out.appendFormat("失敗: %s", last_error_); return; }
                if(last_ok_ms_ != 0){
                    const uint32_t ago = (millis() - last_ok_ms_) / 1000;
                    if(ago < 60) out.appendFormat("%lu秒前に確認(%d部屋)", (unsigned long)ago, rooms_);
                    else out.appendFormat("%lu分前に確認(%d部屋)", (unsigned long)(ago / 60), rooms_);
                    return;
                }
                out.assign("まだ確認していません");
            }

            void update(uint32_t now) override {
                if(!enabled_) return;
                if(job_){ step(now); return; }
                if(!Reached(now, next_ms_)) return;
                //チャットアプリが開いていれば、画面の ChatClient に任せる
                if(ChatWatch::foreground_query && ChatWatch::foreground_query()){
                    next_ms_ = now + interval_ms_;
                    return;
                }
                start(now);
            }

        private:
            void start(uint32_t now){
                Url url;
                FixedString<PICO_STR_LL> auth;
                const char* why = "";
                configured_ = LoadConfig(url, auth, interval_ms_, why);
                strncpy(why_, why, sizeof(why_) - 1);
                why_[sizeof(why_) - 1] = '\0';
                if(!configured_){
                    //設定が置かれるかもしれないので、間隔ごとに見直す
                    next_ms_ = now + interval_ms_;
                    return;
                }
                if(!NetworkFunctions::IsConnected() || !TimeFunctions::IsClockSet()){
                    next_ms_ = now + 10000;
                    return;
                }
                job_ = new (std::nothrow) Job();
                if(!job_){
                    setError("メモリが足りません");
                    next_ms_ = now + kRetryMs;
                    return;
                }
                job_->started_ms = now;
                job_->auth = auth;
                job_->req.setKeepAlive(false);
                if(!job_->req.setExtraHeader(job_->auth.c_str())
                   || !job_->req.begin(url, HttpRequest::Method::GET, job_)){
                    setError("要求を始められません");
                    dropJob();
                    next_ms_ = now + kRetryMs;
                }
            }

            void step(uint32_t now){
                job_->req.update();
                if(job_->req.getStatus() == TaskTools::PROCESSING){
                    if(Reached(now, job_->started_ms + kGiveUpMs)){
                        setError("時間切れ");
                        dropJob();
                        next_ms_ = now + kRetryMs;
                    }
                    return;
                }
                if(job_->req.getStatus() != TaskTools::SUCCESS){
                    setError(job_->req.failureToStr());
                    dropJob();
                    next_ms_ = now + (interval_ms_ > kRetryMs ? interval_ms_ : kRetryMs);
                    return;
                }
                const int code = job_->req.response().statusCode();
                if(code < 200 || code >= 300){
                    char buf[48];
                    snprintf(buf, sizeof(buf), code == 401 ? "トークンが違います(HTTP %d)" : "HTTP %d", code);
                    setError(buf);
                    dropJob();
                    next_ms_ = now + interval_ms_;
                    return;
                }
                job_->flush();
                ChatWatch::OnRooms(job_->rooms, job_->count);
                rooms_ = job_->count;
                last_error_[0] = '\0';
                last_ok_ms_ = now ? now : 1;
                dropJob();
                next_ms_ = now + interval_ms_;
            }

            void setError(const char* why){
                strncpy(last_error_, why ? why : "", sizeof(last_error_) - 1);
                last_error_[sizeof(last_error_) - 1] = '\0';
                LOG_APP_WARN("チャットの未読: %s", last_error_);
            }

            void dropJob(){
                if(!job_) return;
                job_->req.cancel();
                delete job_;
                job_ = nullptr;
            }

            bool enabled_ = true;
            bool configured_ = false;
            Job* job_ = nullptr;
            uint32_t next_ms_ = 0;
            uint32_t interval_ms_ = ChatWatch::kDefaultIntervalSec * 1000;
            uint32_t last_ok_ms_ = 0;
            int rooms_ = 0;
            char why_[64] = "";
            char last_error_[96] = "";

            friend void ChatWatch::Test::Reset();
    };
    Service service;
}

BackgroundService* ChatWatch::Service(){ return &service; }

int ChatWatch::OnRooms(const ChatProto::Room* rooms, int count){
    int posted = 0;
    for(int i = 0; i < count; i++){
        const ChatProto::Room& r = rooms[i];
        Seen* s = FindSeen(r.id);
        if(!s){
            if(seen_count >= kMaxRooms) continue;
            s = &seen[seen_count++];
            s->room = r.id;
            s->last_id = 0;
        }
        const bool fresh = r.unread > 0 && r.last_id > s->last_id;
        s->last_id = r.last_id;
        if(!fresh) continue;
        NotificationFunctions::Content c;
        NotificationFunctions::Sanitize(c.title, r.name.c_str());
        char body[64];
        snprintf(body, sizeof(body), "チャット: 未読 %lu件", (unsigned long)r.unread);
        NotificationFunctions::Sanitize(c.body, body);
        char tag[24];
        snprintf(tag, sizeof(tag), "chat:%lu", (unsigned long)r.id);
        NotificationFunctions::Sanitize(c.tag, tag);
        NotificationFunctions::Sanitize(c.app, "チャット");
        if(NotificationFunctions::Post(c) != 0) posted++;
    }
    return posted;
}

void ChatWatch::Test::Reset(){
    seen_count = 0;
    service.dropJob();
    service.enabled_ = true;
    service.configured_ = false;
    service.next_ms_ = 0;
    service.last_ok_ms_ = 0;
    service.last_error_[0] = '\0';
    service.why_[0] = '\0';
}
