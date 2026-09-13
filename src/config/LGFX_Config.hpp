#pragma once

// パネル構成の選択。
// PCビルド(pc/CMakeLists.txt)ではPICOOS_PCが定義され、SDLパネル版を使う。
// 山かっこincludeにしているのは、"..."だとこのファイルのあるディレクトリが
// 優先され、pc/compat側の実装が拾われないため。
#if defined(PICOOS_PC)
    #include <config/LGFX_Config_PC.hpp>
#else

#include <LovyanGFX.hpp>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include "consts.hpp"

// --- 液晶用 LGFX（SPI0） ---
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel_instance;
  lgfx::Bus_SPI       _bus_instance;
public:
  LGFX(void) {
    auto cfg = _bus_instance.config();
    cfg.spi_host = 0;      // SPI0
    cfg.pin_sclk = TFT_SCK;
    cfg.pin_mosi = TFT_MOSI;
    cfg.pin_miso = TFT_MISO;
    cfg.pin_dc   = TFT_DC;
    cfg.freq_write = TFT_MAX_SPEED; // 描画(書き込み)速度：ILI9341は40MHz程度まで安定
    _bus_instance.config(cfg);
    _panel_instance.setBus(&_bus_instance);

    auto pcfg = _panel_instance.config();
    pcfg.pin_cs  = TFT_CS;
    pcfg.pin_rst = TFT_RST;
    pcfg.panel_width  = SCREEN_WIDTH;
    pcfg.panel_height = SCREEN_HEIGHT;
    pcfg.bus_shared = true;
    _panel_instance.config(pcfg);

    setPanel(&_panel_instance);
  }
};

#endif // PICOOS_PC
