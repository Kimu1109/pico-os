#pragma once

#include "gui/widgets/Widget.hpp"
#include "gui/icons/icon_render.h"

class Statusbar : public Widget {

    private:
        char HH_mm[6];

        unsigned long update_interval_time = 0;
        //音声出力の状態(SoundFunctions::State)。変わったフレームで描き直す
        uint8_t last_sound_state = 0xFF;
        bool last_pad_connected = false;
        //未読の通知の数。変わったフレームで描き直す
        int last_unread = -1;
        uint8_t last_wifi_status = 0xFF;
        // 5秒ごとに見直す、変化の少ない表示(変わったときだけ描き直す)
        int16_t last_wifi_icon = -1;    // 電波の段階(IconID)
        int16_t last_battery_icon = -1; // 電池の段階(IconID)。読めていなければ-2
        int8_t  last_sd_usable = -1;

        constexpr static int MARGIN = 2;
        constexpr static int ICON_MARGIN_TOP = (STATUSBAR_HEIGHT - 16) / 2;

    public:
        // 時刻・電波・電池・音・未読の変化をrender()で見張る
        bool wantsFrameUpdate() const override { return true; }

        Statusbar(){
            this->l_rect = {
                0, 0,
                SCREEN_WIDTH,
                STATUSBAR_HEIGHT
            };
            this->update_interval_time = millis();
        }

        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::OPAQUE; }
        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::Statusbar; }
};