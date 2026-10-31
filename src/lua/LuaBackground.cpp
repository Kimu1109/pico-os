#include "lua/LuaBackground.hpp"

#include "lua/LuaEngine.hpp"
#include "lua/LuaAppScanner.hpp"
#include "services/App_Functions.hpp"
#include "system/Background_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "system/Mem_Functions.hpp"
#include "gui/scenes/LuaScene.hpp"
#include "storage/SD_IO.hpp"
#include "OS_Data.hpp"
#include "Arduino.h"

#include <cstdlib>
#include <cstring>
#include <new>
#include "system/I18n_Functions.hpp"

const char* (*LuaBackground::foreground_script)() = nullptr;

namespace {
    bool Reached(uint32_t now, uint32_t at){ return (int32_t)(now - at) >= 0; }
    // ヒープが足りなくて延ばしたときの次の試し
    constexpr uint32_t kRetryMs = 60000;

    class Job;
    Job* jobs[LuaBackground::kMaxJobs] = {};
    int job_count = 0;

    // 今動いている回(同時に1本)
    Job* running = nullptr;
    LuaEngine* engine = nullptr;
    uint32_t run_started_ms = 0;
    uint32_t last_tick_ms = 0;

    // path(kMaxScriptBytes まで)を読んで malloc した文字列で返す。失敗は nullptr と理由(why)
    char* ReadScript(const char* path, const char*& why){
        FsFile f = OSData::SD.open(path, O_RDONLY);
        if(!f){ why = TR("スクリプトを開けません"); return nullptr; }
        const size_t size = (size_t)f.fileSize();
        if(size > LuaBackground::kMaxScriptBytes){
            f.close();
            why = TR("スクリプトが大きすぎます(16KiBまで)");
            return nullptr;
        }
        char* buf = (char*)malloc(size + 1);
        if(!buf){ f.close(); why = TR("スクリプトを読むメモリが足りません"); return nullptr; }
        size_t got = 0;
        while(got < size){
            const int n = f.read((uint8_t*)buf + got, size - got);
            if(n <= 0) break;
            got += (size_t)n;
        }
        f.close();
        buf[got] = '\0';
        if(got != size){
            free(buf);
            why = TR("スクリプトを最後まで読めません");
            return nullptr;
        }
        return buf;
    }

    class Job : public BackgroundService {
        public:
            FixedString<PICO_STR_L> key_;
            FixedString<PICO_STR_M> name_;
            FixedString<PICO_STR_L> app_dir_;
            FixedString<PICO_STR_LL> script_;
            LuaPermissions perms_;
            uint32_t interval_ms_ = LuaBackground::kDefaultIntervalSec * 1000;
            uint32_t next_ms_ = 0;
            bool enabled_ = true;
            bool seen_ = false;            // Refresh() の印
            FixedString<PICO_STR_LL> last_error_;
            FixedString<PICO_STR_M> note_; // 延ばした理由など(エラーではないもの)
            uint32_t last_done_ms_ = 0;
            uint32_t runs_ = 0;

            const char* key() const override { return key_.c_str(); }
            const char* name() const override { return name_.c_str(); }
            IconID icon() const override { return IconID::AppBox; }
            bool toggleable() const override { return true; }
            bool enabled() const override { return enabled_; }
            void setEnabled(bool on) override {
                enabled_ = on;
                if(!on && running == this) finishRun(TR("止めました"));
            }
            bool active() const override { return enabled_; }
            bool runNow() override {
                if(!enabled_ || running == this) return false;
                next_ms_ = millis();
                return true;
            }
            const char* appName() const override { return name_.c_str(); }

