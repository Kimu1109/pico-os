#pragma once
#include <cstddef>
#include <cstdint>
#include "sound/Chip_Synth.hpp"
#include "sound/Music_Data.hpp"

// 演奏データ(Music_Data.hpp)を時間どおりに音源へ渡すシーケンサー。**2コア目だけで使う。**
//
// - 時間はサンプル数で数える。1ティック = 22050×60 / (テンポ×48) サンプルを整数の積み上げで出すので、
//   長く鳴らしてもずれない
// - render() が「次のティックの境目」で区切って音源を回すので、音符はサンプル単位で正確な位置に鳴る
// - 効果音に借りられているチャンネル(borrowed)には触らない。曲はそのまま進み、返されたら次の音符から鳴らす
// - 演奏データは呼び出し側のもの。鳴らしている間は書き換えないこと
class MusicPlayer {
public:
    explicit MusicPlayer(uint32_t sample_rate) : rate_(sample_rate) {}

    // data を頭から鳴らす(鳴っている曲は止める)。形が不正ならfalse
    bool start(ChipSynth::Engine& engine, const uint8_t* data, size_t size);
    // 止める(曲が鳴らしていたチャンネルを止める)
    void stop(ChipSynth::Engine& engine);
    bool playing() const { return playing_; }
    // 今鳴らしている演奏データ(止まっていればnullptr)
    const uint8_t* data() const { return playing_ ? data_ : nullptr; }

    // 効果音に借りられているチャンネル(bit0 = ch0)。ここに立っているチャンネルへは触らない
    void setBorrowed(uint8_t mask) { borrowed_ = mask; }

    // n サンプル進める(曲の出来事を挟みながら engine.render する)。out が nullptr なら作らずに進める
    void render(ChipSynth::Engine& engine, int16_t* out, size_t n);

    uint16_t tempo() const { return tempo_; }

private:
    struct Loop {
        uint16_t start;     // LoopBeginの次
        uint8_t  remaining;
    };
    struct Track {
        bool     active = false;    // まだ終わっていない
        uint16_t pc = 0;
        uint16_t segno = 0;         // 0なら無し
        uint32_t remaining = 0;     // 次の出来事までのティック
        uint32_t gate = 0;          // 音を切るまでのティック(0なら切らない)
        bool     sounding = false;  // このトラックが音を鳴らしている
        uint8_t  wave = 2, volume = 15, gate_q = 7;
        int8_t   envelope = 0;
        uint8_t  saved_wave = 2, saved_volume = 15, saved_q = 7;
        int8_t   saved_envelope = 0;
        Loop     loops[MusicData::kMaxLoopDepth];
        int      depth = 0;
    };

    void tick(ChipSynth::Engine& engine);
    void fetch(ChipSynth::Engine& engine, int ch);
    void silence(ChipSynth::Engine& engine, int ch);
    uint32_t samplesToNextTick() const;

    uint32_t rate_;
    const uint8_t* data_ = nullptr;
    size_t size_ = 0;
    bool playing_ = false;
    uint16_t tempo_ = 120;
    uint8_t borrowed_ = 0;
    // ティックの境目までの積み上げ(単位: サンプル×テンポ×48。rate×60 に達したら1ティック)
    uint64_t acc_ = 0;
    Track tracks_[MusicData::kChannels];
};
