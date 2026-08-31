#pragma once

#include "gui/widgets/Widget.hpp"
#include "gui/icons/icon_render.h"

class Statusbar : public Widget {

    private:
        char HH_mm[6];

        unsigned long update_interval_time = 0;

        constexpr static int MARGIN = 2;
        constexpr static int ICON_MARGIN_TOP = (20 - 16) / 2;

    public:
        Statusbar(){
            this->l_rect = {
                0, 0,
                SCREEN_WIDTH,
                20
            };
            this->update_interval_time = millis();
        }

        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::OPAQUE; }
        void render() override;
};