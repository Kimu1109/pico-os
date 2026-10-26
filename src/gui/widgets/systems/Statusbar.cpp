#include "gui/widgets/systems/Statusbar.hpp"
#include "gui/icons/icon_render.h"
#include "gui/core/Font_Functions.hpp"
#include "system/Time_Functions.hpp"
#include "hal/Network_Functions.hpp"
#include "hal/Sound_Functions.hpp"
#include "hal/Battery_Functions.hpp"
#include "system/Pad_Functions.hpp"
#include "system/Notification_Functions.hpp"
#include "services/Screenshot_Functions.hpp"
#include "system/Background_Functions.hpp"
#include "config/Hardware_Config.hpp"

#include "gui/core/GFX_Functions.hpp"
#include "OS_Data.hpp"

namespace {
    //残量(%)→4段階のアイコン
    IconID BatteryIcon(int pct){
        if(pct >= 75) return IconID::Battery4;
        if(pct >= 50) return IconID::Battery3;
        if(pct >= 25) return IconID::Battery2;
        if(pct >= 10) return IconID::Battery1;
        return IconID::Battery0;
    }
}

void Statusbar::render(){
    //変化の確認は合成の外(UpdateAll()から)の回だけ。合成の中(FlushDirty())ではMarkDirty()が効かないので、
    //そこで変化を見つけて覚えてしまうと描き直しの要求が失われる
    if(!PICO_GFX::isDirtyDeactivates){
        //時刻は分が変わったときだけ。電波・電池・SDは1秒ごと(電池の読み取り間隔に合わせる)に見て、
        //段階が変わったときだけ描き直す(以前は変化が無くても5秒ごとに全体を描き直していた)
        if(TimeFunctions::changed_HH_mm) this->needsRender();
        if(millis() - this->update_interval_time >= 1000){
            this->update_interval_time = millis();
            const int16_t wifi_icon = (int16_t)NetworkFunctions::GetWifiStateIconID();
            const int16_t battery_icon = BatteryFunctions::HasSample()
                ? (int16_t)(BatteryFunctions::IsExternallyPowered() ? IconID::BatteryCharging : BatteryIcon(BatteryFunctions::GetPercent())) : -2;
            const int8_t sd_usable = OSData::SD_usable ? 1 : 0;
            if(wifi_icon != this->last_wifi_icon || battery_icon != this->last_battery_icon || sd_usable != this->last_sd_usable){
                this->last_wifi_icon = wifi_icon;
                this->last_battery_icon = battery_icon;
                this->last_sd_usable = sd_usable;
                this->needsRender();
            }
        }
        const uint8_t sound_state = (uint8_t)((uint8_t)SoundFunctions::GetState() | ((uint8_t)SoundFunctions::GetRoute() << 4));
        if(sound_state != this->last_sound_state){
            this->last_sound_state = sound_state;
            this->needsRender();
        }

        const bool pad_connected = PadFunctions::IsConnected();
        if(pad_connected != this->last_pad_connected){
            this->last_pad_connected = pad_connected;
            this->needsRender();
        }

        //Wi-Fiの接続/切断/OFFは5秒の定期更新を待たずに出す
        const uint8_t wifi_status = (uint8_t)NetworkFunctions::currentStatus;
        if(wifi_status != this->last_wifi_status){
            this->last_wifi_status = wifi_status;
            this->needsRender();
        }

        const int16_t bg_icon = (int16_t)BackgroundFunctions::IndicatorIcon();
        if(bg_icon != this->last_bg_icon){
            this->last_bg_icon = bg_icon;
            this->needsRender();
        }

        const int unread = NotificationFunctions::UnreadCount();
        if(unread != this->last_unread){
            this->last_unread = unread;
            this->needsRender();
        }
    }

    if(!this->needs_redraw) return;
    if(!this->visible) return;
    //合成の外の回は描いても捨てられる(dirtyはneedsRender()で積んである)
    if(PICO_GFX::render_suppressed){
        this->needs_redraw = false;
        return;
    }

    const uint8_t sound_state = this->last_sound_state;
    const bool pad_connected = this->last_pad_connected;
    const int unread = this->last_unread;

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
    //圏外は「最弱の棒 + バツ」で表す(SDカードと同じ組み立て方)。OFFは満タンの扇を薄い灰色で(バツは付けない)
    if(NetworkFunctions::currentStatus == NetworkFunctions::NetStatus::OFF){
        IconRender::DrawIcon(IconID::WifiSignal4, IconSize::Px16, draw_pos, ICON_MARGIN_TOP, PICO_LIGHTGREY);
    }else{
        IconRender::DrawIcon(NetworkFunctions::GetWifiStateIconID(), IconSize::Px16, draw_pos, ICON_MARGIN_TOP, PICO_BLACK);
    }
    if(!NetworkFunctions::IsConnected() && NetworkFunctions::currentStatus != NetworkFunctions::NetStatus::OFF){
        IconRender::DrawIcon(IconID::X, IconSize::Px16, draw_pos, ICON_MARGIN_TOP, PICO_RED);
    }
    draw_pos += 16 + MARGIN;

    //音声出力
    //未接続は「鳴らす先の絵 + バツ」(SD/Wi-Fiと同じ組み立て方)、output=offは消音のスピーカー。
    //鳴らす先がイヤホンならスピーカーの代わりにイヤホンの絵。音声出力を載せない版は何も出さない
    if constexpr (HardwareConfig::kSound){
        const IconID out_icon = ((SoundFunctions::Route)(sound_state >> 4) == SoundFunctions::Route::Headphone)
            ? IconID::Headphones : IconID::VolumeHigh;
        switch((SoundFunctions::State)(sound_state & 0x0F)){
            case SoundFunctions::State::Active:
                IconRender::DrawIcon(out_icon, IconSize::Px16, draw_pos, ICON_MARGIN_TOP, PICO_BLACK);
                break;
            case SoundFunctions::State::Muted:
                IconRender::DrawIcon(IconID::VolumeOff, IconSize::Px16, draw_pos, ICON_MARGIN_TOP, PICO_BLACK);
                break;
            case SoundFunctions::State::Disconnected:
                IconRender::DrawIcon(out_icon, IconSize::Px16, draw_pos, ICON_MARGIN_TOP, PICO_BLACK);
                IconRender::DrawIcon(IconID::X, IconSize::Px16, draw_pos, ICON_MARGIN_TOP, PICO_RED);
                break;
        }
        draw_pos += 16 + MARGIN;
    }

    //バッテリー: 残量に応じた4段階。USB給電中は稲妻入りのアイコン(tablerの battery-charging)に差し替える
    //(まだ1回もVSYS読み取りが済んでいなければ何も描かない。通常はSetup()で即座に読むので起動直後の1フレーム目だけの話)
    if(BatteryFunctions::HasSample()){
        const IconID battery_icon = BatteryFunctions::IsExternallyPowered() ? IconID::BatteryCharging : BatteryIcon(BatteryFunctions::GetPercent());

        IconRender::DrawIcon(battery_icon, IconSize::Px16, draw_pos, ICON_MARGIN_TOP, PICO_BLACK);
        draw_pos += 16 + MARGIN;
    }

    //外部コントローラー: つながっているときだけ出す(無いのが普通なのでバツは付けない)
    if(pad_connected){
        IconRender::DrawIcon(IconID::Game, IconSize::Px16, draw_pos, ICON_MARGIN_TOP, PICO_BLACK);
        draw_pos += 16 + MARGIN;
    }

    //バックグラウンドの仕事: 何か動いている間だけ青で出す(ミュージックが鳴っていれば音符、定期の仕事だけなら回る矢印)。
    //止めるのは設定アプリの「他」→「バックグラウンド」
    if(this->last_bg_icon >= 0 && this->last_bg_icon < (int16_t)IconID::IconCount){
        IconRender::DrawIcon((IconID)this->last_bg_icon, IconSize::Px16, draw_pos, ICON_MARGIN_TOP, PICO_BLUE);
        draw_pos += 16 + MARGIN;
    }

    //スクリーンショット: 右端のカメラ(タップで撮る。範囲はScreenshotFunctions::kButtonWidth)
    IconRender::DrawIcon(IconID::Camera, IconSize::Px16, g_rect.x + g_rect.w - MARGIN - 16, ICON_MARGIN_TOP, PICO_BLACK);

    //未読の通知: カメラの左にベルと数(タップで通知センター)。無ければ何も出さない
    if(unread > 0){
        char num[4];
        snprintf(num, sizeof(num), "%d", unread > 99 ? 99 : unread);
        OSData::frame->setFont(FontFn::GetSmall());
        const int num_w = OSData::frame->textWidth(num);
        const int bell_x = g_rect.x + g_rect.w - (MARGIN + 16) - MARGIN - num_w - 1 - 16;
        IconRender::DrawIcon(IconID::Bell, IconSize::Px16, bell_x, ICON_MARGIN_TOP, PICO_RED);
        OSData::frame->setTextColor(PICO_RED);
        OSData::frame->setCursor(bell_x + 16 + 1, g_rect.y);
        OSData::frame->print(num);
        FontFn::SetNormal();
    }

    OSData::frame->drawFastHLine(g_rect.x, g_rect.y + g_rect.h - 1, g_rect.w, PICO_BLACK);

    this->needs_redraw = false;
}