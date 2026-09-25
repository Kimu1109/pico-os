#pragma once

#define PICO_STR_S      24
#define PICO_STR_M      48
#define PICO_STR_L      96
#define PICO_STR_LL     192
#define PICO_STR_256B   256
#define PICO_STR_512B   512
#define PICO_STR_1KiB   1024
#define PICO_STR_2KiB   2048
#define PICO_STR_4KiB   4096
#define PICO_STR_8KiB   8192
#define PICO_STR_16KiB  16384
#define PICO_STR_32KiB  32768

#define PICO_PATH_LEN   255

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

// --- 常駐UI(オーバーレイ)のサイズ ---
// シーンが使える領域はステータスバーの下から画面下端まで(Scene::contentRect()参照)
#define STATUSBAR_HEIGHT 20

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

// --- 音声出力: I2S(MAX98357A) ---
// arduino-picoのI2SはLRCLKをBCLK+1に固定するので、この2本は必ず隣り合う番号にする。
// GP14/15はGP15がSDのCSなので使えない。ADCの使えるGP26〜28は外部コントローラー用に残す
#define AUDIO_I2S_BCLK  2
#define AUDIO_I2S_LRCLK 3   // = AUDIO_I2S_BCLK + 1(ソフトからは指定しない。配線の控え)
#define AUDIO_I2S_DATA  4
// アンプの有無の検出。内部プルアップで読み、アンプ側でGNDへ落としておけば
// 「LOW=接続」「HIGH=未接続」になる(I2Sは一方通行なので、信号線からは分からない)
#define AUDIO_DETECT    5
// MAX98357AのSD(休止)端子。LOWで休止、HIGHで左チャンネル(こちらは常にL=Rで送る)。
// つながなければ基板の既定((L+R)/2)で鳴るので、配線は任意
#define AUDIO_SHUTDOWN  6
