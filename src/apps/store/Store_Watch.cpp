#include "apps/store/Store_Watch.hpp"

#include "apps/store/App_Store.hpp"
#include "system/Background_Functions.hpp"
#include "system/Config_Functions.hpp"
#include "system/Notification_Functions.hpp"
#include "system/Time_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "system/I18n_Functions.hpp"
#include "hal/Network_Functions.hpp"
#include "storage/SD_Path.hpp"
#include "OS_Data.hpp"
#include "Arduino.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <new>

bool (*StoreWatch::foreground_query)() = nullptr;

uint32_t StoreWatch::Fingerprint(const char* const* ids, const char* const* versions, int n){
    //FNV-1a("<id>@<版>;" を並べたもの)。0は「何も無い」に取っておく
    uint32_t h = 2166136261u;
    auto mix = [&](const char* s){
        for(; *s; s++){ h ^= (uint8_t)*s; h *= 16777619u; }
    };
    for(int i = 0; i < n; i++){
        mix(ids[i]);
        mix("@");
        mix(versions[i]);
        mix(";");
    }
    if(n == 0) return 0;
    return h != 0 ? h : 1;
}

namespace {
    using PICO_Path::FILE::STORE_STATE;

    constexpr uint32_t kFailRetryMs = 60UL * 60UL * 1000UL;

    bool Reached(uint32_t now, uint32_t at){ return (int32_t)(now - at) >= 0; }

    // /sys/store/state.cfg の checked / notified
    void ReadWatchState(int64_t& checked, uint32_t& notified){
        checked = 0;
        notified = 0;
        if(!OSData::SD_usable || !OSData::SD.exists(STORE_STATE)) return;
        PICO_Config::ParseFile(STORE_STATE, [&](const char* key, const char* value){
            if(strcmp(key, "checked") == 0) checked = strtoll(value, nullptr, 10);
            else if(strcmp(key, "notified") == 0) notified = (uint32_t)strtoul(value, nullptr, 10);
        });
    }

    class Service : public BackgroundService {
        public:
            const char* key() const override { return "store-update"; }
            const char* name() const override { return TR("アプリの更新の確認"); }
            IconID icon() const override { return IconID::AppStore; }
            bool toggleable() const override { return true; }
            bool enabled() const override { return enabled_; }
            void setEnabled(bool on) override {
                enabled_ = on;
                if(!on) drop();
                next_ms_ = millis();
            }
            const char* appName() const override { return AppStore::kAppName; }
            bool active() const override { return enabled_; }
            bool runNow() override {
                if(!enabled_ || store_) return false;
                force_ = true;
                next_ms_ = millis();
                return true;
            }

            void status(FixedString<PICO_STR_LL>& out) const override {
                out.clear();
                if(!enabled_){ out.assign(TR("切っています")); return; }
                if(store_){ out.assign(TR("一覧を取っています…")); return; }
                if(last_error_[0]){ out.appendFormat(TR("失敗: %s"), last_error_); return; }
                if(last_ok_ms_ != 0){
                    const uint32_t ago = (millis() - last_ok_ms_) / 60000;
                    if(last_updates_ > 0) out.appendFormat(TR("%lu分前に確認(更新%d件)"), (unsigned long)ago, last_updates_);
                    else out.appendFormat(TR("%lu分前に確認(更新なし)"), (unsigned long)ago);
                    return;
                }
                out.assign(TR("1日1回、入っているアプリの更新を確かめます"));
            }

            void update(uint32_t now) override {
                if(!enabled_) return;
                if(store_){ step(now); return; }
                if(!started_){
                    started_ = true;
                    next_ms_ = now + StoreWatch::kStartDelayMs;
                    return;
                }
                if(!Reached(now, next_ms_)) return;
                if(StoreWatch::foreground_query && StoreWatch::foreground_query()){
                    next_ms_ = now + StoreWatch::kRetryMs;
                    return;
                }
                if(!force_){
                    const uint32_t wait_ms = untilDue(now);
                    if(wait_ms > 0){
                        //時計が合わせ直されることもあるので、長くても1時間ごとに見直す
                        next_ms_ = now + (wait_ms < kFailRetryMs ? wait_ms : kFailRetryMs);
                        return;
                    }
                }
                start(now);
            }

