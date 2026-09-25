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

        constexpr static int MARGIN = 2;
        constexpr static int ICON_MARGIN_TOP = (STATUSBAR_HEIGHT - 16) / 2;

    public:
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