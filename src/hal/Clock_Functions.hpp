#pragma once
#include <cstdint>

// スリープ中にCPUのクロック(clk_sys)を下げる(深い省電力)。docs/design/power-display.md「スリープ中のクロック低減」。
//
// clk_sys を pll_sys の 1/N へ分周するだけ(PLLは止めない。戻すのは分周を1へ戻すだけで、PLLのロック待ちが無い)。
// 影響を受けるもの:
//   - SPI0(液晶・タッチ)/SPI1(SD)… clk_peri で動く。arduino-picoは起動時に clk_peri を pll_usb(48MHz)へ
//       付け替える(set_sys_clock_khz()。libpico.aの逆アセンブルで確認)ので速さは変わらない。ただしSPI(PL022)は
//       「clk_peri ≤ clk_sys」が要るので、**clk_sys は clk_peri より下げない**(150MHzなら1/3=50MHzまで。MinHz())。
//       clk_peri が clk_sys から来ている作りなら一緒に遅くなるだけなので、下限は kMinMhz だけ
//   - I2C(CardKB2・Wiiのコントローラー・RTC)… clk_sys で動くので 1/N で遅くなる(動く)
//   - CYW43(Wi-Fi)のPIOのSPI … 1/N で遅くなるだけで動く(接続は保つ)
//   - PWM(バックライト)… 1/N になるが、スリープ中は消してある
//   - I2S(音声・マイク)/SerialPIO(CardKB2のUART)… **周波数が狂う**。だから
//       ・I2Sの出力・マイクが動いている間は下げない(下げてよいかは2コア目が自分で判断する。下の Apply())
//       ・I2Sを始める直前に必ず戻す(EnsureFull()。Sound/Micの開始の中から呼ぶ)
//       ・CardKB2のUARTのときは下げるよう頼まない(PowerFunctions)
//   - タイマー(millis()/delay())・ウォッチドッグは clk_ref(水晶)から作るので狂わない。USBは pll_usb なので無関係
//
// **切り替えるのは音声のコア(2コア目)だけ**。I2Sを始めるのも2コア目なので、「下げた直後にI2Sが始まって
// 分周が狂った値で計算される」入れ違いが起きない。1コア目は Request() で頼むだけ。
// 2コア目を持たない版(音声もマイクも外した版)は I2S が無いので、1コア目の loop() が Apply(true) を呼ぶ(main.cpp)。
//
// PC/Web/ホストテストは分周の数を覚えるだけ(何も遅くならない)。
namespace ClockFunctions {

    // 下げる下限(MHz)。これより低いとUSB・Wi-Fiの割り込みの処理が間に合わない心配がある
    constexpr uint32_t kMinMhz = 12;

    // 起動時のclk_sysを覚え、pll_sysから直接来ているか確かめる(違えば下げない)。最初に1回
    void Setup();

    // 今のclk_sys(Hz)と、起動時(通常時)のclk_sys(Hz)
    uint32_t CurrentHz();
    uint32_t NormalHz();
    // clk_peri(Hz)と、clk_sysを下げてよい下限(Hz。kMinMhz と、clk_sysから独立したclk_peri の大きい方)
    uint32_t PeriHz();
    uint32_t MinHz();
    // MHz → 分周の数(1〜)。0や通常以上なら1。結果が MinHz() を下回らないように丸める
    uint32_t DividerForMhz(uint32_t mhz);

    // ===== 1コア目から使う =====
    // スリープ中に使ってほしい分周の数(1で通常へ)。実際の切り替えは次の Apply() で
    void Request(uint32_t divider);
    uint32_t Requested();
    // 今の分周の数(1なら通常)
    uint32_t Divider();
    // 下げられない作りか(clk_sysがpll_sysから来ていない等)
    bool Available();

    // ===== 切り替える側(2コア目。2コア目の無い版は1コア目)から使う =====
    // may_lower: I2Sの出力もマイクも止まっている。頼まれた分周へ合わせる(下げられなければ戻す)
    void Apply(bool may_lower);
    // I2S/PIOを始める直前に呼ぶ。下げていれば通常へ戻す
    void EnsureFull();
}
