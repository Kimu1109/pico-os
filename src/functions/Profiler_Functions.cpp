#include "functions/Profiler_Functions.hpp"
#include "functions/CrashDump_Functions.hpp"
#include "functions/Log_Functions.hpp"

#include <Arduino.h>
#include <string.h>

namespace {
    bool enabled = false;

    // 今のフレーム
    bool     in_frame = false;
    uint32_t frame_start_us = 0;
    uint32_t last_mark_us = 0;
    uint32_t frame_section_us[(int)ProfilerFunctions::Section::Count] = {};
    uint32_t frame_work_us = 0;
    bool     work_marked = false;

    // 今の窓
    uint32_t window_start_ms = 0;
    uint32_t win_frames = 0;
    uint64_t win_frame_us = 0;
    uint32_t win_frame_max_us = 0;
    uint64_t win_work_us = 0;
    uint64_t win_section_us[(int)ProfilerFunctions::Section::Count] = {};
    uint64_t win_lua_us = 0;

    ProfilerFunctions::Stats last;
    uint32_t window_serial = 0;

    uint32_t history[ProfilerFunctions::kHistory] = {};
    int history_head = 0;   // 次に書く位置
    int history_count = 0;

    uint32_t frame_no = 0;

    const char* const kNames[(int)ProfilerFunctions::Section::Count] = {
        "入力", "電源", "シーン", "画面更新", "描画", "タスク", "Wi-Fi", "サービス",
    };

    void closeWindow(uint32_t now_ms){
        const uint32_t elapsed = now_ms - window_start_ms;
        ProfilerFunctions::Stats s;
        s.frames = win_frames;
        if(win_frames > 0){
            s.frame_avg_us = (uint32_t)(win_frame_us / win_frames);
            s.frame_max_us = win_frame_max_us;
            s.work_avg_us  = (uint32_t)(win_work_us / win_frames);
            for(int i = 0; i < (int)ProfilerFunctions::Section::Count; i++){
                s.section_avg_us[i] = (uint32_t)(win_section_us[i] / win_frames);
            }
            s.lua_avg_us = (uint32_t)(win_lua_us / win_frames);
        }
        s.fps_x10 = elapsed > 0 ? (uint32_t)((uint64_t)win_frames * 10000 / elapsed) : 0;
        last = s;
        window_serial++;

        window_start_ms = now_ms;
        win_frames = 0;
        win_frame_us = 0;
        win_frame_max_us = 0;
        win_work_us = 0;
        memset(win_section_us, 0, sizeof(win_section_us));
        win_lua_us = 0;
    }
}

const char* ProfilerFunctions::SectionName(Section s){
    const int i = (int)s;
    if(i < 0 || i >= (int)Section::Count) return "?";
    return kNames[i];
}

void ProfilerFunctions::SetEnabled(bool e){
    if(e == enabled) return;
    enabled = e;
    in_frame = false;
    win_frames = 0;
    win_frame_us = 0;
    win_frame_max_us = 0;
    win_work_us = 0;
    memset(win_section_us, 0, sizeof(win_section_us));
    win_lua_us = 0;
    window_start_ms = millis();
    history_head = 0;
    history_count = 0;
    lua_us_in_frame = 0;
}

bool ProfilerFunctions::Enabled(){ return enabled; }

void ProfilerFunctions::BeginFrameAt(uint32_t now_us, uint32_t now_ms){
    frame_no++;
    CrashDumpFunctions::BeginFrame(frame_no);
    CrashDumpFunctions::SetPhase((uint8_t)Section::Input);
    if(!enabled) return;

    if(in_frame){
        //前のフレームを閉じる(ここまでが1フレーム)
        const uint32_t frame_us = now_us - frame_start_us;
        if(!work_marked) frame_work_us = now_us - frame_start_us;
        //最後のMarkから今までは「その他」に入れず、仕事の時間にだけ含めない(IdleWaitの中)
        win_frames++;
        win_frame_us += frame_us;
        if(frame_us > win_frame_max_us) win_frame_max_us = frame_us;
        win_work_us += frame_work_us;
        for(int i = 0; i < (int)Section::Count; i++) win_section_us[i] += frame_section_us[i];
        win_lua_us += lua_us_in_frame;

        history[history_head] = frame_us;
        history_head = (history_head + 1) % kHistory;
        if(history_count < kHistory) history_count++;

        if(now_ms - window_start_ms >= kWindowMs) closeWindow(now_ms);
    }else{
        window_start_ms = now_ms;
    }

    in_frame = true;
    frame_start_us = now_us;
    last_mark_us = now_us;
    memset(frame_section_us, 0, sizeof(frame_section_us));
    frame_work_us = 0;
    work_marked = false;
    lua_us_in_frame = 0;
}

