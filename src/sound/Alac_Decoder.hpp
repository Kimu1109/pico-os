#pragma once
#include <cstddef>
#include <cstdint>
#include <SdFat.h>

#include "consts.hpp"
#include "sound/Pcm_File_Decoder.hpp"
#include "sound/Pcm_Resampler.hpp"

// SD上の .m4a(MP4コンテナの Apple Lossless)を読み、音源と同じ「16bit・out_rate Hz」のサンプル列へ直す係(1コア目専用)。
// 出力は既定でモノラル(左右の平均)。setStereoOutput(true) ならステレオのファイルを左右交互で出す。
//
// - **自前の実装(ライブラリは同梱しない)**。FlacDecoder と同じ作りで、整数演算だけ・ファイル全体をRAMへ載せない
// - **MP4の解析**: 末尾から見つけた moov(ffmpeg等の既定は mdat の後ろ。先頭にある faststart も可)の中の、
//   stsd が 'alac' の音声トラックを選ぶ。**サンプル表(stsz/stsc/stco)はRAMへ全部は置かず、SDから都度読む**
//   (長い曲でもRAMは増えない。stsz だけ128個ずつの窓を持つ)。mdat の中は frame 番号→位置を表から引いて読む
// - 対応: 1〜2ch・16/20/24bit・kMaxRate Hz 以下・1フレームの長さ kMaxFrame 以下(iTunes等は4096)。AACの .m4a は断る
//   (ALACだけ)。24bit等は16bitへ丸める(四捨五入。ディザは掛けない)
// - **長さ・シークはサンプル単位で正確**(stts の合計から)。シークは frame 番号へ飛んで、目的のサンプルまで捨てる
// - 壊れたフレームは、表から次のフレームの位置が分かるので、無音で置き換えて続ける(長さがずれない)
// - 周波数は PcmResampler で out_rate へ直す
//
// 大きさは約46KB(展開したフレーム 2ch×kMaxFrame×4B と、24bitの下位8bitの置き場・読み込みバッファ等)。
// SoundFunctions は MP3・FLAC と共用の1つの領域に、.m4a を鳴らす間だけ置く
class AlacDecoder : public PcmFileDecoder {
public:
    enum class Error : uint8_t { None, NoSd, OpenFailed, NotAlac, Aac, Unsupported, NoData };

    static constexpr size_t kBufferBytes = 4096;
    static constexpr uint32_t kMaxFrame = 4096;         // 1フレームのサンプル数の上限
    static constexpr uint32_t kMaxRate = 96000;         // これより高い周波数は断る(FLACと同じ。FlacDecoder::kMaxRate)
    static constexpr uint32_t kSizeWindow = 128;        // stsz を一度に読む個数

    AlacDecoder() = default;
    ~AlacDecoder() override { this->close(); }
    AlacDecoder(const AlacDecoder&) = delete;
    AlacDecoder& operator=(const AlacDecoder&) = delete;

    bool open(const char* path, uint32_t out_rate) override;
    void close() override;
    bool isOpen() const override { return this->open_; }

    bool seekMs(uint32_t ms) override;
    bool canReopen() const { return this->path_[0] != '\0'; }
    bool reopen(){ return this->path_[0] != '\0' && this->open(this->path_, this->out_rate_); }

    void setLoop(bool loop) override { this->loop_ = loop; }
    void setStereoOutput(bool stereo) override { this->stereo_out_ = stereo; }
    uint8_t outChannels() const override { return this->lanes_; }

    size_t read(int16_t* out, size_t max) override;
    bool finished() const override { return this->finished_; }

    Error error() const { return this->error_; }
    const char* errorText() const override { return ErrorText(this->error_); }
    static const char* ErrorText(Error e);

    // 見出しの中身(open()が成功した後)。bitsPerSample() は元の値(出力は16bit)
    uint16_t channels() const override { return this->channels_; }
    uint16_t bitsPerSample() const override { return this->bps_; }
    uint32_t sampleRate() const override { return this->rate_; }
    uint32_t durationMs() const override;
    uint64_t totalSamples() const { return this->total_samples_; }
    uint32_t frameCount() const { return this->frame_count_; }

    // 展開の速さ(1フレームを展開した時間。SDの読み込みを含む=1コア目が1フレームに使った時間。周波数の変換は含まない)。open() で0へ戻る
    struct DecodeStats {
        uint32_t blocks = 0;
        uint64_t total_us = 0;
        uint32_t max_us = 0;
        uint64_t total_samples = 0;
    };
    const DecodeStats& stats() const { return this->stats_; }
    uint64_t audioUs() const { return this->rate_ ? this->stats_.total_samples * 1000000u / this->rate_ : 0; }

private:
    using Frame = PcmResampler::Frame;
    static bool Source(void* ctx, Frame& v){ return static_cast<AlacDecoder*>(ctx)->rawSource(v); }
    bool rawSource(Frame& v);
    bool fail(Error e);

