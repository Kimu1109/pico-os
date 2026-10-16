#include "home/Home_Agenda.hpp"

#include "home/Home_Time.hpp"
#include "calendar/Ical.hpp"
#include "functions/Log_Functions.hpp"
#include "storage/SD_Path.hpp"
#include "storage/SD_IO.hpp"
#include "OS_Data.hpp"

#include <cstring>
#include <new>

namespace {
    HomeAgenda::Entry entries[HomeAgenda::kMaxEntries];
    int entry_count = 0;
    uint32_t revision = 1;
    int32_t loaded_day = -1;
    bool stale = true;
    bool no_files = true;

    constexpr int kMaxFiles = 8;
    // カレンダーアプリと同じ色(ファイル名順)
    const int8_t kColors[] = {
        PICO_DARKGREEN, PICO_BLUE, PICO_RED, PICO_PURPLE,
        PICO_DARKCYAN, PICO_MAROON, PICO_NAVY, PICO_OLIVE,
    };

    bool EndsWithIcs(const char* name){
        const size_t n = strlen(name);
        if(n < 4) return false;
        const char* e = name + n - 4;
        return e[0] == '.' && (e[1] | 0x20) == 'i' && (e[2] | 0x20) == 'c' && (e[3] | 0x20) == 's';
    }

    int32_t SecOr0(int32_t s){ return s < 0 ? 0 : s; }
}

void HomeAgenda::Refresh(int32_t today, int32_t now_sec, int32_t utc_offset_sec){
    entry_count = 0;
    revision++;
    loaded_day = today;
    stale = false;
    no_files = true;
    if(today < 0 || !OSData::SD_usable) return;

    // ---- /calendar/*.ics を名前順に(色をカレンダーアプリと揃えるため) ----
    FixedString<PICO_STR_M> names[kMaxFiles];
    int file_count = 0;
    {
        FsFile dir = OSData::SD.open(PICO_Path::DIR::CALENDAR);
        if(!dir) return;
        FsFile file;
        while(file.openNext(&dir, O_RDONLY)){
            char name[128];
            const bool ok = file.getName(name, sizeof(name)) && !file.isDirectory() && EndsWithIcs(name);
            file.close();
            if(!ok || file_count >= kMaxFiles) continue;
            FixedString<PICO_STR_M> n;
            if(!n.assign(name)) continue;
            int i = file_count++;
            while(i > 0 && strcmp(names[i - 1].c_str(), n.c_str()) > 0){
                names[i] = names[i - 1];
                i--;
            }
            names[i] = n;
        }
        dir.close();
    }
    if(file_count == 0) return;
    no_files = false;

    //読む間だけ(約16KB)
    IcalCalendar* cal = new (std::nothrow) IcalCalendar();
    if(!cal){
        LOG_SYS_WARN("ホーム: 予定を読むメモリが足りません");
        return;
    }
    Ical::Options opt;
    opt.utc_offset_sec = utc_offset_sec;
    opt.window_from_day = today;
    opt.window_to_day = today + kDays;
    for(int i = 0; i < file_count; i++){
        FixedString<PICO_PATH_LEN> path;
        if(!PICO_IO::join(path, PICO_Path::DIR::CALENDAR, names[i].c_str())) continue;
        opt.file_index = (uint8_t)i;
        Ical::ParseFile(path.c_str(), *cal, opt);
    }

    for(int32_t d = today; d < today + kDays && entry_count < kMaxEntries; d++){
        uint8_t idx[16];
        const int n = Ical::EventsOn(*cal, d, idx, 16);
        for(int k = 0; k < n && entry_count < kMaxEntries; k++){
            const IcalEvent& ev = cal->events[idx[k]];
            const bool starts = Ical::StartsOn(ev, d);
            //明日以降は、その日に始まるものだけ(続きは今日の欄にだけ出す)
            if(!starts && d != today) continue;
            Entry e;
            e.day = d;
            e.continued = !starts;
            e.color = file_count > 1 ? kColors[ev.file_index % (sizeof(kColors) / sizeof(kColors[0]))] : PICO_DARKGREEN;
            e.title.assign(ev.summary.c_str());
            if(starts && !ev.start.isAllDay()){
                e.start_sec = ev.start.sec;
                const int32_t dur = (ev.end.day - ev.start.day) * 86400 + SecOr0(ev.end.sec) - ev.start.sec;
                e.end_sec = ev.start.sec + (dur > 0 ? dur : 0);
                //今日、もう終わった予定は出さない
                if(d == today && e.end_sec <= now_sec && dur > 0) continue;
            }
            entries[entry_count++] = e;
        }
    }
    delete cal;
}

void HomeAgenda::RefreshIfStale(){
    int32_t now_sec = 0;
    const int32_t today = HomeTime::Today(&now_sec);
    if(!stale && today == loaded_day) return;
    Refresh(today, now_sec, HomeTime::UtcOffsetSec());
}

void HomeAgenda::Invalidate(){ stale = true; }
int HomeAgenda::Count(){ return entry_count; }
const HomeAgenda::Entry& HomeAgenda::At(int i){ return entries[(i < 0 || i >= entry_count) ? 0 : i]; }
uint32_t HomeAgenda::Revision(){ return revision; }
int32_t HomeAgenda::Day(){ return loaded_day; }
bool HomeAgenda::NoFiles(){ return no_files; }
