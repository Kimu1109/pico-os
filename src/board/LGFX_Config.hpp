#pragma once

// パネル構成の選択。
// PCビルド(pc/CMakeLists.txt)ではPICOOS_PCが定義され、SDLパネル版を使う。
// 山かっこincludeにしているのは、"..."だとこのファイルのあるディレクトリが
// 優先され、pc/compat側の実装が拾われないため。
#if defined(PICOOS_PC)
    #include <board/LGFX_Config_PC.hpp>
#else

#include <LovyanGFX.hpp>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <hardware/dma.h>
#include "consts.hpp"
#include "board/HwConfig.hpp"

// --- 液晶の種類 ---
// buildcfg/ の版で -DPICO_PANEL_ST7796 等を足すと切り替わる(既定はILI9341。部品は buildcfg/options.ini の opt_screen_*)。
// 画面の大きさ(SCREEN_WIDTH/HEIGHT、consts.hpp)はパネルの縦向きの大きさと合わせること
// (setRotation()はしていない)。タッチはXPT2046のまま(Touch_Functions.hpp)。
#if defined(PICO_PANEL_ST7796)
  using PicoPanel = lgfx::Panel_ST7796;   // 320x480
#elif defined(PICO_PANEL_ILI9488)
  using PicoPanel = lgfx::Panel_ILI9488;  // 320x480
#elif defined(PICO_PANEL_ST7789)
  using PicoPanel = lgfx::Panel_ST7789;   // 240x320 / 240x240
#else
  using PicoPanel = lgfx::Panel_ILI9341;  // 240x320
#endif

// --- 液晶用 LGFX（SPI0） ---
// ピン・速さ・向き・ずれ・色反転は起動時に initWith(HwConfig) で決める(board/HwConfig.hpp。既定は consts.hpp、
// 内蔵フラッシュの値で上書きされる。hal/HwConfig_Functions)。作るだけでは何も触らない。
class LGFX : public lgfx::LGFX_Device {
  PicoPanel           _panel_instance;
  lgfx::Bus_SPI       _bus_instance;
  lgfx::Light_PWM     _light_instance; // バックライトのPWM調光
  HwConfig            _hw = HwConfigDefault();
  bool                _begun = false;
  int                 _dma_ch = -1;    // 前の init() でBus_SPIが取ったDMAのチャンネル

  static uint32_t claimedDmaMask(void) {
    uint32_t m = 0;
    for (uint i = 0; i < NUM_DMA_CHANNELS; i++) if (dma_channel_is_claimed(i)) m |= 1u << i;
    return m;
  }

public:
  LGFX(void) {
    setPanel(&_panel_instance);
  }

  // 設定を当てて初期化する。何度呼んでもよい(液晶が応答しないとき既定のピンで試し直すため)。
  // init() の結果を返すが、SPIの液晶はつながっていなくても真になるので、確かめるのは probe()。
  bool initWith(const HwConfig& c) {
    if (_begun) {
      // 前の設定の後始末。Bus_SPIは init() のたびにDMAのチャンネルを取って返さないので、ここで返す
      _bus_instance.release();
      if (_dma_ch >= 0) { dma_channel_unclaim(_dma_ch); _dma_ch = -1; }
      // 前の設定だけで使っていたピンは入力へ戻す(出力のままのCS等がほかの部品を選ばないように)
      const int8_t old_pins[] = { _hw.lcd_sck, _hw.lcd_mosi, _hw.lcd_miso, _hw.lcd_cs, _hw.lcd_dc, _hw.lcd_rst, _hw.lcd_bl };
      const int8_t new_pins[] = { c.lcd_sck, c.lcd_mosi, c.lcd_miso, c.lcd_cs, c.lcd_dc, c.lcd_rst, c.lcd_bl };
      for (int8_t op : old_pins) {
        if (op < 0) continue;
        bool reused = false;
        for (int8_t np : new_pins) if (np == op) reused = true;
        if (!reused) pinMode(op, INPUT);
      }
    }
    _hw = c;

    auto cfg = _bus_instance.config();
    cfg.spi_host = 0;      // SPI0(ピンはSPI0の端子だけ。HwConfigCodec::Validate()が確かめる)
    cfg.pin_sclk = c.lcd_sck;
    cfg.pin_mosi = c.lcd_mosi;
    cfg.pin_miso = c.lcd_miso;
    cfg.pin_dc   = c.lcd_dc;
    cfg.freq_write = c.lcd_hz; // 描画(書き込み)速度：ILI9341は40MHz程度まで安定
    _bus_instance.config(cfg);
    _panel_instance.setBus(&_bus_instance);

    auto pcfg = _panel_instance.config();
    pcfg.pin_cs  = c.lcd_cs;
    pcfg.pin_rst = c.lcd_rst;
    pcfg.panel_width  = SCREEN_WIDTH;
    pcfg.panel_height = SCREEN_HEIGHT;
    pcfg.offset_x = c.lcd_offset_x;
    pcfg.offset_y = c.lcd_offset_y;
    pcfg.offset_rotation = c.lcd_rotation;
    if (c.lcd_invert >= 0) pcfg.invert = c.lcd_invert != 0;
    if (c.lcd_bgr >= 0) pcfg.rgb_order = c.lcd_bgr != 0;
    pcfg.bus_shared = true;
    _panel_instance.config(pcfg);

    // バックライトのPWM調光。明るさは0〜100(百分率)がそのままデューティ比になる
    // (LovyanGFXのrp2040実装はPWMのwrapを100に固定している。PICO_GFX::SetBrightness()参照)。
    // チャンネルは偶数ピン=A=0、奇数ピン=B=1(既定のGP22はPWMスライス3のチャンネルA)。
    if (c.lcd_bl >= 0) {
      auto lcfg = _light_instance.config();
      lcfg.pin_bl = c.lcd_bl;
      lcfg.pwm_channel = c.lcd_bl & 1;
      lcfg.invert = false;
      _light_instance.config(lcfg);
      _panel_instance.setLight(&_light_instance);
    } else {
      _panel_instance.setLight(nullptr);
    }

    const uint32_t before = claimedDmaMask();
    const bool ok = init();
    const uint32_t added = claimedDmaMask() & ~before;
    for (uint i = 0; i < NUM_DMA_CHANNELS; i++) if (added & (1u << i)) { _dma_ch = (int)i; break; }
    _begun = true;
    return ok;
  }

  // 液晶が応答するか。RDDPM(0x0A、電源の状態)を読み、スリープ解除(bit4)と表示オン(bit2)が立っていれば応答あり。
  // 読み出しの線(MISO)が無い・配線が違うと 0x00 か 0xFF(浮いていれば揺れる)になるので、2回読んで揃うかも見る。
  // ILI9341/ST7789/ST7796/ILI9488 はどれもこの命令とビットの並びが同じ。ただしSPIで8ビットの読み出しの前に
  // ダミーの1クロックが要るかは液晶ごとに違い、LovyanGFXは1クロック飛ばす(dummy_read_bits)ので、
  // 1ビット左右へずれて読めた場合も応答ありとみなす(実機では未確認。docs/design/devtools.md)。
  bool probe(void) {
    const uint32_t a = _panel_instance.readCommand(0x0A, 0, 1) & 0xFF;
    const uint32_t b = _panel_instance.readCommand(0x0A, 0, 1) & 0xFF;
    if (a != b || a == 0x00 || a == 0xFF) return false;
    return (a & 0x14) == 0x14 || (a & 0x28) == 0x28 || (a & 0x0A) == 0x0A;
  }
};

#endif // PICOOS_PC
