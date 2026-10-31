#include "apps/calendar/Calendar_Reminders.hpp"

#include "system/Notification_Functions.hpp"
#include "system/Config_Functions.hpp"
#include "storage/SD_Path.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "system/I18n_Functions.hpp"

namespace {
    using namespace NotificationFunctions;

    constexpr int kMaxItems = 32;   // 1回に並べて選ぶ候補の数(近い順に4件だけ残す)

    bool IsOurs(const Rule* r){
        return r && strcmp(r->content.owner.c_str(), CalendarReminders::kOwner) == 0;
    }

    // UIDが無い予定は題名と開始時刻から作る(同じ予定なら同じ値になればよい)
    uint32_t KeyOf(const IcalEvent& ev){
        if(ev.uid_hash) return ev.uid_hash;
        uint32_t h = 2166136261u;
        for(const char* p = ev.summary.c_str(); *p; p++){ h ^= (uint8_t)*p; h *= 16777619u; }
        h ^= (uint32_t)ev.start.day * 2654435761u;
        h ^= (uint32_t)ev.start.sec;
        return h ? h : 1;
    }

    struct Item {
        int64_t fire = 0;            // 知らせる時刻(UTC)
        int64_t start = 0;           // 予定の始まり(UTC)
        const IcalEvent* ev = nullptr;
        int32_t day = 0;
        Tag tag;
        uint16_t rule_id = 0;        // 範囲の外で残す予約
    };

    void MakeTag(Tag& t, uint32_t key, int32_t day){
        char buf[24];
        snprintf(buf, sizeof(buf), "%s%08lx:%ld", CalendarReminders::kTagPrefix, (unsigned long)key, (long)day);
        t.assign(buf);
    }
}

CalendarReminders::Settings CalendarReminders::LoadSettings(){
    Settings s;
    PICO_Config::ParseFile(PICO_Path::FILE::CFG::SYS_CALENDAR_CFG, [&](const char* key, const char* value){
        if(strcmp(key, "reminders") == 0){
            bool b = true;
            if(PICO_Config::ConfigValue::AsBool(value, b)) s.enabled = b;
        }else if(strcmp(key, "remind-before-min") == 0){
            int v = 0;
            if(PICO_Config::ConfigValue::AsInt(value, v) && v >= 0 && v <= 24 * 60) s.before_min = v;
        }
    });
    return s;
}

