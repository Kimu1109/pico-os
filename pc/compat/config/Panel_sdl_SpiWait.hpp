// PCビルド用: 液晶への書き込みに、実機のSPI転送にかかるはずの時間ぶんの待ちを入れるSDLパネル。
//
// PCのSDLパネルへの書き込みはメモリへのコピーなので一瞬で終わる。実機では同じ書き込みが
// SPI(ILI9341へ1ピクセル16bit)の転送になり、画面1枚で約16msかかる。PCでは
// 「液晶へ送る量が多すぎて重い」類の問題(スクラッチパッドでキャンバス全体を毎回送っていた等)が
// 見えないので、転送量から理論上の転送時間を求めて、その時間だけ書き込み元を待たせる。
//
// ■ 理論値の求め方(LovyanGFXのrp2040実装 lgfx/v1/platforms/rp2040/common.cpp と同じ計算)
//   SPIのクロック = clk_peri / (CPSR × (1 + SCR))。LovyanGFXはCPSR=2に固定し、
//   SCR = FreqToClockDiv(freq_write) を選ぶ:
//       fapb = clk_peri / 2
//       fapb <= 要求周波数 なら SCR=0、そうでなければ SCR = fapb / (1 + 要求周波数)(上限255)
//   RP2350(arduino-picoの既定)は clk_peri = clk_sys = 150MHz なので、
//   TFT_MAX_SPEED=80MHzを要求しても実際は 150/2 = 75MHz 止まりになる。
//
//   転送量は「ピクセル数 × 16bit」+ 描く範囲の指定(CASET/RASET/RAMWR = コマンド3バイト+
//   データ8バイト = 88bit)。範囲の指定はLovyanGFXが変化の無い分を省くことがあるが、
//   ここでは毎回送る側(遅い側)で見積もる。
//
// ■ 含めていないもの
//   - 4bppのframeスプライトをRGB565へ変換するCPUの時間(PCのCPUの速さで決まってしまう)
//   - CSの上げ下げ・トランザクション開始などの細かいオーバーヘッド
//   - SPI0を共有するタッチ(XPT2046)の読み取り
//   つまりこの待ちは「実機はこれより速くはならない」という下限で、実機の方がもう少し遅い。
//
// ■ 切り替え
//   環境変数 PICOOS_SPI_WAIT = on / off(既定: ネイティブはon、Webはoff)。
//   Webではメインスレッドを止めることになるので既定でoff(URLのクエリ ?spi_wait=on で有効にできる)。
#pragma once

#include <lgfx/v1/platforms/sdl/Panel_sdl.hpp>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include "consts.hpp"

#if !defined(__EMSCRIPTEN__)
    #include <thread>
#endif

namespace PcSpiWait {
    // RP2350(arduino-pico既定)のclk_peri。clk_sysと同じ150MHz
    constexpr uint32_t kClkPeriHz = 150000000;

    // LovyanGFX(rp2040)のFreqToClockDiv()と、CPSR=2固定の組み合わせで決まる実際のSPIクロック
    constexpr uint32_t ActualSpiHz(uint32_t requested_hz){
        const uint32_t fapb = kClkPeriHz >> 1;
        const uint32_t div = (fapb <= requested_hz) ? 0
                           : std::min<uint32_t>(255, fapb / (1 + requested_hz));
        return kClkPeriHz / (2 * (div + 1));
    }

    constexpr uint32_t kSpiHz = ActualSpiHz(TFT_MAX_SPEED);
    constexpr uint32_t kBitsPerPixel = 16;  // ILI9341へはRGB565で送る
    constexpr uint32_t kWindowBits = 11 * 8; // CASET(1+4) + RASET(1+4) + RAMWR(1) バイト

    using Clock = std::chrono::steady_clock;

    inline bool enabled = false;
    // 液晶が「送り終わる」予定の時刻。書き込みが続けば後ろへ積み上がる
    inline Clock::time_point busy_until{};

    inline void Setup(){
#if defined(__EMSCRIPTEN__)
        enabled = false;
#else
        enabled = true;
#endif
        if(const char* v = getenv("PICOOS_SPI_WAIT")){
            if(strcmp(v, "on") == 0 || strcmp(v, "1") == 0) enabled = true;
            else if(strcmp(v, "off") == 0 || strcmp(v, "0") == 0) enabled = false;
            else printf("[PC] PICOOS_SPI_WAIT の値が不明です: %s (on/offのどちらか)\n", v);
        }
        const double full_ms = (double)SCREEN_WIDTH * SCREEN_HEIGHT * kBitsPerPixel * 1000.0 / kSpiHz;
        printf("[PC] 液晶転送の待ち: %s (SPI %.1fMHz相当、画面1枚 %.1fms)\n",
               enabled ? "有効" : "無効", kSpiHz / 1e6, full_ms);
    }

