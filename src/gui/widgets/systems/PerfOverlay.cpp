#include "gui/widgets/systems/PerfOverlay.hpp"
#include "functions/Profiler_Functions.hpp"
#include "functions/Font_Functions.hpp"
#include "OS_Data.hpp"

#include <stdio.h>

namespace {
    //マイクロ秒を "12.3" の形に(ミリ秒、小数1桁)
    void Ms1(char* out, size_t size, uint32_t us){
        snprintf(out, size, "%lu.%lu", (unsigned long)(us / 1000), (unsigned long)(us % 1000 / 100));
    }
}

void PerfOverlay::render(){
    if(!this->visible) return;

    const uint32_t serial = ProfilerFunctions::WindowSerial();
    if(serial != this->shown_serial){
        this->shown_serial = serial;
        this->needsRender();
    }
    if(!this->needs_redraw) return;

    const Rect g = this->getScreenRect();
    LGFX_Sprite* f = OSData::frame;
    f->fillRect(g.x, g.y, g.w, g.h, PICO_BLACK);

    const ProfilerFunctions::Stats& s = ProfilerFunctions::Last();
    using Sec = ProfilerFunctions::Section;
    const uint32_t upd = s.section_avg_us[(int)Sec::Widgets] + s.section_avg_us[(int)Sec::Scene];
    const uint32_t drw = s.section_avg_us[(int)Sec::Flush];
    const uint32_t etc = s.work_avg_us > upd + drw ? s.work_avg_us - upd - drw : 0;

    char a[12], b[12], c[12], line[64];
    f->setFont(&fonts::Font0);
    f->setTextSize(1);

    Ms1(a, sizeof(a), s.frame_avg_us);
    snprintf(line, sizeof(line), "%sms %lu.%lufps", a, (unsigned long)(s.fps_x10 / 10), (unsigned long)(s.fps_x10 % 10));
    f->setTextColor(s.frame_avg_us <= 16700 ? PICO_GREEN : (s.frame_avg_us <= 33300 ? PICO_YELLOW : PICO_RED));
    f->drawString(line, g.x + 2, g.y + 2);

    Ms1(a, sizeof(a), s.frame_max_us);
    Ms1(b, sizeof(b), s.lua_avg_us);
    snprintf(line, sizeof(line), "max %s lua %s", a, b);
    f->setTextColor(PICO_WHITE);
    f->drawString(line, g.x + 2, g.y + 11);

    Ms1(a, sizeof(a), upd);
    Ms1(b, sizeof(b), drw);
    Ms1(c, sizeof(c), etc);
    snprintf(line, sizeof(line), "upd%s drw%s etc%s", a, b, c);
    f->setTextColor(PICO_LIGHTGREY);
    f->drawString(line, g.x + 2, g.y + 20);

    //グラフ: 1フレーム=1本(幅は60本で揃える)。33.3msを上限に頭打ち、16.7msの位置に目盛り
    const int gx = g.x + 2, gy = g.y + 30, gw = g.w - 4, gh = g.h - 32;
    uint32_t hist[ProfilerFunctions::kHistory];
    const int n = ProfilerFunctions::History(hist, ProfilerFunctions::kHistory);
    const int bar_w = gw / ProfilerFunctions::kHistory > 0 ? gw / ProfilerFunctions::kHistory : 1;
    const int x0 = gx + gw - n * bar_w;
    for(int i = 0; i < n; i++){
        const uint32_t us = hist[i];
        int h = (int)((uint64_t)us * gh / 33333);
        if(h > gh) h = gh;
        if(h < 1) h = 1;
        const uint8_t col = us <= 16700 ? PICO_GREEN : (us <= 33300 ? PICO_YELLOW : PICO_RED);
        f->fillRect(x0 + i * bar_w, gy + gh - h, bar_w, h, col);
    }
    f->drawFastHLine(gx, gy + gh - gh / 2, gw, PICO_DARKGREY);

    FontFn::SetNormal();
    this->needs_redraw = false;
}
