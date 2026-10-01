#include "functions/Notification_Functions.hpp"
#include "functions/Config_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "storage/SD_Path.hpp"
#include "OS_Data.hpp"

#include <Arduino.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
    using namespace NotificationFunctions;

    Rule  rules[kMaxRules];
    uint16_t next_rule_id = 1;

    // 履歴は輪。history_head が次に書く場所、history_count が入っている数
    Entry history[kMaxHistory];
    int   history_head = 0;
    int   history_count = 0;
    uint32_t next_seq = 1;

    uint32_t toast_queue[kMaxToastQueue];
    int   toast_head = 0;   // 次に取り出す場所
    int   toast_count = 0;

    int   chime_pending = 0;
    uint32_t revision = 1;

    Mode  mode = Mode::On;
    bool  sound_enabled = true;

    // 保存待ち(変更があったら kSaveDelayMs 後にまとめて書く)
    bool  save_pending = false;
    unsigned long save_requested_ms = 0;

    // 最後にUpdateAt()へ渡された時刻。Post()/Schedule()はこれを「今」として使う
    unsigned long last_now_ms = 0;
    int64_t last_epoch = 0;
    bool    clock_valid = false;
    Sensors last_sensors;
    bool    has_sensors = false;

    // 起動理由(Open()→TakeLaunchReason())
    bool  reason_pending = false;
    Owner reason_owner;
    Tag   reason_tag;
    Tag   reason_data;
    unsigned long reason_ms = 0;

    bool (*launcher)(const char* app_name) = nullptr;

    const char* const kTriggerNames[] = {
        "now", "delay", "at", "daily", "every", "battery_low", "wifi_connected", "wifi_disconnected"
    };
    constexpr int kTriggerCount = sizeof(kTriggerNames) / sizeof(kTriggerNames[0]);

    void Touch(){ revision++; }

    void RequestSave(){
        save_pending = true;
        save_requested_ms = last_now_ms;
    }

    bool IsPersistent(Trigger t){
        return t != Trigger::Now && t != Trigger::Delay;
    }

    template<size_t N>
    bool SanitizeImpl(FixedString<N>& out, const char* s){
        out.clear();
        if(!s) return true;
        char buf[N];
        size_t i = 0;
        for(; s[i] != '\0' && i < N - 1; i++){
            const char c = s[i];
            buf[i] = (c == '\t' || c == '\n' || c == '\r') ? ' ' : c;
        }
        buf[i] = '\0';
        //assign()がUTF-8の途中で切れた文字を巻き戻す
        const bool fit = (s[i] == '\0');
        out.assign(buf);
        return fit;
    }

    // ---- 履歴 ----

    Entry* EntryAtIndex(int index){
        if(index < 0 || index >= history_count) return nullptr;
        int pos = history_head - 1 - index;
        while(pos < 0) pos += kMaxHistory;
        return &history[pos];
    }

    Entry* FindEntryMut(uint32_t seq){
        if(seq == 0) return nullptr;
        for(int i = 0; i < history_count; i++){
            Entry* e = EntryAtIndex(i);
            if(e && e->seq == seq) return e;
        }
        return nullptr;
    }

    // index番目(0=最新)を抜いて詰める
    void RemoveAtIndex(int index){
        if(index < 0 || index >= history_count) return;
        //新しい側から古い側へ1つずつずらす(高々16件)
        for(int i = index; i > 0; i--){
            *EntryAtIndex(i) = *EntryAtIndex(i - 1);
        }
        //一番新しい場所が空く
        history_head = (history_head - 1 + kMaxHistory) % kMaxHistory;
        history[history_head] = Entry();
        history_count--;
    }

    void PushToast(uint32_t seq){
        if(toast_count >= kMaxToastQueue){
            //溢れたら一番古い順番待ちを捨てる(履歴には残っている)
            toast_head = (toast_head + 1) % kMaxToastQueue;
            toast_count--;
        }
        toast_queue[(toast_head + toast_count) % kMaxToastQueue] = seq;
        toast_count++;
    }

    // タブ/改行は保存の区切りなので空白にしておく(C++から直接Contentを組み立てた場合も)
    Content Sanitized(const Content& in){
        Content c = in;
        Sanitize(c.title, in.title.c_str());
        Sanitize(c.body, in.body.c_str());
        Sanitize(c.tag, in.tag.c_str());
        Sanitize(c.data, in.data.c_str());
        Sanitize(c.app, in.app.c_str());
        Sanitize(c.owner, in.owner.c_str());
        return c;
    }

    uint32_t Deliver(const Content& in){
        if(in.title.empty()) return 0;
        const Content c = Sanitized(in);

        //同じ送り主・同じtagの通知は置き換える(古いほうを履歴から消す)
        if(!c.tag.empty()){
            for(int i = history_count - 1; i >= 0; i--){
                Entry* e = EntryAtIndex(i);
                if(e && e->content.tag == c.tag && e->content.owner == c.owner) RemoveAtIndex(i);
            }
        }

        Entry& e = history[history_head];
        e = Entry();
        e.seq = next_seq++;
        if(next_seq == 0) next_seq = 1;
        e.content = c;
        e.epoch = clock_valid ? last_epoch : 0;
        e.ms = last_now_ms;
        e.read = false;
        history_head = (history_head + 1) % kMaxHistory;
        if(history_count < kMaxHistory) history_count++;

        if(mode == Mode::On){
            PushToast(e.seq);
            if(sound_enabled && c.sound) chime_pending++;
        }

        LOG_APP_MSG("通知: %s%s%s", c.title.c_str(), c.body.empty() ? "" : " / ", c.body.c_str());
        Touch();
        return e.seq;
    }

    // ---- 予約 ----

    int OwnerRuleCount(const Owner& owner){
        int n = 0;
        for(int i = 0; i < kMaxRules; i++){
            if(rules[i].id != 0 && rules[i].content.owner == owner) n++;
        }
        return n;
    }

    void ArmRule(Rule& r){
        r.last_minute_key = -1;
        r.armed = true;
        switch(r.when.trigger){
            case Trigger::Delay: r.due_ms = last_now_ms + r.when.delay_ms; break;
            case Trigger::Every: r.due_ms = last_now_ms + r.when.every_ms; break;
            case Trigger::WifiConnected:
            case Trigger::WifiDisconnected:
                //登録した時点の状態を覚える(登録した瞬間に出さないため)
                r.has_last = has_sensors;
                r.last_state = last_sensors.wifi_connected;
                break;
            case Trigger::Daily:
                //今の分がちょうど指定の時刻でも、登録した分には出さない
                r.last_minute_key = -2;
                break;
            default: break;
        }
    }

    bool ValidWhen(const When& w){
        switch(w.trigger){
            case Trigger::Now:   return true;
            case Trigger::Delay: return w.delay_ms > 0;
            case Trigger::At:    return w.at_epoch > 0;
            case Trigger::Daily: return w.hour < 24 && w.minute < 60;
            case Trigger::Every: return w.every_ms >= kMinEveryMs;
            case Trigger::BatteryLow: return w.below >= 1 && w.below <= 99;
            case Trigger::WifiConnected:
            case Trigger::WifiDisconnected: return true;
        }
        return false;
    }

    long MinuteKey(const struct tm& now){
        return ((long)now.tm_year * 366L + now.tm_yday) * 1440L + now.tm_hour * 60L + now.tm_min;
    }

    // ---- 保存 ----

    constexpr size_t kLineMax = 512;

    bool WriteRule(FsFile& f, const Rule& r){
        char param[32];
        switch(r.when.trigger){
            case Trigger::At:    snprintf(param, sizeof(param), "%lld", (long long)r.when.at_epoch); break;
            case Trigger::Daily: snprintf(param, sizeof(param), "%02u:%02u", r.when.hour, r.when.minute); break;
            case Trigger::Every: snprintf(param, sizeof(param), "%lu", r.when.every_ms); break;
            case Trigger::BatteryLow: snprintf(param, sizeof(param), "%u", r.when.below); break;
            default: snprintf(param, sizeof(param), "-"); break;
        }
        const Content& c = r.content;
        char line[kLineMax];
        const int n = snprintf(line, sizeof(line), "%s\t%s\t%d\t%s\t%s\t%s\t%s\t%s\t%s\n",
            TriggerToStr(r.when.trigger), param, c.sound ? 1 : 0,
            c.owner.c_str(), c.app.c_str(), c.tag.c_str(), c.data.c_str(),
            c.title.c_str(), c.body.c_str());
        if(n <= 0 || n >= (int)sizeof(line)) return false;
        return f.write(line, (size_t)n) == (size_t)n;
    }

    bool SaveRules(){
        if(!OSData::SD_usable) return false;
        const char* path = PICO_Path::FILE::SYS_NOTIFY_RULES;
        char tmp[PICO_PATH_LEN];
        snprintf(tmp, sizeof(tmp), "%s.tmp", path);

        int count = 0;
        for(int i = 0; i < kMaxRules; i++){
            if(rules[i].id != 0 && IsPersistent(rules[i].when.trigger)) count++;
        }
        if(count == 0){
            if(OSData::SD.exists(path)) OSData::SD.remove(path);
            return true;
        }

        FsFile f = OSData::SD.open(tmp, O_WRONLY | O_CREAT | O_TRUNC);
        if(!f){
            LOG_SYS_FAIL("通知: 予約を保存できません (%s)", tmp);
            return false;
        }
        bool ok = true;
        for(int i = 0; i < kMaxRules && ok; i++){
            if(rules[i].id != 0 && IsPersistent(rules[i].when.trigger)) ok = WriteRule(f, rules[i]);
        }
        f.close();
        if(!ok){
            LOG_SYS_FAIL("通知: 予約の書き込みに失敗しました");
            OSData::SD.remove(tmp);
            return false;
        }
        if(OSData::SD.exists(path)) OSData::SD.remove(path);
        if(!OSData::SD.rename(tmp, path)){
            LOG_SYS_FAIL("通知: 予約のファイルを差し替えられません");
            return false;
        }
        return true;
    }

    bool ParseTrigger(const char* s, Trigger& out){
        for(int i = 0; i < kTriggerCount; i++){
            if(strcmp(s, kTriggerNames[i]) == 0){ out = (Trigger)i; return true; }
        }
        return false;
    }

    // 1行をタブで切る(fieldsはlineの中を指す)
    int SplitTabs(char* line, char** fields, int max){
        int n = 0;
        char* p = line;
        while(n < max){
            fields[n++] = p;
            char* t = strchr(p, '\t');
            if(!t) break;
            *t = '\0';
            p = t + 1;
        }
        return n;
    }

    bool ParseRuleLine(char* line, Rule& out){
        //末尾の改行を落とす
        size_t len = strlen(line);
        while(len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
        if(len == 0) return false;

        char* f[9];
        if(SplitTabs(line, f, 9) != 9) return false;

        Rule r;
        if(!ParseTrigger(f[0], r.when.trigger) || !IsPersistent(r.when.trigger)) return false;
        switch(r.when.trigger){
            case Trigger::At:    r.when.at_epoch = strtoll(f[1], nullptr, 10); break;
            case Trigger::Daily: {
                int h = -1, m = -1;
                if(sscanf(f[1], "%d:%d", &h, &m) != 2 || h < 0 || h > 23 || m < 0 || m > 59) return false;
                r.when.hour = (uint8_t)h; r.when.minute = (uint8_t)m;
                break;
            }
            case Trigger::Every: r.when.every_ms = strtoul(f[1], nullptr, 10); break;
            case Trigger::BatteryLow: r.when.below = (uint8_t)atoi(f[1]); break;
            default: break;
        }
        if(!ValidWhen(r.when)) return false;

        r.content.sound = (strcmp(f[2], "0") != 0);
        r.content.owner.assign(f[3]);
        r.content.app.assign(f[4]);
        r.content.tag.assign(f[5]);
        r.content.data.assign(f[6]);
        r.content.title.assign(f[7]);
        r.content.body.assign(f[8]);
        if(r.content.title.empty()) return false;
        out = r;
        return true;
    }

    void LoadRules(){
        const char* path = PICO_Path::FILE::SYS_NOTIFY_RULES;
        if(!OSData::SD_usable || !OSData::SD.exists(path)) return;
        FsFile f = OSData::SD.open(path, O_RDONLY);
        if(!f) return;
        char line[kLineMax];
        int line_no = 0;
        int slot = 0;
        while(true){
            const int n = f.fgets(line, sizeof(line));
            if(n <= 0) break;
            line_no++;
            Rule r;
            if(!ParseRuleLine(line, r)){
                LOG_SYS_WARN("通知: %s の%d行目を読めないので捨てます", path, line_no);
                continue;
            }
            if(slot >= kMaxRules) break;
            r.id = next_rule_id++;
            ArmRule(r);
            rules[slot++] = r;
        }
        f.close();
    }

    void LoadSettings(){
        const char* path = PICO_Path::FILE::CFG::SYS_NOTIFY_CFG;
        if(!OSData::SD_usable || !OSData::SD.exists(path)) return;
        PICO_Config::ParseFile(path, [](const char* key, const char* value){
            if(strcmp(key, "mode") == 0){
                if(strcmp(value, "on") == 0) mode = Mode::On;
                else if(strcmp(value, "quiet") == 0) mode = Mode::Quiet;
                else LOG_SYS_WARN("notify.cfg: mode は on | quiet です: %s", value);
            }else if(strcmp(key, "sound") == 0){
                bool v = true;
                if(PICO_Config::ConfigValue::AsBool(value, v)) sound_enabled = v;
                else LOG_SYS_WARN("notify.cfg: sound は true | false です: %s", value);
            }
        });
    }

    void Fire(Rule& r){
        Deliver(r.content);
        if(r.when.trigger == Trigger::Delay || r.when.trigger == Trigger::At){
            const bool persistent = IsPersistent(r.when.trigger);
            r = Rule();
            if(persistent) RequestSave();
            Touch();
        }
    }
}

