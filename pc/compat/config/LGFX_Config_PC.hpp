// PCビルド用のLGFX設定。
//
// 実機版(src/config/LGFX_Config.hpp)はILI9341+SPIを構成するが、PCでは
// LovyanGFXが同梱しているSDLパネル(lgfx::Panel_sdl)を使う。
// インクルードパスの先頭に pc/compat を置くことで、src/には手を入れずに差し替わる。
#pragma once

#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/sdl/Panel_sdl.hpp>
#include "consts.hpp"

// PCの画面上での拡大率。240x320は実寸だと小さいので既定で2倍にする
#ifndef PICOOS_PC_SCALE
    #define PICOOS_PC_SCALE 2
#endif

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_sdl _panel_instance;

public:
    LGFX(void) {
        auto cfg = _panel_instance.config();
        cfg.memory_width  = SCREEN_WIDTH;
        cfg.memory_height = SCREEN_HEIGHT;
        cfg.panel_width   = SCREEN_WIDTH;
        cfg.panel_height  = SCREEN_HEIGHT;
        cfg.offset_x = 0;
        cfg.offset_y = 0;
        _panel_instance.config(cfg);

        //ウィンドウの拡大率とタイトル
        _panel_instance.setScaling(PICOOS_PC_SCALE, PICOOS_PC_SCALE);
        _panel_instance.setWindowTitle("pico-os (PC)");

        setPanel(&_panel_instance);
    }
};
