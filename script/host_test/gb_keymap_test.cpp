// キーボード → Game Boyのボタン(GbKeyMap)と、CardKB2(UART)をゲームのボタンとして読む口の検証。
// 確かめること:
//   - 打鍵の対応(矢印・WASD・大文字・A/B・START/SELECT・Esc=メニュー・Ctrl付きや他のキーは使わない)
//   - CardKB2の押しているキーの対応(同時押し・Fnで矢印・Fn+1=メニュー)
//   - Hold: 最初は350ms、続けて来たら120msずつ延びる、離せば止まる、ボタンごとに別
//   - CardKbUart::SetSuppressed: 押している状態は持ったまま打鍵を積まない、戻せば積む
#include "apps/gb/Gb_KeyMap.hpp"
#include "hal/CardKB_Uart.hpp"
#include "system/KeyInput_Functions.hpp"

#include <cstdio>
#include <string>

using KeyInputFunctions::Event;
using KeyInputFunctions::Key;

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

static Event K(Key k, uint8_t mods = 0){ Event e; e.key = k; e.mods = mods; return e; }
static Event C(uint32_t cp, uint8_t mods = 0){ Event e; e.key = Key::Char; e.cp = cp; e.mods = mods; return e; }

static void Frame(uint8_t key, bool pressed, unsigned long now){
    const uint8_t st = pressed ? CardKbUart::kStatePressed : CardKbUart::kStateReleased;
    const uint8_t f[5] = {0xAA, 0x03, key, st, (uint8_t)(0x03 + key + st)};
    for(uint8_t b : f) CardKbUart::Feed(b, now);
    CardKbUart::Tick(now);
}
static uint64_t Bit(int k){ return 1ull << k; }

int main(){
    using namespace GbKeyMap;

    // ---- 打鍵 ----
    check(FromEvent(K(Key::Up)) == GbEmu::Up && FromEvent(K(Key::Down)) == GbEmu::Down &&
          FromEvent(K(Key::Left)) == GbEmu::Left && FromEvent(K(Key::Right)) == GbEmu::Right, "打鍵: 矢印");
    check(FromEvent(C('w')) == GbEmu::Up && FromEvent(C('a')) == GbEmu::Left &&
          FromEvent(C('s')) == GbEmu::Down && FromEvent(C('d')) == GbEmu::Right, "打鍵: WASD");
    check(FromEvent(C('W', KeyInputFunctions::Shift)) == GbEmu::Up, "打鍵: 大文字(Shift)も同じ");
    check(FromEvent(C('k')) == GbEmu::A && FromEvent(C('x')) == GbEmu::A &&
          FromEvent(C('j')) == GbEmu::B && FromEvent(C('z')) == GbEmu::B, "打鍵: A=K/X、B=J/Z");
    check(FromEvent(K(Key::Enter)) == GbEmu::Start && FromEvent(C(' ')) == GbEmu::Select &&
          FromEvent(K(Key::Backspace)) == GbEmu::Select, "打鍵: START=Enter、SELECT=Space/Backspace");
    check(IsMenu(K(Key::Escape)) && FromEvent(K(Key::Escape)) == kNone, "打鍵: Escはメニュー(ボタンではない)");
    check(FromEvent(C('s', KeyInputFunctions::Ctrl)) == kNone && FromEvent(C('k', KeyInputFunctions::Alt)) == kNone,
          "打鍵: Ctrl/Alt付きは使わない");
    check(FromEvent(C('q')) == kNone && FromEvent(C('1')) == kNone && FromEvent(K(Key::Tab)) == kNone &&
          FromEvent(C(0x3042)) == kNone, "打鍵: 割り当てのないキー");

    // ---- CardKB2の押しているキー ----
    check(FromCardKb(Bit(12) | Bit(30)) == (GbEmu::Up | GbEmu::A), "CardKB2: W+Kの同時押し");
    check(FromCardKb(Bit(25)) == GbEmu::Right && FromCardKb(Bit(35)) == GbEmu::B && FromCardKb(Bit(36)) == GbEmu::A,
          "CardKB2: Fn無しの D/Z/X");
    const uint64_t fn = Bit(CardKbUart::kKeyFn);
    check(FromCardKb(fn | Bit(25)) == GbEmu::Up && FromCardKb(fn | Bit(35)) == GbEmu::Left &&
          FromCardKb(fn | Bit(36)) == GbEmu::Down && FromCardKb(fn | Bit(37)) == GbEmu::Right, "CardKB2: Fn+D/Z/X/Cは矢印");
    check(FromCardKb(fn | Bit(37) | Bit(29)) == (GbEmu::Right | GbEmu::B), "CardKB2: Fn+C(→)とJ(B)の同時押し");
    check(FromCardKb(Bit(CardKbUart::kKeyEnter) | Bit(CardKbUart::kKeySpace)) == (GbEmu::Start | GbEmu::Select),
          "CardKB2: Enter=START、Space=SELECT");
    check(FromCardKb(Bit(CardKbUart::kKeyDelete)) == GbEmu::Select, "CardKB2: Del=SELECT");
    check(CardKbMenu(fn | Bit(0)) && !CardKbMenu(Bit(0)) && !CardKbMenu(fn), "CardKB2: Fn+1がメニュー");
    check(FromCardKb(fn | Bit(0)) == 0 && FromCardKb(Bit(CardKbUart::kKeyAa)) == 0, "CardKB2: 他のキーはボタンにならない");

    // ---- Hold ----
    Hold h;
    h.press(GbEmu::A, 1000);
    check(h.buttons(1000) == GbEmu::A && h.buttons(1349) == GbEmu::A, "Hold: 最初は350ms押している");
    check(h.buttons(1350) == 0, "Hold: 続けて来なければ離す");
    h.press(GbEmu::Right, 2000);
    h.press(GbEmu::Right, 2300);    //キーリピート
    check(h.buttons(2410) == GbEmu::Right && h.buttons(2420) == 0, "Hold: 続けて来たら120msずつ延びる");
    h.press(GbEmu::Up, 3000);
    h.press(GbEmu::B, 3200);
    check(h.buttons(3300) == (GbEmu::Up | GbEmu::B) && h.buttons(3400) == GbEmu::B, "Hold: ボタンごとに別");
    h.clear();
    check(h.buttons(3401) == 0, "Hold: clear");

    // ---- CardKbUart の押している状態と打鍵を積まないモード ----
    KeyInputFunctions::Setup();
    CardKbUart::Reset();
    CardKbUart::SetSuppressed(true);
    Frame(30, true, 100);       // K
    Frame(12, true, 110);       // W
    check(CardKbUart::HeldKeys() == (Bit(30) | Bit(12)), "UART: 押しているキーが分かる(同時押し)");
    CardKbUart::Tick(800);      //繰り返しの時間を過ぎても
    check(KeyInputFunctions::Pending() == 0, "UART: 積まないモードでは打鍵(繰り返し含む)を積まない");
    Frame(30, false, 900);
    check(CardKbUart::HeldKeys() == Bit(12), "UART: 離せば外れる");
    Frame(12, false, 910);
    CardKbUart::SetSuppressed(false);
    Frame(23, true, 1000); Frame(23, false, 1010);
    Event ev;
    check(KeyInputFunctions::Pop(ev) && ev.key == Key::Char && ev.cp == 'a' && KeyInputFunctions::Pending() == 0,
          "UART: 戻せば文字として積む");

    printf("\n%s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