// ---------------------------------------------------------------------------

const char* NotificationFunctions::TriggerToStr(Trigger t){
    const int i = (int)t;
    return (i >= 0 && i < kTriggerCount) ? kTriggerNames[i] : "now";
}

const char* NotificationFunctions::ResultToStr(Result r){
    switch(r){
        case Result::Ok:         return "ok";
        case Result::Full:       return "通知の予約がいっぱいです";
        case Result::OwnerQuota: return "このアプリの通知の予約が上限です";
        case Result::Invalid:    return "通知の指定が正しくありません";
    }
    return "?";
}

bool NotificationFunctions::Sanitize(FixedString<PICO_STR_S>& out, const char* s){ return SanitizeImpl(out, s); }
bool NotificationFunctions::Sanitize(FixedString<PICO_STR_M>& out, const char* s){ return SanitizeImpl(out, s); }
bool NotificationFunctions::Sanitize(FixedString<PICO_STR_L>& out, const char* s){ return SanitizeImpl(out, s); }

void NotificationFunctions::SetupAt(unsigned long now_ms){
    for(int i = 0; i < kMaxRules; i++) rules[i] = Rule();
    for(int i = 0; i < kMaxHistory; i++) history[i] = Entry();
    history_head = history_count = 0;
    toast_head = toast_count = 0;
    chime_pending = 0;
    next_rule_id = 1;
    next_seq = 1;
    mode = Mode::On;
    sound_enabled = true;
    save_pending = false;
    last_now_ms = now_ms;
    last_epoch = 0;
    clock_valid = false;
    has_sensors = false;
    last_sensors = Sensors();
    reason_pending = false;
    Touch();

    LoadSettings();
    LoadRules();
}

