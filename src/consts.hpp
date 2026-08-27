#pragma once

#define PICO_BACKGROUND  15
#define PICO_FORECOLOR   0

#define PICO_BLACK      0
#define PICO_NAVY       1
#define PICO_DARKGREEN  2
#define PICO_DARKCYAN   3
#define PICO_MAROON     4
#define PICO_PURPLE     5
#define PICO_OLIVE      6
#define PICO_LIGHTGREY  7
#define PICO_DARKGREY   8
#define PICO_BLUE       9
#define PICO_GREEN      10
#define PICO_CYAN       11
#define PICO_RED        12
#define PICO_MAGENTA    13
#define PICO_YELLOW     14
#define PICO_WHITE      15

#define PICO_SCROLL_EX  1.30f

// --- スクリーンサイズ ---
#define SCREEN_WIDTH    240
#define SCREEN_HEIGHT   320

// --- SPI0: LCD + タッチ 共有 ---
#define TFT_SCK   18
#define TFT_MOSI  19
#define TFT_MISO  16
#define TFT_CS    17
#define TFT_DC    20
#define TFT_RST   21
#define TOUCH_SCK  18
#define TOUCH_MOSI 19
#define TOUCH_MISO 16
#define TOUCH_CS   13
#define TOUCH_IRQ  9
#define TFT_MAX_SPEED   80000000

// --- SPI1: SD専用 ---
#define SD_CS     15
#define SD_SCK    10
#define SD_MOSI   11
#define SD_MISO   12
#define SD_MAX_SPEED_MHZ    10

// --- バックライト ---
#define TFT_LED    22