            void status(FixedString<PICO_STR_LL>& out) const override {
                out.clear();
                if(!enabled_){ out.assign(TR("切っています")); return; }
                if(running == this){ out.assign(TR("動いています…")); return; }
                if(!last_error_.empty()){ out.appendFormat(TR("失敗: %s"), last_error_.c_str()); return; }
                if(!note_.empty()){ out.assign(note_.c_str()); return; }
                const int32_t left = (int32_t)(next_ms_ - millis());
                const uint32_t sec = left > 0 ? (uint32_t)left / 1000 : 0;
                if(runs_ == 0) out.assign(TR("まだ動いていません"));
                else out.appendFormat(TR("%lu回動きました"), (unsigned long)runs_);
                if(sec >= 60) out.appendFormat(TR("・次は%lu分後"), (unsigned long)(sec / 60));
                else out.appendFormat(TR("・次は%lu秒後"), (unsigned long)sec);
            }

            void update(uint32_t now) override {
                if(running == this){ step(now); return; }
                if(!enabled_ || running || !Reached(now, next_ms_)) return;
                start(now);
            }

            void finishRun(const char* why){
                if(running != this) return;
                if(engine && engine->lastError()[0]) last_error_.assign(engine->lastError());
                else if(why) last_error_.assign(why);
                delete engine;
                engine = nullptr;
                running = nullptr;
                runs_++;
                last_done_ms_ = millis();
            }

        private:
            void start(uint32_t now){
                //状態の一言は今回のものだけを見せる(前の回の失敗は、見送ったら消える)
                note_.clear();
                last_error_.clear();
                //そのアプリが前に出ていれば今回は見送る(同じファイルを書き合わないように)
                if(LuaBackground::foreground_script){
                    const char* fg = LuaBackground::foreground_script();
                    const size_t n = app_dir_.length();
                    if(fg && strncmp(fg, app_dir_.c_str(), n) == 0 && fg[n] == '/'){
                        note_.assign(TR("アプリを開いているので見送りました"));
                        next_ms_ = now + interval_ms_;
                        return;
                    }
                }
                if(!OSData::SD_usable){
                    note_.assign(TR("SDカードが使えません"));
                    next_ms_ = now + kRetryMs;
                    return;
                }
                //ヒープの残りが足りなければ延ばす(画面のアプリの邪魔をしない)
                const size_t room = MemFunctions::HeapTopRoom();
                if(room != MemFunctions::kRoomUnknown && room < LuaBackground::kBudgetBytes + LuaEngine::kHeapReserveBytes){
                    note_.assign(TR("メモリが足りないので延ばしました"));
                    next_ms_ = now + kRetryMs;
                    return;
                }
                const char* why = "";
                char* src = ReadScript(script_.c_str(), why);
                next_ms_ = now + interval_ms_;
                if(!src){
                    last_error_.assign(why);
                    LOG_APP_WARN("Luaのバックグラウンド(%s): %s", app_dir_.c_str(), why);
                    return;
                }
                engine = new (std::nothrow) LuaEngine(LuaBackground::kBudgetBytes, perms_, app_dir_.c_str(), true);
                if(!engine || !engine->valid()){
                    delete engine;
                    engine = nullptr;
                    free(src);
                    last_error_.assign(TR("Luaを起動できません(メモリ不足)"));
                    return;
                }
                running = this;
                run_started_ms = now;
                last_tick_ms = now;
                engine->SetScriptPath(script_.c_str());
                const bool ok = engine->Run(src, script_.c_str());
                free(src);
                if(!ok){ finishRun(nullptr); return; }
                done(now);
            }

            void step(uint32_t now){
                engine->UpdateHttp();
                if(!engine) return;    //コールバックの中で切られた(setEnabled(false))
                engine->UpdateTimers(now - last_tick_ms);
                last_tick_ms = now;
                if(!engine) return;
                if(Reached(now, run_started_ms + LuaBackground::kMaxRunMs)){
                    LOG_APP_WARN("Luaのバックグラウンド(%s): %lu秒を過ぎたので止めました", app_dir_.c_str(),
                                 (unsigned long)(LuaBackground::kMaxRunMs / 1000));
                    finishRun(TR("時間切れ(通信やタイマーが終わりませんでした)"));
                    return;
                }
                done(now);
            }

            // 残っている仕事(通信・タイマー)が無ければ終える
            void done(uint32_t){
                if(running != this || !engine) return;
                if(engine->finishRequested() || (!engine->HttpBusy() && !engine->timersActive())) finishRun(nullptr);
            }
    };