uint32_t NotificationFunctions::Post(const Content& c){
    return Deliver(c);
}

NotificationFunctions::Result NotificationFunctions::Schedule(const Content& c, const When& w, uint16_t* out_id){
    if(out_id) *out_id = 0;
    if(c.title.empty() || !ValidWhen(w)) return Result::Invalid;
    if(w.trigger == Trigger::Now){
        return Post(c) ? Result::Ok : Result::Invalid;
    }

    //同じ送り主・同じtagは置き換える
    int slot = -1;
    if(!c.tag.empty()){
        for(int i = 0; i < kMaxRules; i++){
            if(rules[i].id != 0 && rules[i].content.owner == c.owner && rules[i].content.tag == c.tag){
                slot = i;
                break;
            }
        }
    }
    if(slot < 0){
        if(!c.owner.empty() && OwnerRuleCount(c.owner) >= kMaxRulesPerOwner) return Result::OwnerQuota;
        for(int i = 0; i < kMaxRules; i++){
            if(rules[i].id == 0){ slot = i; break; }
        }
        if(slot < 0) return Result::Full;
    }

    Rule& r = rules[slot];
    const bool was_persistent = (r.id != 0) && IsPersistent(r.when.trigger);
    r = Rule();
    r.id = next_rule_id++;
    if(next_rule_id == 0) next_rule_id = 1;
    r.when = w;
    r.content = Sanitized(c);
    ArmRule(r);

    if(IsPersistent(w.trigger) || was_persistent) RequestSave();
    Touch();
    if(out_id) *out_id = r.id;
    return Result::Ok;
}

