#include "hal/Clock_Functions.hpp"

#include <atomic>

// 実機はpico-sdkのクロックの窓口で分周を変える。PC/Web/ホストテストは数を覚えるだけ
// (Battery_Functions.cppと同じく、分岐はこのファイルの中に閉じ込める)
#if !defined(PICOOS_PC)
#include <hardware/clocks.h>
#endif

namespace {
    // 1コア目→切り替える側: 使ってほしい分周
    std::atomic<uint32_t> requested{1};
    // 切り替える側→1コア目: 今の分周
    std::atomic<uint32_t> current{1};
    uint32_t normal_hz = 150000000u;
    uint32_t peri_hz   = 48000000u;
    uint32_t min_hz    = 48000000u;
    bool     available = false;

    void SetDivider(uint32_t div){
        if(div == current.load(std::memory_order_relaxed)) return;
#if !defined(PICOOS_PC)
        //pll_sysのまま分周だけ変える。clock_configure()は分周を上げるときは先に分周を、
        //下げるときは後に分周を書くので、途中でclk_sysが通常より速くなることは無い
        clock_configure(clk_sys,
                        CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
                        CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
                        normal_hz, normal_hz / div);
#endif
        current.store(div, std::memory_order_release);
    }
}

void ClockFunctions::Setup(){
    requested.store(1, std::memory_order_relaxed);
    current.store(1, std::memory_order_relaxed);
#if !defined(PICOOS_PC)
    normal_hz = clock_get_hz(clk_sys);
    //arduino-picoは起動時に clk_sys = pll_sys(分周1)にする。違う作りなら何もしない
    //(戻す先の分周・周波数が分からなくなるため)
    const uint32_t ctrl = clocks_hw->clk[clk_sys].ctrl;
    const uint32_t src    = (ctrl & CLOCKS_CLK_SYS_CTRL_SRC_BITS) >> CLOCKS_CLK_SYS_CTRL_SRC_LSB;
    const uint32_t auxsrc = (ctrl & CLOCKS_CLK_SYS_CTRL_AUXSRC_BITS) >> CLOCKS_CLK_SYS_CTRL_AUXSRC_LSB;
    available = src == CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX
             && auxsrc == CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS
             && clocks_hw->clk[clk_sys].div == (1u << CLOCKS_CLK_SYS_DIV_INT_LSB);
    peri_hz = clock_get_hz(clk_peri);
    const uint32_t peri_aux = (clocks_hw->clk[clk_peri].ctrl & CLOCKS_CLK_PERI_CTRL_AUXSRC_BITS) >> CLOCKS_CLK_PERI_CTRL_AUXSRC_LSB;
    const bool peri_follows_sys = peri_aux == CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS;
#else
    //実機(arduino-pico)と同じ作り: clk_sys = pll_sys 150MHz、clk_peri = pll_usb 48MHz
    normal_hz = 150000000u;
    peri_hz   = 48000000u;
    available = true;
    const bool peri_follows_sys = false;
#endif
    //SPI(PL022)は clk_peri ≤ clk_sys が要る。clk_periがclk_sysから来ていれば一緒に下がるので気にしなくてよい
    min_hz = kMinMhz * 1000000u;
    if(!peri_follows_sys && peri_hz > min_hz) min_hz = peri_hz;
}

uint32_t ClockFunctions::NormalHz(){ return normal_hz; }
uint32_t ClockFunctions::CurrentHz(){ return normal_hz / current.load(std::memory_order_acquire); }

uint32_t ClockFunctions::PeriHz(){ return peri_hz; }
uint32_t ClockFunctions::MinHz(){ return min_hz; }

uint32_t ClockFunctions::DividerForMhz(uint32_t mhz){
    if(mhz == 0) return 1;
    uint64_t target = (uint64_t)mhz * 1000000u;
    if(target < min_hz) target = min_hz;
    if(target >= normal_hz) return 1;
    //結果が指定(と下限)を下回らない最大の分周。150MHz・下限48MHzなら 50→3(50MHz)、40→3、30→3
    return (uint32_t)(normal_hz / target);
}

void ClockFunctions::Request(uint32_t divider){
    if(divider < 1) divider = 1;
    requested.store(divider, std::memory_order_release);
}
uint32_t ClockFunctions::Requested(){ return requested.load(std::memory_order_acquire); }
uint32_t ClockFunctions::Divider(){ return current.load(std::memory_order_acquire); }
bool ClockFunctions::Available(){ return available; }

void ClockFunctions::Apply(bool may_lower){
    const uint32_t want = requested.load(std::memory_order_acquire);
    SetDivider((available && may_lower && want > 1) ? want : 1);
}

void ClockFunctions::EnsureFull(){ SetDivider(1); }
