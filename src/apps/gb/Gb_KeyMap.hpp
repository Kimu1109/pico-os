#pragma once
#include <cstdint>
#include "apps/gb/Gb_Emu.hpp"
#include "system/KeyInput_Functions.hpp"
#include "hal/CardKB_Uart.hpp"

// キーボード → Game Boyのボタン(GameBoyScene で使う)。
//
//   十字キー … ↑↓←→ / W A S D(CardKB2のUARTモードでは Fn+D/Z/X/C も)
//   A … K または X      B … J または Z
//   START … Enter       SELECT … Space または Backspace(CardKB2のDel)
//   Esc(CardKB2は Fn+1)… メニュー(途中保存など。外部コントローラーのZLと同じ)
//
// キーボードの打鍵(KeyInputFunctions::Event)には「離した」が無いので、押している間を2通りで作る:
//   - CardKB2のUARTモード: 本体が押した/離したを送ってくるので、CardKbFunctions::HeldKeys() をそのまま読む(正確)
//   - それ以外(USBシリアルの "key" の行・PCビルドのキー・CardKB2のI2Cモード): 打鍵が来たらしばらく押していることにする
//     (Hold)。最初は kFirstHoldMs(キーリピートが始まるまでの300msより長く)、続けて来たら kRepeatHoldMs ずつ延ばす。
//     そのため短く叩いても約0.35秒押したことになり、離してから止まるまで少し遅れる。正確に遊ぶならUARTモードか、
//     pad_serial.py のコントローラーのモード(押した/離したを送る)を使う
namespace GbKeyMap {

    // ボタンでないもの
    constexpr uint8_t kNone = 0;

    inline uint8_t FromChar(uint32_t cp){
        if(cp >= 'A' && cp <= 'Z') cp = cp - 'A' + 'a';
        switch(cp){
            case 'w': return GbEmu::Up;
            case 's': return GbEmu::Down;
            case 'a': return GbEmu::Left;
            case 'd': return GbEmu::Right;
            case 'k': case 'x': return GbEmu::A;
            case 'j': case 'z': return GbEmu::B;
            case ' ': return GbEmu::Select;
            default:  return kNone;
        }
    }

    // 打鍵 → ボタン(1つ)。Ctrl/Alt付きは使わない(電源メニュー等の操作を取らないため)
    inline uint8_t FromEvent(const KeyInputFunctions::Event& ev){
        using KeyInputFunctions::Key;
        if(ev.ctrl() || ev.alt()) return kNone;
        switch(ev.key){
            case Key::Up:        return GbEmu::Up;
            case Key::Down:      return GbEmu::Down;
            case Key::Left:      return GbEmu::Left;
            case Key::Right:     return GbEmu::Right;
            case Key::Enter:     return GbEmu::Start;
            case Key::Backspace: return GbEmu::Select;
            case Key::Char:      return FromChar(ev.cp);
            default:             return kNone;
        }
    }

    inline bool IsMenu(const KeyInputFunctions::Event& ev){
        return ev.key == KeyInputFunctions::Key::Escape;
    }

    // CardKB2(UART)の押しているキー → ボタン(同時押しもそのまま)
    inline uint8_t FromCardKb(uint64_t held){
        auto on = [held](int k){ return (held >> k) & 1u; };
        uint8_t b = 0;
        const bool fn = on(CardKbUart::kKeyFn);
        if(fn){
            if(on(25)) b |= GbEmu::Up;      // Fn+D
            if(on(35)) b |= GbEmu::Left;    // Fn+Z
            if(on(36)) b |= GbEmu::Down;    // Fn+X
            if(on(37)) b |= GbEmu::Right;   // Fn+C
        }else{
            if(on(25)) b |= GbEmu::Right;   // D
            if(on(35)) b |= GbEmu::B;       // Z
            if(on(36)) b |= GbEmu::A;       // X
        }
        if(on(12)) b |= GbEmu::Up;          // W
        if(on(23)) b |= GbEmu::Left;        // A
        if(on(24)) b |= GbEmu::Down;        // S
        if(on(30)) b |= GbEmu::A;           // K
        if(on(29)) b |= GbEmu::B;           // J
        if(on(CardKbUart::kKeyEnter))  b |= GbEmu::Start;
        if(on(CardKbUart::kKeySpace) || on(CardKbUart::kKeyDelete)) b |= GbEmu::Select;
        return b;
    }

    // CardKB2(UART)でメニューのキー(Fn+1)を押しているか
    inline bool CardKbMenu(uint64_t held){
        return ((held >> CardKbUart::kKeyFn) & 1u) && (held & 1u);
    }

    // 打鍵から「押している間」を作る(上の説明の2つ目)
    struct Hold {
        static constexpr uint32_t kFirstHoldMs  = 350;
        static constexpr uint32_t kRepeatHoldMs = 120;

        uint32_t until[8] = {};     // ボタンのビットごとに、押していることにする時刻の終わり
        uint8_t  active = 0;

        void press(uint8_t button, uint32_t now){
            for(int i = 0; i < 8; i++){
                if(!(button & (1u << i))) continue;
                const bool held = (active & (1u << i)) && (int32_t)(until[i] - now) > 0;
                until[i] = now + (held ? kRepeatHoldMs : kFirstHoldMs);
                active |= (uint8_t)(1u << i);
            }
        }
        uint8_t buttons(uint32_t now){
            for(int i = 0; i < 8; i++){
                if((active & (1u << i)) && (int32_t)(until[i] - now) <= 0) active &= (uint8_t)~(1u << i);
            }
            return active;
        }
        void clear(){ active = 0; }
    };
}