int NotificationFunctions::Cancel(const char* owner, uint16_t id){
    if(id == 0) return 0;
    for(int i = 0; i < kMaxRules; i++){
        Rule& r = rules[i];
        if(r.id == id && r.content.owner == (owner ? owner : "")){
            if(IsPersistent(r.when.trigger)) RequestSave();
            r = Rule();
            Touch();
            return 1;
        }
    }
    return 0;
}

int NotificationFunctions::CancelTag(const char* owner, const char* tag){
    if(!tag || tag[0] == '\0') return 0;
    int n = 0;
    for(int i = 0; i < kMaxRules; i++){
        Rule& r = rules[i];
        if(r.id != 0 && r.content.owner == (owner ? owner : "") && r.content.tag == tag){
            if(IsPersistent(r.when.trigger)) RequestSave();
            r = Rule();
            n++;
        }
    }
    if(n) Touch();
    return n;
}

int NotificationFunctions::CancelAll(const char* owner){
    int n = 0;
    for(int i = 0; i < kMaxRules; i++){
        Rule& r = rules[i];
        if(r.id != 0 && r.content.owner == (owner ? owner : "")){
            if(IsPersistent(r.when.trigger)) RequestSave();
            r = Rule();
            n++;
        }
    }
    if(n) Touch();
    return n;
}

