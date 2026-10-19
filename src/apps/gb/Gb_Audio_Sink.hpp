#pragma once
#include <cstdint>

// GBエミュ(GbEmu)が音源チップのレジスタへの書き込みを渡す先。
// 実物は SoundFunctions::GbAudio()(2コア目のGbApuへ時刻付きで渡す)。テストでは記録するだけの偽物を使う。
// エミュ本体をSoundFunctionsへ直接つながないのは、音を扱わないテスト(gb_emu_test)を軽く保つため。
class GbAudioSink {
public:
    virtual ~GbAudioSink() = default;
    // ROMを起動する(音源を電源投入時の状態へ)
    virtual void begin() = 0;
    // reg は0xFF10からの位置(0x00〜0x2F)、cycle はフレームの頭からのクロック数(0〜70223)
    virtual void write(uint32_t cycle, uint8_t reg, uint8_t val) = 0;
    // 1フレーム走り終えた
    virtual void endFrame() = 0;
    // ROMを閉じた/エミュが止まった(音を止める)
    virtual void end() = 0;
    // 鳴っているチャンネル(NR52の下位4bit)。音源から最後に知らされた値
    virtual uint8_t activeChannels() = 0;
};
