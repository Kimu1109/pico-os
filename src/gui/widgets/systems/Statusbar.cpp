#include "gui/widgets/systems/Statusbar.hpp"
#include "gui/icons/icon_render.h"
#include "functions/Font_Functions.hpp"
#include "functions/Time_Functions.hpp"
#include "functions/Network_Functions.hpp"

#include "OS_Data.hpp"

void Statusbar::render(){
    if(TimeFunctions::changed_HH_mm || millis() - this->update_interval_time >= 5000){
        this->update_interval_time = millis();
        this->needsRender();
    }

    if(!this->needs_redraw) return;
    if(!this->visible) return;

    int draw_pos = 0;

    const Rect g_rect = this->getScreenRect();

    //時間(HH:mm)
    strftime(HH_mm, sizeof(HH_mm), "%H:%M", &TimeFunctions::timeinfo);

    OSData::frame->setCursor(g_rect.x, g_rect.y, FontFn::GetSmall());
    int HH_mm_w = OSData::frame->textWidth(HH_mm);
    OSData::frame->print(HH_mm);
    FontFn::SetNormal();
    draw_pos += HH_mm_w + MARGIN;

    //SDステート
    IconRender::DrawIcon(IconID::SdCard, IconSize::Px16, draw_pos, ICON_MARGIN_TOP, PICO_BLACK);
    if(!OSData::SD_usable){
        IconRender::DrawIcon(IconID::X, IconSize::Px16, draw_pos, ICON_MARGIN_TOP, PICO_RED);
    }
    draw_pos += 16 + MARGIN;

    //Wi-Fiステート
    IconRender::DrawIcon(NetworkFunctions::GetWifiStateIconID(), IconSize::Px16, draw_pos, ICON_MARGIN_TOP, PICO_BLACK);
    draw_pos += 16 + MARGIN;
    
    OSData::frame->drawFastHLine(g_rect.x, g_rect.y + g_rect.h - 1, g_rect.w, PICO_BLACK);

    this->needs_redraw = false;
}