        private:
            // 次に確かめるまでのミリ秒(0なら今)。最後に確かめた時刻は SD に覚えてあるので、電源を切っても1日1回
            uint32_t untilDue(uint32_t now){
                int64_t checked = 0;
                uint32_t notified = 0;
                ReadWatchState(checked, notified);
                if(TimeFunctions::IsClockSet() && checked > 0){
                    const int64_t since = (int64_t)time(nullptr) - checked;
                    if(since >= 0 && since < (int64_t)StoreWatch::kIntervalSec){
                        return (uint32_t)((int64_t)StoreWatch::kIntervalSec - since) * 1000UL;
                    }
                    return 0;
                }
                //時計が合っていない: この起動の中で確かめてから1日経つまで待つ
                if(last_ok_ms_ != 0){
                    const uint32_t since = now - last_ok_ms_;
                    const uint32_t interval = StoreWatch::kIntervalSec * 1000UL;
                    return since < interval ? interval - since : 0;
                }
                return 0;
            }

            void start(uint32_t now){
                if(!OSData::SD_usable){
                    next_ms_ = now + kFailRetryMs;
                    return;
                }
                if(!NetworkFunctions::IsConnected()){
                    next_ms_ = now + StoreWatch::kRetryMs;
                    return;
                }
                force_ = false;
                store_ = new (std::nothrow) AppStore();
                if(!store_){
                    setError(TR("メモリが足りません"));
                    next_ms_ = now + kFailRetryMs;
                    return;
                }
                store_->loadConfig();
                if(!store_->beginRefresh()){
                    setError(store_->message());
                    drop();
                    next_ms_ = now + kFailRetryMs;
                    return;
                }
                started_ms_ = now;
            }

            void step(uint32_t now){
                store_->update();
                if(store_->busy() && !Reached(now, started_ms_ + StoreWatch::kGiveUpMs)) return;
                if(store_->state() == AppStore::State::Done){
                    finish(now);
                }else{
                    setError(store_->busy() ? TR("時間切れ") : store_->message());
                    next_ms_ = now + kFailRetryMs;
                }
                drop();
            }

            // 一覧を取れた: 更新を数えて、顔ぶれが前と違えば知らせる
            void finish(uint32_t now){
                const char* ids[AppRepo::kMaxEntries];
                const char* vers[AppRepo::kMaxEntries];
                int n = 0;
                const char* first_name = "";
                for(int i = 0; i < store_->count(); i++){
                    if(store_->installed(i) != AppStore::Installed::Update || !store_->installable(i) || store_->held(i)) continue;
                    const AppRepo::Entry* e = store_->entry(i);
                    if(n == 0) first_name = e->name.c_str();
                    ids[n] = e->id.c_str();
                    vers[n] = e->version.c_str();
                    n++;
                }
                const uint32_t fp = StoreWatch::Fingerprint(ids, vers, n);
                int64_t checked = 0;
                uint32_t notified = 0;
                ReadWatchState(checked, notified);
                if(n > 0 && fp != notified){
                    NotificationFunctions::Content c;
                    NotificationFunctions::Sanitize(c.title, TR("アプリの更新があります"));
                    char body[PICO_STR_L];
                    if(n == 1) snprintf(body, sizeof(body), TR("%s の新しい版があります"), first_name);
                    else snprintf(body, sizeof(body), TR("%s など%d件のアプリに新しい版があります"), first_name, n);
                    NotificationFunctions::Sanitize(c.body, body);
                    NotificationFunctions::Sanitize(c.app, AppStore::kAppName);
                    c.sound = false;
                    NotificationFunctions::Post(c);
                    LOG_SYS_MSG("StoreWatch: 更新%d件を知らせました", n);
                }
                char num[24];
                snprintf(num, sizeof(num), "%lu", (unsigned long)fp);
                PICO_Config::SetValue(STORE_STATE, "notified", num);
                if(TimeFunctions::IsClockSet()){
                    snprintf(num, sizeof(num), "%lld", (long long)time(nullptr));
                    PICO_Config::SetValue(STORE_STATE, "checked", num);
                }
                last_error_[0] = '\0';
                last_ok_ms_ = now == 0 ? 1 : now;
                last_updates_ = n;
                next_ms_ = now + kFailRetryMs;
            }

            void drop(){
                if(!store_) return;
                store_->cancel();
                store_->close();
                delete store_;
                store_ = nullptr;
            }

            void setError(const char* why){
                strncpy(last_error_, why ? why : "", sizeof(last_error_) - 1);
                last_error_[sizeof(last_error_) - 1] = '\0';
            }

            bool enabled_ = true;
            bool started_ = false;
            bool force_ = false;
            AppStore* store_ = nullptr;
            uint32_t next_ms_ = 0;
            uint32_t started_ms_ = 0;
            uint32_t last_ok_ms_ = 0;
            int last_updates_ = 0;
            char last_error_[96] = "";
    };

    Service service;
}

BackgroundService* StoreWatch::Service(){ return &service; }