namespace {
    //Mark(s)は「sが終わった」印なので、今からやるのは次の区間(並びはloop()の順)
    void SetNextPhase(ProfilerFunctions::Section s){
        const int next = (int)s + 1;
        CrashDumpFunctions::SetPhase(next < (int)ProfilerFunctions::Section::Count
            ? (uint8_t)next : CrashDumpFunctions::kPhaseIdle);
    }
}

void ProfilerFunctions::MarkAt(Section s, uint32_t now_us){
    SetNextPhase(s);
    if(!enabled || !in_frame) return;
    const int i = (int)s;
    if(i < 0 || i >= (int)Section::Count) return;
    frame_section_us[i] += now_us - last_mark_us;
    last_mark_us = now_us;
}

void ProfilerFunctions::EndWorkAt(uint32_t now_us){
    if(!enabled || !in_frame) return;
    frame_work_us = now_us - frame_start_us;
    work_marked = true;
}

void ProfilerFunctions::BeginFrame(){ BeginFrameAt((uint32_t)micros(), (uint32_t)millis()); }
void ProfilerFunctions::Mark(Section s){
    if(!enabled){ SetNextPhase(s); return; }
    MarkAt(s, (uint32_t)micros());
}
void ProfilerFunctions::EndWork(){
    //ここから次のフレームの頭までは休み(PowerFunctions::IdleWait())
    CrashDumpFunctions::SetPhase(CrashDumpFunctions::kPhaseIdle);
    if(enabled) EndWorkAt((uint32_t)micros());
}

const ProfilerFunctions::Stats& ProfilerFunctions::Last(){ return last; }
uint32_t ProfilerFunctions::WindowSerial(){ return window_serial; }

int ProfilerFunctions::History(uint32_t* out, int max){
    if(!out || max <= 0) return 0;
    const int n = history_count < max ? history_count : max;
    //古い順に、最後のn件
    int start = history_head - n;
    while(start < 0) start += kHistory;
    for(int i = 0; i < n; i++) out[i] = history[(start + i) % kHistory];
    return n;
}

void ProfilerFunctions::LogReport(){
    const Stats& s = last;
    char buf[200];
    int n = snprintf(buf, sizeof(buf), "[PROF] %lu.%lufps frame %lu.%02lums(max %lu.%02lu) work %lu.%02lums lua %lu.%02lums |",
        (unsigned long)(s.fps_x10 / 10), (unsigned long)(s.fps_x10 % 10),
        (unsigned long)(s.frame_avg_us / 1000), (unsigned long)(s.frame_avg_us % 1000 / 10),
        (unsigned long)(s.frame_max_us / 1000), (unsigned long)(s.frame_max_us % 1000 / 10),
        (unsigned long)(s.work_avg_us / 1000), (unsigned long)(s.work_avg_us % 1000 / 10),
        (unsigned long)(s.lua_avg_us / 1000), (unsigned long)(s.lua_avg_us % 1000 / 10));
    for(int i = 0; i < (int)Section::Count && n > 0 && n < (int)sizeof(buf); i++){
        n += snprintf(buf + n, sizeof(buf) - n, " %s %lu.%02lu",
            kNames[i], (unsigned long)(s.section_avg_us[i] / 1000), (unsigned long)(s.section_avg_us[i] % 1000 / 10));
    }
    //SDのログへ毎回書くと重いのでシリアルにだけ出す
    Serial.printf("%s\n", buf);
}
