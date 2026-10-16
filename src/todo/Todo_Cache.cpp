#include "todo/Todo_Cache.hpp"

#include "functions/Log_Functions.hpp"
#include "storage/SD_Path.hpp"
#include "OS_Data.hpp"

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
    TodoCache::Entry entries[TodoCache::kMax];
    int entry_count = 0;
    int total = 0;
    int64_t fetched = 0;
    bool has = false;
    bool loaded = false;
    uint32_t revision = 1;

    bool Save(){
        if(!OSData::SD_usable) return false;
        const char* path = PICO_Path::FILE::SYS_TODO_CACHE;
        char tmp[PICO_PATH_LEN];
        snprintf(tmp, sizeof(tmp), "%s.tmp", path);
        FsFile f = OSData::SD.open(tmp, O_WRONLY | O_CREAT | O_TRUNC);
        if(!f) return false;
        bool ok = true;
        char line[160];
        snprintf(line, sizeof(line), "#todo %" PRId64 " %d\n", fetched, total);
        ok = f.write(line, strlen(line)) == strlen(line);
        for(int i = 0; i < entry_count && ok; i++){
            const TodoCache::Entry& e = entries[i];
            snprintf(line, sizeof(line), "%ld\t%ld\t%d\t%s\n", (long)e.due.day,
                     (long)(e.due.has_time ? e.due.sec : -1), (int)e.priority, e.content.c_str());
            ok = f.write(line, strlen(line)) == strlen(line);
        }
        f.close();
        if(!ok){ OSData::SD.remove(tmp); return false; }
        if(OSData::SD.exists(path)) OSData::SD.remove(path);
        return OSData::SD.rename(tmp, path);
    }
}

void TodoCache::Store(const Todoist::Task* tasks, int count, bool all_due_by_today, int32_t today_day, int64_t now_epoch){
    entry_count = 0;
    total = 0;
    for(int i = 0; i < count; i++){
        const Todoist::Task& t = tasks[i];
        if(!all_due_by_today && (!t.due.has || today_day < 0 || t.due.day > today_day)) continue;
        total++;
        if(entry_count >= kMax) continue;
        Entry& e = entries[entry_count++];
        e.due = t.due;
        e.due.text.clear();
        e.priority = t.priority;
        //タブ・改行は1行の書式を壊すので空白へ。切り詰まってよい(1行で見せるだけ)
        char buf[PICO_STR_LL]; //Content と同じ大きさ(ここでは切らず、assign() に文字の境目で切らせる)
        strncpy(buf, t.content.c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        for(char* p = buf; *p; p++) if(*p == '\t' || *p == '\n' || *p == '\r') *p = ' ';
        e.content.assign(buf);
    }
    fetched = now_epoch;
    has = true;
    loaded = true;
    revision++;
    if(!Save()) LOG_SYS_WARN("TODO: %s を書けませんでした", PICO_Path::FILE::SYS_TODO_CACHE);
}

void TodoCache::EnsureLoaded(){
    if(loaded) return;
    if(!OSData::SD_usable) return;
    loaded = true;
    FsFile f = OSData::SD.open(PICO_Path::FILE::SYS_TODO_CACHE, O_RDONLY);
    if(!f) return;
    char line[200];
    bool head = false;
    entry_count = 0;
    while(true){
        const int n = f.fgets(line, sizeof(line));
        if(n <= 0) break;
        size_t len = strlen(line);
        while(len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
        if(!head){
            long long ep = 0;
            int tot = 0;
            if(sscanf(line, "#todo %lld %d", &ep, &tot) != 2) break;
            fetched = (int64_t)ep;
            total = tot;
            head = true;
            continue;
        }
        if(entry_count >= kMax) break;
        char* save = nullptr;
        char* f_day = strtok_r(line, "\t", &save);
        char* f_sec = strtok_r(nullptr, "\t", &save);
        char* f_pri = strtok_r(nullptr, "\t", &save);
        char* f_txt = strtok_r(nullptr, "", &save);
        if(!f_day || !f_sec || !f_pri || !f_txt) continue;
        Entry& e = entries[entry_count++];
        e = Entry{};
        e.due.has = true;
        e.due.day = (int32_t)atol(f_day);
        const long sec = atol(f_sec);
        e.due.has_time = sec >= 0;
        e.due.sec = sec >= 0 ? (int32_t)sec : 0;
        e.priority = (uint8_t)atoi(f_pri);
        e.content.assign(f_txt);
    }
    f.close();
    has = head;
    revision++;
}

bool TodoCache::Has(){ return has; }
int TodoCache::Count(){ return entry_count; }
int TodoCache::Total(){ return total; }
const TodoCache::Entry& TodoCache::At(int i){ return entries[(i < 0 || i >= entry_count) ? 0 : i]; }
int64_t TodoCache::FetchedEpoch(){ return fetched; }
uint32_t TodoCache::Revision(){ return revision; }

void TodoCache::Test::Reset(){
    entry_count = 0;
    total = 0;
    fetched = 0;
    has = false;
    loaded = false;
    revision++;
}
