#include "gui/widgets/systems/Statusbar.hpp"
#include "gui/icons/icon_render.h"
#include "functions/Font_Functions.hpp"
#include "functions/Time_Functions.hpp"
#include "functions/Network_Functions.hpp"
#include "functions/Sound_Functions.hpp"
#include "functions/Pad_Functions.hpp"

#include "OS_Data.hpp"

void Statusbar::render(){
    if(TimeFunctions::changed_HH_mm || millis() - this->update_interval_time >= 5000){
        this->update_interval_time = millis();
        this->needsRender();
    }
    const uint8_t sound_state = (uint8_t)SoundFunctions::GetState();
    if(sound_state != this->last_sound_state){
        this->last_sound_state = sound_state;
        this->needsRender();
    }

    const bool pad_connected = PadFunctions::IsConnected();
    if(pad_connected != this->last_pad_connected){
        this->last_pad_connected = pad_connected;
        this->needsRender();
    }

    if(!this->needs_redraw) return;
    if(!this->visible) return;

    int draw_pos = 0;

    const Rect g_rect = this->getScreenRect();

    //時間(HH:mm)
    strftime(HH_mm, sizeof(HH_mm), "%H:%M", &TimeFunctions::timeinfo);

    OSData::frame->setCursor(g_rect.x, g_rect.y, FontFn::GetSmall());
    OSData::frame->setTextColor(PICO_BLACK);
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
    //圏外は「最弱の棒 + バツ」で表す(SDカードと同じ組み立て方)
    IconRender::DrawIcon(NetworkFunctions::GetWifiStateIconID(), IconSize::Px16, draw_pos, ICON_MARGIN_TOP, PICO_BLACK);
    if(!NetworkFunctions::IsConnected()){
        IconRender::DrawIcon(IconID::X, IconSize::Px16, draw_pos, ICON_MARGIN_TOP, PICO_RED);
    }
    draw_pos += 16 + MARGIN;

    //音声出力
    //未接続は「スピーカー + バツ」(SD/Wi-Fiと同じ組み立て方)、output=offは消音のスピーカー
    switch((SoundFunctions::State)sound_state){
        case SoundFunctions::State::Active:
            IconRender::DrawIcon(IconID::VolumeHigh, IconSize::Px16, draw_pos, ICON_MARGIN_TOP, PICO_BLACK);
            break;
        case SoundFunctions::State::Muted:
            IconRender::DrawIcon(IconID::VolumeOff, IconSize::Px16, draw_pos, ICON_MARGIN_TOP, PICO_BLACK);
            break;
        case SoundFunctions::State::Disconnected:
            IconRender::DrawIcon(IconID::VolumeHigh, IconSize::Px16, draw_pos, ICON_MARGIN_TOP, PICO_BLACK);
            IconRender::DrawIcon(IconID::X, IconSize::Px16, draw_pos, ICON_MARGIN_TOP, PICO_RED);
            break;
    }
    draw_pos += 16 + MARGIN;

    //外部コントローラー: つながっているときだけ出す(無いのが普通なのでバツは付けない)
    if(pad_connected){
        IconRender::DrawIcon(IconID::Game, IconSize::Px16, draw_pos, ICON_MARGIN_TOP, PICO_BLACK);
        draw_pos += 16 + MARGIN;
    }

    OSData::frame->drawFastHLine(g_rect.x, g_rect.y + g_rect.h - 1, g_rect.w, PICO_BLACK);

    this->needs_redraw = false;
}