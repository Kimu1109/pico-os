#pragma once
#include <cstddef>
#include <cstdint>
#include <SdFat.h>

#include "sound/Pcm_File_Decoder.hpp"
#include "sound/Pcm_Resampler.hpp"

// SD上の .wav(RIFF/WAVE)を読み、音源と同じ「16bit・out_rate Hz」のサンプル列へ直す係(1コア目専用)。
// 出力は既定でモノラル。setStereoOutput(true) なら2ch以上のファイルを左右交互のステレオにする。
//
// - 対応: 整数PCM 8/16/24/32bit、浮動小数点 32bit(format 3)、WAVE_FORMAT_EXTENSIBLE(中身がそのどちらか)。
//   チャンネルは1〜8で、モノラル出力は全チャンネルの平均。ステレオ出力は偶数番目の平均が左・奇数番目の平均が右(2chならそのまま)。
//   ADPCM・μ-law等の圧縮形式と64bit浮動小数点は Unsupported で断る
// - サンプリング周波数は PcmResampler で out_rate へ直す(下げるときは先に低域通過フィルタ)
// - **ファイル全体をRAMへ載せない**。1KBの読み込みバッファで少しずつ読む
// - fmt/data以外のチャンク(LIST等)は読み飛ばす。dataの長さがファイルの残りより長い
//   (録音途中で切れた/長さ未記入の0xFFFFFFFF)ときは、ファイルの終わりまでを使う
class WavDecoder : public PcmFileDecoder {
public:
    enum class Error : uint8_t { None, NoSd, OpenFailed, NotWav, Unsupported, NoData };

    static constexpr size_t kBufferBytes = 1024;
    static constexpr uint16_t kMaxChannels = 8;

    WavDecoder() = default;
    ~WavDecoder() override { this->close(); }
    WavDecoder(const WavDecoder&) = delete;
    WavDecoder& operator=(const WavDecoder&) = delete;

    // 開いて見出しを読む。失敗したら false(error()/errorText()で理由)
    bool open(const char* path, uint32_t out_rate) override;
    void close() override;
    bool isOpen() const override { return this->open_; }

    // 再生位置(元のファイルの時刻 ms)へ飛ぶ。開いていなければ(読み終えて閉じた後は)開き直してから飛ぶ。
    // 範囲外は終わりぎわへ丸める。周波数の変換の途中状態は捨てる。失敗したら false
    bool seekMs(uint32_t ms) override;
    bool canReopen() const { return this->path_[0] != '\0'; }
    bool reopen(){ return this->path_[0] != '\0' && this->open(this->path_, this->out_rate_); }

    // 終わりまで来たら頭から読み直す(BGM向け)
    void setLoop(bool loop) override { this->loop_ = loop; }

    // 2ch以上のファイルを左右に分けて出す(open()の前に決める)。既定はfalse(全チャンネルの平均のモノラル)。
    // trueでも1chのファイルはモノラル。実際に出る形は outChannels()
    void setStereoOutput(bool stereo) override { this->stereo_out_ = stereo; }
    uint8_t outChannels() const override { return this->lanes_; }

    // 最大 max フレーム(outChannels() が2なら左右交互なので out は max*2 個)を書き、書いたフレーム数を返す。
    // 終わりまで来たら finished() が true になる
    size_t read(int16_t* out, size_t max) override;
    bool finished() const override { return this->finished_; }

    Error error() const { return this->error_; }
    const char* errorText() const override { return ErrorText(this->error_); }
    static const char* ErrorText(Error e);

    // 見出しの中身(open()が成功した後)
    uint16_t channels() const override { return this->channels_; }
    uint16_t bitsPerSample() const override { return this->bits_; }
    uint32_t sampleRate() const override { return this->rate_; }
    uint32_t durationMs() const override;

private:
    bool fail(Error e);
    bool parseHeader();
    bool refill();
    using Frame = PcmResampler::Frame;
    static bool Source(void* ctx, Frame& v){ return static_cast<WavDecoder*>(ctx)->rawSource(v); }
    bool rawSource(Frame& v);
    void decodeFrame(const uint8_t* p, Frame& out) const;

    FsFile f_;
    bool open_ = false;
    bool loop_ = false;
    bool finished_ = false;
    bool stereo_out_ = false;
    uint8_t lanes_ = 1;         // 出力のチャンネル数(1 or 2)
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

    char path_[128] = {0};      // reopen()用に覚えておく(長すぎるパスは覚えない=reopenできない)
    uint32_t out_rate_ = 22050;

    uint8_t buf_[kBufferBytes];
    size_t buf_pos_ = 0;
    size_t buf_len_ = 0;

    PcmResampler rs_;
};
