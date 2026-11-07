#pragma once
#include <cstdint>

#include "config/Hardware_Config.hpp"

// 音声ファイルの経路(読み取り係 → 周波数の変換 → 先読みの列 WavStream → 2コア目のミックス)で使う標本の型。
//
// - 既定の版(PICO_HW_SOUND_24BIT=0)は int16_t。16bitの桁で持つ(今までどおり)
// - 24bitの版は int32_t。鳴らす深さ(SoundFunctions::BitDepth())に合わせて、16bitのときは16bitの桁(±32767)、
//   24bitのときは24bitの桁(±8388607)の値を入れる。どちらの桁かは読み取り係の setWideOutput() で決まり、
//   同じ列に2つの桁が混ざらないよう、深さを変えるときは鳴っている音を止めて列を捨てる
//   (docs/design/sound/output.md「24bitの出力」)
#if PICO_HW_SOUND_24BIT
using PcmSample = int32_t;
#else
using PcmSample = int16_t;
#endif

namespace PcmWide {
    constexpr int32_t kMax24 = 8388607;
    constexpr int32_t kMin24 = -8388608;
    inline int32_t Clamp24(int64_t v){ return v > kMax24 ? kMax24 : (v < kMin24 ? kMin24 : (int32_t)v); }
    inline int32_t Clamp16(int32_t v){ return v > 32767 ? 32767 : (v < -32768 ? -32768 : v); }
    // bits ビット(4〜32)の標本を、24bitの桁(wide)か16bitの桁へ直す(深いものは四捨五入。ディザは掛けない)
    inline int32_t FromBits(int32_t v, int bits, bool wide){
        const int to = wide ? 24 : 16;
        if(bits == to) return v;
        if(bits < to) return (int32_t)((uint32_t)v << (to - bits));
        const int sh = bits - to;
        const int64_t r = ((int64_t)v + ((int64_t)1 << (sh - 1))) >> sh;
        if(wide) return Clamp24(r);
        return r > 32767 ? 32767 : (r < -32768 ? -32768 : (int32_t)r);
    }
}