int CalendarReminders::Sync(const IcalCalendar& cal, int32_t from_day, int32_t to_day, int64_t now_epoch,
                            int32_t utc_offset_sec, int before_min, const char* app_name){
    const int64_t before_sec = (int64_t)before_min * 60;

    // ---- 読み込んだ範囲の、これから知らせる回 ----
    static Item items[kMaxItems + kMaxRules];   // 約3KB。スタックに置かない
    int n = 0;
    for(int32_t day = from_day; day < to_day; day++){
        for(int i = 0; i < cal.count; i++){
            const IcalEvent& ev = cal.events[i];
            if(ev.cancelled || ev.start.isAllDay()) continue;
            if(!Ical::StartsOn(ev, day)) continue;
            const int64_t start = (int64_t)day * 86400 + ev.start.sec - utc_offset_sec;
            const int64_t fire = start - before_sec;
            if(fire <= now_epoch) continue;
            // 遠いものから捨てる(日の順に見ているので、溢れたら一番遠いものと入れ替える)
            int slot = n;
            if(n >= kMaxItems){
                int far = 0;
                for(int k = 1; k < n; k++) if(items[k].fire > items[far].fire) far = k;
                if(items[far].fire <= fire) continue;
                slot = far;
            }else{
                n++;
            }
            Item& it = items[slot];
            it = Item();
            it.fire = fire;
            it.start = start;
            it.ev = &ev;
            it.day = day;
            MakeTag(it.tag, KeyOf(ev), day);
        }
    }

    // ---- 前に入れた予約 ----
    uint16_t cancel_ids[kMaxRules];
    int cancel_n = 0;
    for(int i = 0; i < RuleCount(); i++){
        const Rule* r = RuleAt(i);
        if(!IsOurs(r)) continue;
        bool wanted = false;
        for(int k = 0; k < n; k++){
            if(items[k].tag == r->content.tag){ wanted = true; break; }
        }
        if(wanted) continue;
        // 予定の始まり。予約の時刻と通知の何分前かは変わりうるので、tagの日を見て範囲を判断する
        const char* colon = strrchr(r->content.tag.c_str(), ':');
        const long day = colon ? strtol(colon + 1, nullptr, 10) : 0;
        if((day >= from_day && day < to_day) || r->when.at_epoch <= now_epoch){
            cancel_ids[cancel_n++] = r->id;   // 範囲の中なのに無い(消した/動かした)
        }else if(n < kMaxItems + kMaxRules){
            Item& it = items[n++];
            it = Item();
            it.fire = r->when.at_epoch;
            it.rule_id = r->id;
            it.tag = r->content.tag;
        }
    }
    for(int i = 0; i < cancel_n; i++) CancelById(cancel_ids[i]);

    // ---- 近い順に並べて、入る数だけ ----
    for(int i = 1; i < n; i++){
        Item t = items[i];
        int j = i;
        while(j > 0 && items[j - 1].fire > t.fire){ items[j] = items[j - 1]; j--; }
        items[j] = t;
    }
    const int quota = kMaxRulesPerOwner;

    cancel_n = 0;
    for(int i = quota; i < n; i++){
        for(int k = 0; k < RuleCount(); k++){
            const Rule* r = RuleAt(k);
            if(IsOurs(r) && r->content.tag == items[i].tag) cancel_ids[cancel_n++] = r->id;
        }
    }
    for(int i = 0; i < cancel_n; i++) CancelById(cancel_ids[i]);

    int registered = 0;
    for(int i = 0; i < n && i < quota; i++){
        if(items[i].rule_id != 0){ registered++; continue; }
        const IcalEvent& ev = *items[i].ev;

        Content c;
        Sanitize(c.title, ev.summary.empty() ? TR("(無題の予定)") : ev.summary.c_str());
        const int hh = ev.start.sec / 3600, mm = (ev.start.sec / 60) % 60;
        char body[96];
        int len = (before_min > 0)
            ? snprintf(body, sizeof(body), TR("%02d:%02dから(%d分前)"), hh, mm, before_min)
            : snprintf(body, sizeof(body), TR("%02d:%02dから"), hh, mm);
        if(!ev.location.empty() && len > 0 && len < (int)sizeof(body)){
            snprintf(body + len, sizeof(body) - len, " %s", ev.location.c_str());
        }
        Sanitize(c.body, body);
        c.tag = items[i].tag;
        char data[16];
        snprintf(data, sizeof(data), "%ld", (long)items[i].day);
        c.data.assign(data);
        c.app.assign(app_name ? app_name : "");
        c.owner.assign(kOwner);
        c.sound = true;

        bool same = false;
        for(int k = 0; k < RuleCount(); k++){
            const Rule* r = RuleAt(k);
            if(IsOurs(r) && r->content.tag == c.tag && r->when.trigger == Trigger::At
               && r->when.at_epoch == items[i].fire && r->content.title == c.title && r->content.body == c.body){
                same = true;
                break;
            }
        }
        if(same){ registered++; continue; }

        //同じtagの古い予約(時刻や題名が変わった)は置き換え(Schedule()が同じ送り主・同じtagを置き換える)
        When w;
        w.trigger = Trigger::At;
        w.at_epoch = items[i].fire;
        if(Schedule(c, w) == Result::Ok) registered++;
    }
    return registered;
}

void CalendarReminders::CancelAll(){
    NotificationFunctions::CancelAll(kOwner);
}

int CalendarReminders::Count(){
    int n = 0;
    for(int i = 0; i < RuleCount(); i++) if(IsOurs(RuleAt(i))) n++;
    return n;
}