bool NotificationFunctions::CancelById(uint16_t id){
    const Rule* r = FindRule(id);
    if(!r) return false;
    return Cancel(r->content.owner.c_str(), id) > 0;
}

int NotificationFunctions::RuleCount(){
    int n = 0;
    for(int i = 0; i < kMaxRules; i++) if(rules[i].id != 0) n++;
    return n;
}

const NotificationFunctions::Rule* NotificationFunctions::RuleAt(int index){
    if(index < 0) return nullptr;
    for(int i = 0; i < kMaxRules; i++){
        if(rules[i].id == 0) continue;
        if(index-- == 0) return &rules[i];
    }
    return nullptr;
}

const NotificationFunctions::Rule* NotificationFunctions::FindRule(uint16_t id){
    if(id == 0) return nullptr;
    for(int i = 0; i < kMaxRules; i++) if(rules[i].id == id) return &rules[i];
    return nullptr;
}

int NotificationFunctions::HistoryCount(){ return history_count; }
const NotificationFunctions::Entry* NotificationFunctions::HistoryAt(int index){ return EntryAtIndex(index); }
const NotificationFunctions::Entry* NotificationFunctions::FindEntry(uint32_t seq){ return FindEntryMut(seq); }

int NotificationFunctions::UnreadCount(){
    int n = 0;
    for(int i = 0; i < history_count; i++) if(!EntryAtIndex(i)->read) n++;
    return n;
}