    // ---- ファイルとビットの読み取り ----
    bool seekFile(uint32_t pos);
    uint32_t bytePos() const;
    bool refill();
    void loadMore();
    uint32_t readBits(unsigned n);          // n=0..32。データが尽きたら exhausted_ を立てて0を返す
    int32_t readSigned(unsigned n);
    uint32_t readUnary9();                  // 1が続く数(最大9。9なら終わりの0は読まない)
    uint32_t readScalar(unsigned k, unsigned bps);
    void alignByte(){ this->nbits_ -= this->nbits_ % 8; }
    bool rd(uint32_t pos, void* buf, size_t n);     // ビット読み取りとは別に、表の読み取り用(読み込み位置は refill が毎回合わせ直す)

    // ---- MP4 ----
    struct Atom { uint32_t start = 0, body = 0, end = 0; };
    bool atomAt(uint32_t pos, uint32_t limit, Atom& a, char type[4]);
    bool findAtom(uint32_t from, uint32_t limit, const char* type, Atom& out);
    bool parseContainer();
    bool parseTrack(const Atom& trak, bool& saw_aac);
    bool parseCookie(uint32_t pos, uint32_t end);

    // ---- フレームの表 ----
    uint32_t sizeOf(uint32_t index);        // フレーム index のバイト数
    bool stscEntry(uint32_t i, uint32_t& first, uint32_t& spc);
    bool chunkOffset(uint32_t chunk, uint32_t& off);
    bool locate(uint32_t frame);            // frame 番号を次に読むフレームにする
    bool nextFrame(uint32_t& off);          // 次のフレームの位置を返して進める
    bool setupChunk(uint32_t chunk);

    // ---- 展開 ----
    bool advance();                         // 次のフレームへ進む(ループなら頭へ戻る)。時間も数える
    bool decodeNext();
    bool decodeFrame(uint32_t expect);
    bool decodeResidual(int32_t* out, uint32_t n, unsigned bps, int32_t hist_mult);
    void predict(int32_t* buf, uint32_t n, unsigned bps, int16_t* coefs, unsigned order, unsigned quant);
    void startAt(uint32_t frame);
    bool seekSample(uint64_t sample);

    FsFile f_;
    bool open_ = false;
    bool loop_ = false;
    bool finished_ = false;
    bool stereo_out_ = false;
    uint8_t lanes_ = 1;
    Error error_ = Error::None;

    // alac の設定(cookie)
    uint16_t channels_ = 0;
    uint16_t bps_ = 0;
    uint32_t rate_ = 0;
    uint32_t frame_length_ = 0;
    uint8_t rice_pb_ = 0, rice_mb_ = 0, rice_kb_ = 0;
    uint32_t timescale_ = 0;
    uint64_t total_samples_ = 0;
    uint32_t file_size_ = 0;

    // 表(SDの位置と個数だけ持つ)
    uint32_t stsz_pos_ = 0, stsz_fixed_ = 0, frame_count_ = 0;
    uint32_t stsc_pos_ = 0, stsc_count_ = 0;
    uint32_t stco_pos_ = 0, stco_count_ = 0;
    bool co64_ = false;
    uint32_t stts_pos_ = 0, stts_count_ = 0;
    uint32_t size_cache_[kSizeWindow];
    uint32_t size_base_ = 0, size_n_ = 0;

    // 次に読むフレームの位置の計算(stsc の何番目の組か・今のチャンクの中の何個目か)
    uint32_t frame_idx_ = 0;        // 次に読むフレーム番号
    uint32_t chunk_ = 0;            // 今のチャンク(0始まり)
    uint32_t in_chunk_ = 0;         // 今のチャンクの中で読んだ数
    uint32_t spc_ = 0;              // 今のチャンクのフレーム数
    uint32_t chunk_off_ = 0;        // 次のフレームのファイル位置
    uint32_t stsc_i_ = 0;           // 今の組
    uint32_t stsc_next_first_ = 0;  // 次の組の先頭チャンク(1始まり。最後なら stco_count_+1)
    uint32_t stsc_spc_ = 0;

    // 読み取り
    uint32_t buf_base_ = 0;
    size_t buf_pos_ = 0;
    size_t buf_len_ = 0;
    uint64_t acc_ = 0;
    unsigned nbits_ = 0;
    bool exhausted_ = false;

    // 今のフレーム。block_pos_ が次に出すサンプル、block_first_ がその先頭のサンプル番号(1ch分)
    uint32_t block_len_ = 0;
    uint32_t block_pos_ = 0;
    uint64_t block_first_ = 0;
    uint64_t skip_until_ = 0;       // シーク直後、これより前のサンプルは捨てる
    DecodeStats stats_;

    char path_[PICO_PATH_LEN + 1] = {0};
    uint32_t out_rate_ = 22050;

    uint8_t buf_[kBufferBytes];
    int32_t ch_[2][kMaxFrame];
    uint8_t low_[2][kMaxFrame];     // 24bit等の下位8bit(あるときだけ使う)
    PcmResampler rs_;
};
