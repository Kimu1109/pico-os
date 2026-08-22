#pragma once

#include "widgets/Widget.hpp"
#include "icons/icons_data.h"
#include "icons/icon_render.h"
#include "consts.hpp"

class Icon : public Widget {
    private:
        IconID iconId;
        IconSize iconSize;
        int8_t color = PICO_BLACK;
        bool opaque = false;

    public:

        Icon(int16_t x, int16_t y, IconID iconId, IconSize iconSize){
            const int16_t size_px = IconRender::IconPixelSize(iconSize);
            this->l_rect = {
                x, y,
                size_px, size_px
            };

            this->iconId = iconId;
            this->iconSize = iconSize;
            this->color = PICO_BLACK;

            this->prev_l_rect.copy(this->l_rect);
        }

        void render() override;

        WidgetTools::RenderMode GetRenderMode() const override { return this->opaque ? WidgetTools::OPAQUE : WidgetTools::CLEAR; };
        void setOpaque(bool opaque) { 
            this->opaque = opaque;
            this->needsRender();
        }

        IconID GetIconId() { return this->iconId; }
        void SetIconId(IconID iconId) {
            this->iconId = iconId;
            this->needsRender();
        }

        IconSize GetIconSize() { return this->iconSize; }
        void SetIconSize(IconSize iconSize){
            this->iconSize = iconSize;

            this->l_rect.w = IconRender::IconPixelSize(iconSize);
            this->l_rect.h = this->l_rect.w;

            this->needsRender();
        }

        int8_t Color() { return this->color; }
        void Color(int8_t color){
            this->color = color;
            this->needsRender();
        }
};