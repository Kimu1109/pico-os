#include "todo/Todo_Reminders.hpp"

#include "functions/Notification_Functions.hpp"

#include <cstdio>
#include <cstring>

namespace {
    using namespace NotificationFunctions;

    constexpr size_t kPrefixLen = 3; // "td:"

    bool IsOurs(const Rule* r){
        return r && r->content.owner.empty()
            && strncmp(r->content.tag.c_str(), TodoReminders::kTagPrefix, kPrefixLen) == 0;
    }

    const char* TaskIdOf(const Rule* r){ return r->content.tag.c_str() + kPrefixLen; }

    const Todoist::Task* FindTask(const Todoist::Task* tasks, int count, const char* id){
        for(int i = 0; i < count; i++){
            if(strcmp(tasks[i].id.c_str(), id) == 0) return &tasks[i];
        }
        return nullptr;
    }

    // 並べるもの: 一覧から入れたい予約(task != nullptr)か、範囲の外で残す予約(rule_id != 0)
    struct Item {
        int64_t fire = 0;
        const Todoist::Task* task = nullptr;
        uint16_t rule_id = 0;
    };

    // RuleAt() は消すと詰まるので、先に id を控えてから消す
    void CancelIds(const uint16_t* ids, int n){
        for(int i = 0; i < n; i++) CancelById(ids[i]);
    }
}

int TodoReminders::Sync(const Todoist::Task* tasks, int count, int64_t now_epoch, int64_t covered_until_epoch,
                        int32_t utc_offset_sec, int before_min, const char* app_name){
    const int64_t before_sec = (int64_t)before_min * 60;

    // ---- 一覧から、これから知らせるもの ----
    Item items[kMaxRules + 32];
    int n = 0;
    for(int i = 0; i < count && n < 32; i++){
        int64_t due = 0;
        if(!Todoist::DueEpoch(tasks[i].due, utc_offset_sec, due)) continue;
        const int64_t fire = due - before_sec;
        if(fire <= now_epoch) continue;
        //tag に入らない長いidは扱わない
        if(tasks[i].id.length() + kPrefixLen >= PICO_STR_S) continue;
        items[n].fire = fire;
        items[n].task = &tasks[i];
        n++;
    }

    // ---- 前に入れた予約を見る ----
    uint16_t cancel_ids[kMaxRules];
    int cancel_n = 0;
    int others = 0; //C++(空の送り主)の、ほかの用途の予約
    for(int i = 0; i < RuleCount(); i++){
        const Rule* r = RuleAt(i);
        if(!r) continue;
        if(!IsOurs(r)){
            if(r->content.owner.empty()) others++;
            continue;
        }
        const char* id = TaskIdOf(r);
        bool wanted = false;
        for(int k = 0; k < n; k++){
            if(strcmp(items[k].task->id.c_str(), id) == 0){ wanted = true; break; }
        }
        if(wanted) continue; //下で入れ直す(変わっていなければ触らない)

        const bool in_list = FindTask(tasks, count, id) != nullptr;
        const int64_t due = r->when.at_epoch + before_sec;
        if(in_list || due <= covered_until_epoch){
            //一覧にあるのに知らせる時刻が無くなった(日付だけにした・過ぎた)か、
            //一覧が網羅している範囲なのに載っていない(完了した/消した/期限を変えた)
            cancel_ids[cancel_n++] = r->id;
        }else{
            //別の表示で入れたもの(この一覧の範囲の外)。残す
            items[n].fire = r->when.at_epoch;
            items[n].rule_id = r->id;
            n++;
        }
    }
    CancelIds(cancel_ids, cancel_n);

    // ---- 近い順に、入る数だけ ----
    for(int i = 1; i < n; i++){
        Item t = items[i];
        int j = i;
        while(j > 0 && items[j - 1].fire > t.fire){ items[j] = items[j - 1]; j--; }
        items[j] = t;
    }
    int quota = kMaxRulesPerOwner - others;
    if(quota < 0) quota = 0;

    cancel_n = 0;
    for(int i = quota; i < n; i++){
        if(items[i].rule_id != 0){
            cancel_ids[cancel_n++] = items[i].rule_id;
            continue;
        }
        //入りきらない一覧のタスクに、前の予約が残っていれば消す
        Tag tag;
        tag.assign(kTagPrefix);
        tag.append(items[i].task->id);
        for(int k = 0; k < RuleCount(); k++){
            const Rule* r = RuleAt(k);
            if(IsOurs(r) && r->content.tag == tag) cancel_ids[cancel_n++] = r->id;
        }
    }
    CancelIds(cancel_ids, cancel_n);

    int registered = 0;
    for(int i = 0; i < n && i < quota; i++){
        if(items[i].rule_id != 0){
            registered++;
            continue;
        }
        const Todoist::Task& t = *items[i].task;
        Content c;
        Sanitize(c.title, t.content.c_str());
        char when[48];
        Todoist::FormatDue(t.due, -1, when, sizeof(when));
        char body[96];
        if(before_min > 0) snprintf(body, sizeof(body), "期限 %s(%d分前)", when, before_min);
        else snprintf(body, sizeof(body), "期限 %s", when);
        Sanitize(c.body, body);
        c.tag.assign(kTagPrefix);
        c.tag.append(t.id);
        c.data.assign(t.id);
        c.app.assign(app_name ? app_name : "");
        c.sound = true;

        //同じ予約が既にあれば触らない(SDへの書き込みを減らす)
        bool same = false;
        for(int k = 0; k < RuleCount(); k++){
            const Rule* r = RuleAt(k);
            if(IsOurs(r) && r->content.tag == c.tag && r->when.trigger == Trigger::At
               && r->when.at_epoch == items[i].fire && r->content.title == c.title && r->content.body == c.body){
                same = true;
                break;
            }
        }
        if(same){
            registered++;
            continue;
        }

        When w;
        w.trigger = Trigger::At;
        w.at_epoch = items[i].fire;
        if(Schedule(c, w) == Result::Ok) registered++;
    }
    return registered;
}

void TodoReminders::Cancel(const char* task_id){
    if(!task_id || !*task_id) return;
    Tag tag;
    tag.assign(kTagPrefix);
    tag.append(task_id);
    CancelTag("", tag.c_str());
}

void TodoReminders::CancelAll(){
    uint16_t ids[kMaxRules];
    int n = 0;
    for(int i = 0; i < RuleCount(); i++){
        const Rule* r = RuleAt(i);
        if(IsOurs(r)) ids[n++] = r->id;
    }
    CancelIds(ids, n);
}

int TodoReminders::Count(){
    int n = 0;
    for(int i = 0; i < RuleCount(); i++){
        if(IsOurs(RuleAt(i))) n++;
    }
    return n;
}