    // bitsぶんの転送が終わるまで待つ。実機の非DMA転送と同じく、呼び出し元はその間止まる
    inline void Transfer(uint64_t bits){
        if(!enabled || bits == 0) return;

        const auto cost = std::chrono::nanoseconds(bits * 1000000000ull / kSpiHz);
        const auto now = Clock::now();
        busy_until = std::max(busy_until, now) + cost;

        // OSのsleepは数十µs単位で寝過ごすので、長い分だけ寝て、最後は空回りで合わせる
        // (小さな転送が大量に来ても、合計がずれないようにするため)
#if !defined(__EMSCRIPTEN__)
        constexpr auto kSpinMargin = std::chrono::microseconds(200);
        const auto remaining = busy_until - Clock::now();
        if(remaining > kSpinMargin) std::this_thread::sleep_for(remaining - kSpinMargin);
#endif
        while(Clock::now() < busy_until){}
    }

    inline uint64_t PixelBits(uint64_t pixels){ return pixels * kBitsPerPixel; }
}

// 書き込み系の関数だけを包んで、転送量に応じた待ちを入れる。描画そのものはPanel_sdlに任せる
class Panel_sdl_SpiWait : public lgfx::Panel_sdl {
public:
    // 設定(環境変数)はここで読む。LGFXはグローバル初期化の時点で作られるが、
    // Webビルドはmain()の中でURLのクエリを環境変数へ置き直すので、それより後のinit()で読む
    bool init(bool use_reset) override {
        PcSpiWait::Setup();
        return lgfx::Panel_sdl::init(use_reset);
    }

    void drawPixelPreclipped(uint_fast16_t x, uint_fast16_t y, uint32_t rawcolor) override {
        lgfx::Panel_sdl::drawPixelPreclipped(x, y, rawcolor);
        PcSpiWait::Transfer(PcSpiWait::kWindowBits + PcSpiWait::PixelBits(1));
    }
    void writeFillRectPreclipped(uint_fast16_t x, uint_fast16_t y, uint_fast16_t w, uint_fast16_t h, uint32_t rawcolor) override {
        lgfx::Panel_sdl::writeFillRectPreclipped(x, y, w, h, rawcolor);
        PcSpiWait::Transfer(PcSpiWait::kWindowBits + PcSpiWait::PixelBits((uint64_t)w * h));
    }
    void setWindow(uint_fast16_t xs, uint_fast16_t ys, uint_fast16_t xe, uint_fast16_t ye) override {
        lgfx::Panel_sdl::setWindow(xs, ys, xe, ye);
        PcSpiWait::Transfer(PcSpiWait::kWindowBits);
    }
    void writeBlock(uint32_t rawcolor, uint32_t length) override {
        lgfx::Panel_sdl::writeBlock(rawcolor, length);
        PcSpiWait::Transfer(PcSpiWait::PixelBits(length));
    }
    void writeImage(uint_fast16_t x, uint_fast16_t y, uint_fast16_t w, uint_fast16_t h, lgfx::pixelcopy_t* param, bool use_dma) override {
        lgfx::Panel_sdl::writeImage(x, y, w, h, param, use_dma);
        PcSpiWait::Transfer(PcSpiWait::kWindowBits + PcSpiWait::PixelBits((uint64_t)w * h));
    }
    void writeImageARGB(uint_fast16_t x, uint_fast16_t y, uint_fast16_t w, uint_fast16_t h, lgfx::pixelcopy_t* param) override {
        lgfx::Panel_sdl::writeImageARGB(x, y, w, h, param);
        PcSpiWait::Transfer(PcSpiWait::kWindowBits + PcSpiWait::PixelBits((uint64_t)w * h));
    }
    void writePixels(lgfx::pixelcopy_t* param, uint32_t len, bool use_dma) override {
        lgfx::Panel_sdl::writePixels(param, len, use_dma);
        PcSpiWait::Transfer(PcSpiWait::PixelBits(len));
    }
};
