#include "apps/todo/Todo_Watch.hpp"

#include "apps/todo/Todo_Cache.hpp"
#include "apps/todo/Todoist_Client.hpp"
#include "apps/home/Home_Layout.hpp"
#include "apps/home/Home_Time.hpp"
#include "system/Background_Functions.hpp"
#include "hal/Network_Functions.hpp"
#include "system/Time_Functions.hpp"
#include "storage/SD_Path.hpp"
#include "OS_Data.hpp"
#include "Arduino.h"

#include <cstring>
#include <ctime>
#include <new>

bool (*TodoWatch::foreground_query)() = nullptr;

namespace {
    bool Reached(uint32_t now, uint32_t at){ return (int32_t)(now - at) >= 0; }

    class Service : public BackgroundService {
        public:
            const char* key() const override { return "todo"; }
            const char* name() const override { return "TODOの取り直し"; }
            IconID icon() const override { return IconID::CheckboxOn; }
            bool toggleable() const override { return true; }
            bool enabled() const override { return enabled_; }
            void setEnabled(bool on) override {
                enabled_ = on;
                if(!on) drop();
                next_ms_ = millis();
            }
            const char* appName() const override { return "TODO"; }
            // ホーム画面にTODOウィジェットを置いていて、設定がある間だけ動いている扱い
            bool active() const override { return enabled_ && configured_ && HomeLayout::HasKind(HomeLayout::Kind::Todo); }
            bool runNow() override {
                if(!enabled_ || client_) return false;
                next_ms_ = millis();
                return true;
            }

            void status(FixedString<PICO_STR_LL>& out) const override {
                out.clear();
                if(!enabled_){ out.assign("切っています"); return; }
                if(!HomeLayout::HasKind(HomeLayout::Kind::Todo)){ out.assign("ホーム画面にTODOウィジェットがありません"); return; }
                if(!configured_){ out.assign("Todoistが設定されていません"); return; }
                if(client_){ out.assign("取っています…"); return; }
                if(last_error_[0]){ out.appendFormat("失敗: %s", last_error_); return; }
                if(last_ok_ms_ != 0){
                    const uint32_t ago = (millis() - last_ok_ms_) / 60000;
                    out.appendFormat("%lu分前に取得(%d件)", (unsigned long)ago, TodoCache::Total());
                    return;
                }
                out.assign("まだ取っていません");
            }

            void update(uint32_t now) override {
                if(!enabled_) return;
                if(!HomeLayout::HasKind(HomeLayout::Kind::Todo)){
                    drop();
                    return;
                }
                if(client_){ step(now); return; }
                if(!Reached(now, next_ms_)) return;
                if(TodoWatch::foreground_query && TodoWatch::foreground_query()){
                    next_ms_ = now + TodoWatch::kIntervalMs;
                    return;
                }
                start(now);
            }

        private:
            void start(uint32_t now){
                //設定が無いのが普通(ログを出さずに、間隔ごとに見直す)
                if(!OSData::SD_usable || !OSData::SD.exists(PICO_Path::FILE::CFG::SYS_TODOIST_CFG)){
                    configured_ = false;
                    next_ms_ = now + TodoWatch::kIntervalMs;
                    return;
                }
                if(!NetworkFunctions::IsConnected() || !TimeFunctions::IsClockSet()){
                    next_ms_ = now + 10000;
                    return;
                }
                client_ = new (std::nothrow) TodoistClient();
                if(!client_){
                    setError("メモリが足りません");
                    next_ms_ = now + TodoWatch::kRetryMs;
                    return;
                }
                configured_ = client_->loadConfig();
                if(!configured_){
                    drop();
                    next_ms_ = now + TodoWatch::kIntervalMs;
                    return;
                }
                client_->setUtcOffset(HomeTime::UtcOffsetSec());
                client_->setView(TodoistClient::View::Today);
                client_->refreshNow();
                tasks_rev_ = client_->tasksRevision();
                status_rev_ = client_->statusRevision();
                started_ms_ = now;
            }

            void step(uint32_t now){
                client_->update(NetworkFunctions::IsConnected());
                if(client_->loaded() && client_->tasksRevision() != tasks_rev_){
                    TodoCache::Store(&client_->taskAt(0), client_->taskCount(), true,
                                     HomeTime::Today(), (int64_t)time(nullptr));
                    last_error_[0] = '\0';
                    last_ok_ms_ = now;
                    drop();
                    next_ms_ = now + TodoWatch::kIntervalMs;
                    return;
                }
                const TodoistClient::State st = client_->state();
                const bool failed = client_->statusRevision() != status_rev_
                                 && (st == TodoistClient::State::Error || st == TodoistClient::State::AuthError
                                     || st == TodoistClient::State::Offline);
                if(failed || Reached(now, started_ms_ + TodoWatch::kGiveUpMs)){
                    setError(failed ? client_->statusText() : "時間切れ");
                    drop();
                    next_ms_ = now + (st == TodoistClient::State::AuthError ? TodoWatch::kIntervalMs : TodoWatch::kRetryMs);
                }
            }

            void drop(){
                if(!client_) return;
                client_->stop();
                delete client_;
                client_ = nullptr;
            }

            void setError(const char* why){
                strncpy(last_error_, why ? why : "", sizeof(last_error_) - 1);
                last_error_[sizeof(last_error_) - 1] = '\0';
            }

            bool enabled_ = true;
            bool configured_ = true;
            TodoistClient* client_ = nullptr;
            uint32_t next_ms_ = 0;
            uint32_t started_ms_ = 0;
            uint32_t last_ok_ms_ = 0;
            uint32_t tasks_rev_ = 0;
            uint32_t status_rev_ = 0;
            char last_error_[96] = "";
    };

    Service service;
}

BackgroundService* TodoWatch::Service(){ return &service; }
