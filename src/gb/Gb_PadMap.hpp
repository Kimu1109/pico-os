#pragma once
#include "gb/Gb_Emu.hpp"
#include "functions/Pad_Functions.hpp"

// 外部コントローラーのボタン(PadFunctions::Button) → Game Boyのボタン(GbEmu::Button)。
// Wiiクラシックコントローラーの並び(右がA、下がB、左がY)に合わせ、BはYでも押せる。
// HOMEはゲームのボタンではなく、GameBoySceneが「戻る」に使う
namespace GbPadMap {
    inline uint8_t ToGb(uint16_t pad){
        uint8_t gb = 0;
        if(pad & PadFunctions::Up)     gb |= GbEmu::Up;
        if(pad & PadFunctions::Down)   gb |= GbEmu::Down;
        if(pad & PadFunctions::Left)   gb |= GbEmu::Left;
        if(pad & PadFunctions::Right)  gb |= GbEmu::Right;
        if(pad & PadFunctions::A)      gb |= GbEmu::A;
        if(pad & (PadFunctions::B | PadFunctions::Y)) gb |= GbEmu::B;
        if(pad & PadFunctions::Start)  gb |= GbEmu::Start;
        if(pad & PadFunctions::Select) gb |= GbEmu::Select;
        return gb;
    }
}