void NotificationFunctions::MarkRead(uint32_t seq){
    Entry* e = FindEntryMut(seq);
    if(e && !e->read){ e->read = true; Touch(); }
}

void NotificationFunctions::MarkAllRead(){
    bool changed = false;
    for(int i = 0; i < history_count; i++){
        Entry* e = EntryAtIndex(i);
        if(!e->read){ e->read = true; changed = true; }
    }
    if(changed) Touch();
}

void NotificationFunctions::RemoveEntry(uint32_t seq){
    for(int i = 0; i < history_count; i++){
        if(EntryAtIndex(i)->seq == seq){ RemoveAtIndex(i); Touch(); return; }
    }
}

void NotificationFunctions::ClearHistory(){
    for(int i = 0; i < kMaxHistory; i++) history[i] = Entry();
    history_head = history_count = 0;
    toast_head = toast_count = 0;
    Touch();
}

uint32_t NotificationFunctions::Revision(){ return revision; }

bool NotificationFunctions::PopToast(uint32_t& out_seq){
    while(toast_count > 0){
        const uint32_t seq = toast_queue[toast_head];
        toast_head = (toast_head + 1) % kMaxToastQueue;
        toast_count--;
        //置き換え・消去で履歴から消えたものは飛ばす
        if(FindEntryMut(seq)){ out_seq = seq; return true; }
    }
    return false;
}

int NotificationFunctions::PendingToastCount(){ return toast_count; }

bool NotificationFunctions::TakeChime(){
    if(chime_pending <= 0) return false;
    //同じフレームに何件出ても鳴らすのは1回
    chime_pending = 0;
    return true;
}

bool NotificationFunctions::Open(uint32_t seq){
    Entry* e = FindEntryMut(seq);
    if(!e) return false;
    if(!e->read){ e->read = true; Touch(); }
    if(e->content.app.empty() || !launcher) return false;

    reason_pending = true;
    reason_owner = e->content.owner;
    reason_tag = e->content.tag;
    reason_data = e->content.data;
    reason_ms = last_now_ms;
    if(!launcher(e->content.app.c_str())){
        reason_pending = false;
        return false;
    }
    return true;
}

bool NotificationFunctions::TakeLaunchReason(const char* owner, Tag& out_tag, Tag& out_data){
    if(!reason_pending) return false;
    if(last_now_ms - reason_ms > kLaunchReasonTtlMs){
        reason_pending = false;
        return false;
    }
    if(!(reason_owner == (owner ? owner : ""))) return false;
    out_tag = reason_tag;
    out_data = reason_data;
    reason_pending = false;
    return true;
}

NotificationFunctions::Mode NotificationFunctions::GetMode(){ return mode; }

void NotificationFunctions::SetMode(Mode m, bool persist){
    mode = m;
    if(m == Mode::Quiet){
        //控えめにしたら、待っているトーストも出さない
        toast_head = toast_count = 0;
        chime_pending = 0;
    }
    Touch();
    if(persist && OSData::SD_usable){
        PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_NOTIFY_CFG, "mode", m == Mode::On ? "on" : "quiet");
    }
}

