#pragma once
#include <cstddef>
#include <cstdint>

#include "config/Hardware_Config.hpp"
#include "sound/Pcm_Sample.hpp"

// SDの音のファイルを読み、「out_rate Hz」のサンプル列(PcmSample)へ直す係の共通の口(1コア目専用)。
// 桁は16bit(既定)。24bitの版では setWideOutput(true) で24bitの桁にできる(sound/Pcm_Sample.hpp)。
// WavDecoder(.wav)と Mp3Decoder(.mp3)が持つ。SoundFunctions の WAV の経路はどちらもこの口だけで扱う
class PcmFileDecoder {
public:
    virtual ~PcmFileDecoder() = default;

    // 開いて見出しを読む。失敗したら false(errorText()で理由)
    virtual bool open(const char* path, uint32_t out_rate) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;

    // 再生位置(元のファイルの時刻 ms)へ飛ぶ。開いていなければ(読み終えて閉じた後は)開き直してから飛ぶ。失敗したら false
    virtual bool seekMs(uint32_t ms) = 0;
    // 終わりまで来たら頭から読み直す(BGM向け)
    virtual void setLoop(bool loop) = 0;
    // 2ch以上のファイルを左右に分けて出す(open()の前に決める)。実際に出る形は outChannels()
    virtual void setStereoOutput(bool stereo) = 0;
    virtual uint8_t outChannels() const = 0;

    // 最大 max フレーム(outChannels() が2なら左右交互なので out は max*2 個)を書き、書いたフレーム数を返す。
    // 終わりまで来たら finished() が true になる
    virtual size_t read(PcmSample* out, size_t max) = 0;
    virtual bool finished() const = 0;

    virtual const char* errorText() const = 0;

    // 24bitの桁(±8388607)で出す(open()の前に決める)。24bitの版(PICO_HW_SOUND_24BIT)だけが true にできる。
    // 16bitより深いファイル(24bitのFLAC/ALAC/WAV)は下位ビットを残し、16bitのファイルは8bit上へずらす
    void setWideOutput(bool wide){ this->wide_ = wide && PICO_HW_SOUND_24BIT; }
    bool wideOutput() const { return this->wide_; }

    // 見出しの中身(open()が成功した後)
    virtual uint16_t channels() const = 0;
    virtual uint16_t bitsPerSample() const = 0;
    virtual uint32_t sampleRate() const = 0;
    virtual uint32_t durationMs() const = 0;

protected:
    bool wide_ = false;
};