    Job* FindJob(const char* key){
        for(int i = 0; i < job_count; i++) if(strcmp(jobs[i]->key(), key) == 0) return jobs[i];
        return nullptr;
    }

    void DropJob(int i){
        Job* j = jobs[i];
        if(running == j) j->finishRun(TR("アプリが無くなりました"));
        BackgroundFunctions::Remove(j);
        delete j;
        for(int k = i; k + 1 < job_count; k++) jobs[k] = jobs[k + 1];
        jobs[--job_count] = nullptr;
    }
}

int LuaBackground::Refresh(){
    for(int i = 0; i < job_count; i++) jobs[i]->seen_ = false;

    if(OSData::SD_usable){
        for(int i = 0; i < AppFunctions::Count(); i++){
            const AppEntry* e = AppFunctions::Get(i);
            if(!e || e->create != &MakeLuaAppScene) continue;
            FixedString<PICO_PATH_LEN> dir, cfg_path, script;
            if(!PICO_IO::parent(dir, e->arg.c_str()) || !PICO_IO::join(cfg_path, dir.c_str(), "app.cfg")) continue;
            LuaAppScanner::AppInfo info;
            if(!LuaAppScanner::ReadAppConfig(cfg_path.c_str(), info) || info.background.empty()) continue;
            if(!info.permissions.background){
                LOG_SYS_WARN("LuaBackground: %s は permission_background が無いので background を動かしません", dir.c_str());
                continue;
            }
            //スクリプトはアプリのフォルダの中だけ(".." で外へ出させない)
            FixedString<PICO_PATH_LEN> joined;
            if(!PICO_IO::join(joined, dir.c_str(), info.background.c_str()) || !PICO_IO::normalize(script, joined.c_str())
               || strncmp(script.c_str(), dir.c_str(), dir.length()) != 0 || script.c_str()[dir.length()] != '/'){
                LOG_SYS_WARN("LuaBackground: %s の background(%s)がアプリのフォルダの外を指しています", dir.c_str(), info.background.c_str());
                continue;
            }
            FixedString<PICO_STR_L> key;
            key.assign("lua:");
            if(!key.append(PICO_IO::filename(dir.c_str()))) continue;

            Job* j = FindJob(key.c_str());
            const bool fresh = !j;
            if(fresh){
                if(job_count >= kMaxJobs){
                    LOG_SYS_WARN("LuaBackground: バックグラウンドのアプリが多すぎるため %s を動かしません(上限%d)", dir.c_str(), kMaxJobs);
                    continue;
                }
                j = new (std::nothrow) Job();
                if(!j) continue;
            }
            j->key_ = key;
            j->name_.assign(e->name.c_str());
            if(!j->app_dir_.assign(dir.c_str()) || !j->script_.assign(script.c_str())){
                if(fresh) delete j;
                continue;
            }
            j->perms_ = info.permissions;
            uint32_t sec = info.background_interval_sec ? info.background_interval_sec : kDefaultIntervalSec;
            if(sec < kMinIntervalSec) sec = kMinIntervalSec;
            j->interval_ms_ = sec * 1000;
            j->seen_ = true;
            if(fresh){
                //起動直後に全部が一度に動かないよう、少しずつずらす
                j->next_ms_ = millis() + kFirstRunDelayMs + (uint32_t)job_count * 5000;
                jobs[job_count++] = j;
                if(!BackgroundFunctions::Add(j, true)){
                    jobs[--job_count] = nullptr;
                    delete j;
                }
            }
        }
    }

    for(int i = job_count - 1; i >= 0; i--){
        if(!jobs[i]->seen_) DropJob(i);
    }
    return job_count;
}

LuaEngine* LuaBackground::RunningEngine(){ return engine; }

void LuaBackground::Test::Reset(){
    while(job_count > 0) DropJob(job_count - 1);
    running = nullptr;
    delete engine;
    engine = nullptr;
}
