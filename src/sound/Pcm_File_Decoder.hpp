#pragma once
#include <cstddef>
#include <cstdint>

// SDの音のファイルを読み、音源と同じ「16bit・out_rate Hz」のサンプル列へ直す係の共通の口(1コア目専用)。
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
    virtual size_t read(int16_t* out, size_t max) = 0;
    virtual bool finished() const = 0;

    virtual const char* errorText() const = 0;

    // 見出しの中身(open()が成功した後)
    virtual uint16_t channels() const = 0;
    virtual uint16_t bitsPerSample() const = 0;
    virtual uint32_t sampleRate() const = 0;
    virtual uint32_t durationMs() const = 0;
};