bool NotificationFunctions::GetSoundEnabled(){ return sound_enabled; }

void NotificationFunctions::SetSoundEnabled(bool on, bool persist){
    sound_enabled = on;
    if(!on) chime_pending = 0;
    Touch();
    if(persist && OSData::SD_usable){
        PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_NOTIFY_CFG, "sound", on ? "true" : "false");
    }
}

void NotificationFunctions::RefreshOwners(bool (*resolve)(const char* owner, AppName& out_app)){
    if(!resolve) return;
    bool changed = false;
    for(int i = 0; i < kMaxRules; i++){
        Rule& r = rules[i];
        if(r.id == 0 || r.content.owner.empty()) continue;
        AppName app;
        if(!resolve(r.content.owner.c_str(), app)){
            LOG_SYS_MSG("通知: 送り主(%s)が見つからないので予約を消します: %s",
                        r.content.owner.c_str(), r.content.title.c_str());
            if(IsPersistent(r.when.trigger)) RequestSave();
            r = Rule();
            changed = true;
            continue;
        }
        if(!(r.content.app == app)){
            r.content.app = app;
            if(IsPersistent(r.when.trigger)) RequestSave();
            changed = true;
        }
    }
    if(changed) Touch();
}

void NotificationFunctions::SetLauncher(bool (*launch)(const char* app_name)){ launcher = launch; }

void NotificationFunctions::SaveNow(){
    save_pending = false;
    SaveRules();
}

void NotificationFunctions::UpdateAt(unsigned long now_ms, const struct tm& now, int64_t now_epoch, const Sensors& s){
    last_now_ms = now_ms;
    clock_valid = (now.tm_year + 1900 >= kMinValidYear);
    last_epoch = clock_valid ? now_epoch : 0;

    const long minute_key = clock_valid ? MinuteKey(now) : -1;

    for(int i = 0; i < kMaxRules; i++){
        Rule& r = rules[i];
        if(r.id == 0) continue;
        switch(r.when.trigger){
            case Trigger::Delay:
                if((long)(now_ms - r.due_ms) >= 0) Fire(r);
                break;
            case Trigger::Every:
                if((long)(now_ms - r.due_ms) >= 0){
                    //遅れても溜めて何回も出さない(次は今から数え直す)
                    r.due_ms = now_ms + r.when.every_ms;
                    Fire(r);
                }
                break;
            case Trigger::At:
                if(clock_valid && now_epoch >= r.when.at_epoch) Fire(r);
                break;
            case Trigger::Daily:
                if(!clock_valid) break;
                if(r.last_minute_key == -2){
                    //登録した分は見送る(ちょうどその時刻に登録しても今すぐは出さない)
                    r.last_minute_key = minute_key;
                    break;
                }
                if(r.last_minute_key == minute_key) break;
                r.last_minute_key = minute_key;
                if(now.tm_hour == r.when.hour && now.tm_min == r.when.minute) Fire(r);
                break;
            case Trigger::BatteryLow:
                if(!s.battery_valid) break;
                if(r.armed && s.battery_percent < r.when.below){
                    r.armed = false;
                    Fire(r);
                }else if(!r.armed && s.battery_percent >= r.when.below + kBatteryRearmMargin){
                    r.armed = true;
                }
                break;
            case Trigger::WifiConnected:
            case Trigger::WifiDisconnected: {
                const bool want = (r.when.trigger == Trigger::WifiConnected);
                if(r.has_last && r.last_state != s.wifi_connected && s.wifi_connected == want){
                    r.last_state = s.wifi_connected;
                    Fire(r);
                }else{
                    r.has_last = true;
                    r.last_state = s.wifi_connected;
                }
                break;
            }
            case Trigger::Now:
                r = Rule();
                break;
        }
    }
    last_sensors = s;
    has_sensors = true;

    if(save_pending && now_ms - save_requested_ms >= kSaveDelayMs) SaveNow();
}
