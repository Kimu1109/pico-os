#pragma once

#include <cstdint>
#include "consts.hpp"

// 液晶・タッチ・SDの配線と設定。既定値は consts.hpp のピン定義そのもので、
// 内蔵フラッシュの末尾4KB(arduino-picoのEEPROMの領域)に置いた値で起動時に上書きする
// (読み書きは hal/HwConfig_Functions、文字の形との変換と検査は system/HwConfig_Codec)。
// 液晶の種類と画面の大きさはビルド時のまま(LGFX_Config.hpp の PicoPanel・SCREEN_WIDTH/HEIGHT)。
//
// ピンの -1 は「つながっていない」(使えるのは lcd_rst と lcd_bl だけ)。
// lcd_invert / lcd_bgr の -1 は「液晶の種類の既定のまま」。
struct HwConfig {
    // 液晶(SPI0。SCK/MOSI/MISOはタッチと共有)
    int8_t lcd_sck;
    int8_t lcd_mosi;
    int8_t lcd_miso;
    int8_t lcd_cs;
    int8_t lcd_dc;
    int8_t lcd_rst;
    int8_t lcd_bl;       // バックライト(PWM調光)
    uint8_t lcd_rotation; // LovyanGFXの offset_rotation(0〜7。縦横が入れ替わる奇数は使えない)
    int16_t lcd_offset_x;
    int16_t lcd_offset_y;
    int8_t lcd_invert;   // 色反転
    int8_t lcd_bgr;      // 赤と青の並び
    uint32_t lcd_hz;     // 書き込みのSPIの速さ
    // タッチ(XPT2046。液晶と同じSPI0)
    int8_t touch_cs;
    int8_t touch_irq;
    // SD(SPI1)
    int8_t sd_cs;
    int8_t sd_sck;
    int8_t sd_mosi;
    int8_t sd_miso;
    uint32_t sd_hz;
};

constexpr HwConfig HwConfigDefault(){
    return HwConfig{
        TFT_SCK, TFT_MOSI, TFT_MISO, TFT_CS, TFT_DC, TFT_RST, TFT_LED,
        0, 0, 0, -1, -1, TFT_MAX_SPEED,
        TOUCH_CS, TOUCH_IRQ,
        SD_CS, SD_SCK, SD_MOSI, SD_MISO, SD_MAX_SPEED_MHZ * 1000000u,
    };
}
