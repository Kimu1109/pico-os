#pragma once
#include <cstddef>
#include <cstdint>
#include <SdFat.h>

// SD上の .wav(RIFF/WAVE)を読み、音源と同じ「モノラル16bit・out_rate Hz」のサンプル列へ直す係(1コア目専用)。
//
// - 対応: 整数PCM 8/16/24/32bit、浮動小数点 32bit(format 3)、WAVE_FORMAT_EXTENSIBLE(中身がそのどちらか)。
//   チャンネルは1〜8で、全チャンネルの平均をとってモノラルにする。
//   ADPCM・μ-law等の圧縮形式と64bit浮動小数点は Unsupported で断る
// - サンプリング周波数は線形補間で out_rate へ直す(帯域制限はしない。高い周波数から下げると少し濁る)
// - **ファイル全体をRAMへ載せない**。1KBの読み込みバッファで少しずつ読む
// - fmt/data以外のチャンク(LIST等)は読み飛ばす。dataの長さがファイルの残りより長い
//   (録音途中で切れた/長さ未記入の0xFFFFFFFF)ときは、ファイルの終わりまでを使う
class WavDecoder {
public:
    enum class Error : uint8_t { None, NoSd, OpenFailed, NotWav, Unsupported, NoData };

    static constexpr size_t kBufferBytes = 1024;
    static constexpr uint16_t kMaxChannels = 8;

    WavDecoder() = default;
    ~WavDecoder(){ this->close(); }
    WavDecoder(const WavDecoder&) = delete;
    WavDecoder& operator=(const WavDecoder&) = delete;

    // 開いて見出しを読む。失敗したら false(error()/errorText()で理由)
    bool open(const char* path, uint32_t out_rate);
    void close();
    bool isOpen() const { return this->open_; }

    // 終わりまで来たら頭から読み直す(BGM向け)
    void setLoop(bool loop){ this->loop_ = loop; }

    // 最大 max サンプルを書き、書いた数を返す。終わりまで来たら finished() が true になる
    size_t read(int16_t* out, size_t max);
    bool finished() const { return this->finished_; }

    Error error() const { return this->error_; }
    const char* errorText() const { return ErrorText(this->error_); }
    static const char* ErrorText(Error e);

    // 見出しの中身(open()が成功した後)
    uint16_t channels() const { return this->channels_; }
    uint16_t bitsPerSample() const { return this->bits_; }
    uint32_t sampleRate() const { return this->rate_; }
    uint32_t durationMs() const;

private:
    bool fail(Error e);
    bool parseHeader();
    bool refill();
    bool nextSource(int16_t& v);
    int32_t decodeFrame(const uint8_t* p) const;

    FsFile f_;
    bool open_ = false;
    bool loop_ = false;
    bool finished_ = false;
    Error error_ = Error::None;

    // 見出し
    uint16_t format_ = 0;       // 1=整数PCM 3=浮動小数点
    uint16_t channels_ = 0;
    uint16_t bits_ = 0;
    uint16_t block_ = 0;        // 1フレーム(全チャンネルぶん)のバイト数
    uint32_t rate_ = 0;
    uint32_t data_start_ = 0;
    uint32_t data_size_ = 0;
    uint32_t data_left_ = 0;    // まだバッファへ読み込んでいないバイト数

    uint8_t buf_[kBufferBytes];
    size_t buf_pos_ = 0;
    size_t buf_len_ = 0;

    // 周波数の変換(16.16)。s0_とs1_の間を frac_ の位置で補間する
    uint32_t step_ = 0;
    uint32_t frac_ = 0;
    int16_t s0_ = 0;
    int16_t s1_ = 0;
    bool primed_ = false;
    bool ending_ = false;      // 元のサンプルを読み切った(最後の1つを出している)
};
