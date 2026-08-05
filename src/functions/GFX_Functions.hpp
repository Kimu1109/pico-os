#pragma once

#include "config/LGFX_Config.hpp"
#include "OS_Data.hpp"
#include <SPI.h>

namespace PICO_GFX {
    void markDirty(int x, int y, int w, int h);
    const int COLORS[16] = {
        TFT_BLACK,
        TFT_NAVY,
        TFT_DARKGREEN,
        TFT_DARKCYAN,
        TFT_MAROON,
        TFT_PURPLE,
        TFT_OLIVE,
        TFT_LIGHTGREY,
        TFT_DARKGREY,
        TFT_BLUE,
        TFT_GREEN,
        TFT_CYAN,
        TFT_RED,
        TFT_MAGENTA,
        TFT_YELLOW,
        TFT_WHITE
    };
    int16_t dirty_x1, dirty_y1, dirty_x2, dirty_y2;
    bool has_dirty = false;

    void Setup(){
        OSData::lcd->init();
        OSData::lcd->setBaseColor(TFT_WHITE);
        OSData::lcd->clear(TFT_WHITE);
        OSData::lcd->setFont(&lgfxJapanGothicP_24);
        OSData::lcd->setTextColor(TFT_BLACK);

        LGFX_Sprite* frame = new LGFX_Sprite(OSData::lcd);
        frame->setColorDepth(4);
        frame->createSprite(SCREEN_WIDTH, SCREEN_HEIGHT);
        for(int i = 0; i < 16; i++){
            frame->setPaletteColor(i, COLORS[i]);
        }
        frame->setBaseColor(PICO_WHITE);
        frame->clear(PICO_WHITE);
        frame->setFont(&lgfxJapanGothicP_24);
        frame->setTextColor(PICO_BLACK);
        OSData::frame = frame;
        markDirty(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

        pinMode(22, OUTPUT); //LED ON
        digitalWrite(22, HIGH);
    }

    void markDirty(int x, int y, int w, int h) {
        x = max(0, x);
        y = max(0, y);
        w = min(w, SCREEN_WIDTH - x);
        h = min(h, SCREEN_HEIGHT - y);
        if (w <= 0 || h <= 0) return; // 不正な範囲は無視

        if (!has_dirty) {
            dirty_x1 = x; dirty_y1 = y;
            dirty_x2 = x + w; dirty_y2 = y + h;
            has_dirty = true;
        } else {
            dirty_x1 = min(dirty_x1, x);
            dirty_y1 = min(dirty_y1, y);
            dirty_x2 = max(dirty_x2, x + w);
            dirty_y2 = max(dirty_y2, y + h);
        }
    }

    void flushDirty() {
        if (!has_dirty) return;

        // 画面外にはみ出さないようクリップ
        dirty_x1 = max(dirty_x1, 0);
        dirty_y1 = max(dirty_y1, 0);
        dirty_x2 = min(dirty_x2, SCREEN_WIDTH);
        dirty_y2 = min(dirty_y2, SCREEN_HEIGHT);

        int w = dirty_x2 - dirty_x1;
        int h = dirty_y2 - dirty_y1;

        // 転送範囲を限定してpush(ここが差分描画の肝)
        OSData::frame->setClipRect(dirty_x1, dirty_y1, w, h);
        OSData::frame->pushSprite(OSData::lcd, 0, 0);
        OSData::frame->clearClipRect(); // 次回のフル描画用にクリップ解除

        digitalWrite(TFT_CS, HIGH);

        has_dirty = false;
    }
}