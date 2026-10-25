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
// ビルド時に -DSCREEN_WIDTH=... -DSCREEN_HEIGHT=... で変えられる(platformio.ini の env、
// PCビルドは cmake -DPICOOS_SCREEN_W/H)。画面の配置は Scene::contentRect() 等からの相対で決めること。
// 部品の大きさ(文字・ボタン)は変わらないので、幅は240以上・高さは240以上を想定している。
#ifndef SCREEN_WIDTH
    #define SCREEN_WIDTH    240
#endif
#ifndef SCREEN_HEIGHT
    #define SCREEN_HEIGHT   320
#endif
static_assert(SCREEN_WIDTH >= 240 && SCREEN_HEIGHT >= 240, "画面は幅・高さとも240以上を想定している");
static_assert(SCREEN_WIDTH % 2 == 0, "4bppのframeを1行バイト単位で扱う所があるので幅は偶数");

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
// 液晶のSPIクロック。LovyanGFX(rp2040)は clk_peri(150MHz)/2 = 75MHz を整数(1+SCR)で割るので、
// 実際に選べるのは 75 / 37.5 / 25MHz … だけ。37.5〜74.99MHzを指定するとすべて37.5MHzになる
// (60000000も37.5MHz。全画面1枚の転送が約33ms)。75MHzにするには75000000以上を指定する(約16ms)。
// docs/design/performance.md「液晶への転送を減らす」参照
#define TFT_MAX_SPEED   75000000

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
// GP14/15はGP15がSDのCSなので使えない(GP26〜28はマイクの入力に使った。下)
#define AUDIO_I2S_BCLK  2
#define AUDIO_I2S_LRCLK 3   // = AUDIO_I2S_BCLK + 1(ソフトからは指定しない。配線の控え)
#define AUDIO_I2S_DATA  4
// アンプの有無の検出。内部プルアップで読み、アンプ側でGNDへ落としておけば
// 「LOW=接続」「HIGH=未接続」になる(I2Sは一方通行なので、信号線からは分からない)
#define AUDIO_DETECT    5
// MAX98357AのSD(休止)端子。LOWで休止、HIGHで左チャンネル(こちらは常にL=Rで送る)。
// つながなければ基板の既定((L+R)/2)で鳴るので、配線は任意(ただしイヤホンへ切り替えてもスピーカーが黙らない)
#define AUDIO_SHUTDOWN  6
// --- イヤホン(任意): PCM5102AをMAX98357Aと同じI2Sの3本へ並べてつなぎ、どちらを鳴らすかを休止端子で選ぶ ---
// イヤホンの差し込み検出。3.5mmジャックの検出接点を、差すとGNDへ落ちるようにつなぐ(内部プルアップ、LOW=差してある)。
// つながなければ常に「差していない」(=スピーカー)になる
#define AUDIO_HP_DETECT 8
// PCM5102AのXSMT(ソフトミュート)端子。HIGHで鳴らす、LOWで消音。イヤホンを鳴らすときだけHIGHにする
#define AUDIO_HP_MUTE   14

// --- 音声入力: I2Sマイク(SPH0645)。出力とは別のI2S(別のPIOのステートマシン)で受ける ---
// 出力とBCLK/LRCLKを共有しないのは、SPH0645がBCLK 2.048〜4.096MHz・32bitスロットを要るため
// (出力は16bitスロットの22050/44100Hzで、範囲に入らない)。LRCLKはBCLK+1に固定(arduino-picoの決まり)。
// マイクのSELはGND(左チャンネル)、3VとGNDは本体の3V3/GNDへ
#define MIC_I2S_BCLK    26
#define MIC_I2S_LRCLK   27  // = MIC_I2S_BCLK + 1(ソフトからは指定しない。配線の控え)
#define MIC_I2S_DATA    28  // SPH0645のDOUT

// --- CardKB2のUARTモードの受信(/sys/cardkb.cfg で mode = uart のときだけ使う) ---
// CardKB2のTX(Groveの白=SCLの位置)をつなぐ。ハードのUARTの割り当てに空きが合わないので、
// arduino-picoのSerialPIO(PIOで作るUART)で受ける。I2Cモード(既定)ではGP0/GP1のI2C0
#define CARDKB_UART_RX  7
