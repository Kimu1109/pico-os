#pragma once
#include <cstddef>
#include <cstdint>
#include <SdFat.h>

#include "consts.hpp"
#include "sound/Pcm_File_Decoder.hpp"
#include "sound/Pcm_Resampler.hpp"

// SD上の .flac(ネイティブFLAC。Ogg FLACは読まない)を読み、音源と同じ「16bit・out_rate Hz」のサンプル列へ直す係(1コア目専用)。
// 出力は既定でモノラル(左右の平均)。setStereoOutput(true) ならステレオのファイルを左右交互で出す。
//
// - **自前の実装(ライブラリは同梱しない)**。整数演算だけで、ファイル全体をRAMへ載せない。
//   4KBの読み込みバッファから少しずつ読み、1ブロック(最大 kMaxBlock サンプル)ずつ展開する
// - 対応: 1〜2ch・4〜24bit・kMaxRate Hz 以下・ブロック長 kMaxBlock 以下(CDの音は4096が多く、ffmpegは4608。
//   44.1k/48kのSubsetはどれも4608以下。`flac` コマンドは88.2k/96kでも4096だが、ffmpegは96kで8192にするので断る)。
//   これを超えるファイルは Unsupported で断る(黙って壊れた音を出さない)。88.2k/96kは出力の周波数へ下げて鳴らす
//   24bit等は16bitへ丸める(四捨五入。ディザは掛けない)
// - 頭のID3v2は飛ばす。後ろの余分なデータ(ID3v1・APE等)は、STREAMINFOの総サンプル数で終わりを決めるので読まない
// - フレームの見出しはCRC-8で確かめる。**フレーム全体のCRC-16は確かめない**(CPUを使うわりに、SDの読み誤りは
//   ほぼ起きず、壊れたフレームは副フレームの形の検査で大半が弾かれるため)。壊れたフレームは捨て、次の同期語まで進んで続ける
// - **長さ・シークはサンプル単位で正確**(STREAMINFOに総サンプル数があるとき)。つなぎ目の無音の処理は要らない。
//   SEEKTABLEがあればそれで飛び、無ければバイト位置の二分探索(フレームの見出しの標本番号を見る)
// - 周波数は PcmResampler で out_rate へ直す
//
// 大きさは約43KB(展開した2ch×kMaxBlock×4B・読み込みバッファ・パス・周波数の変換)。
// SoundFunctions は MP3 と共用の1つの領域に、.flac を鳴らす間だけ置く
class FlacDecoder : public PcmFileDecoder {
public:
    enum class Error : uint8_t { None, NoSd, OpenFailed, NotFlac, Unsupported, NoData };

    static constexpr size_t kBufferBytes = 4096;
    static constexpr uint32_t kMaxBlock = 4608;         // 1ブロックのサンプル数の上限
    // これより高い周波数は断る。96kHzは44.1k/48kの約2倍の計算(展開・周波数の変換)とSDの読み込み(24bitで毎秒約0.4MB)で済むが、
    // 192kHzはさらに倍でSD(SPI 10MHz)の余裕が無いので入れない(docs/design/sound/playback.md「88.2/96kHzの曲」)
    static constexpr uint32_t kMaxRate = 96000;
    static constexpr uint32_t kMaxResync = 262144;      // 壊れた所から次の同期語を探す最大のバイト数

    FlacDecoder() = default;
    ~FlacDecoder() override { this->close(); }
    FlacDecoder(const FlacDecoder&) = delete;
    FlacDecoder& operator=(const FlacDecoder&) = delete;

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
    uint64_t totalSamples() const { return this->total_samples_; }    // 0なら不明
    bool hasSeekTable() const { return this->seek_count_ > 0; }

    // 展開の速さ(1ブロックを展開した時間。SDの読み込み・周波数の変換も含む=1コア目が1ブロックに使った時間)。open() で0へ戻る
    struct DecodeStats {
        uint32_t blocks = 0;
        uint64_t total_us = 0;
        uint32_t max_us = 0;
        uint64_t total_samples = 0;     // 展開したサンプル数(1ch分)
    };
    const DecodeStats& stats() const { return this->stats_; }
    // 展開したサンプルが表す音の長さ(µs)。stats().total_us と比べると、1コア目のどれだけを使うかが分かる
    uint64_t audioUs() const { return this->rate_ ? this->stats_.total_samples * 1000000u / this->rate_ : 0; }

private:
    using Frame = PcmResampler::Frame;
    static bool Source(void* ctx, Frame& v){ return static_cast<FlacDecoder*>(ctx)->rawSource(v); }
    bool rawSource(Frame& v);
    bool fail(Error e);

    // ---- ファイルとビットの読み取り ----
    bool seekFile(uint32_t pos);            // ファイルのpos番目から読み直す(バッファは捨てる)
    uint32_t bytePos() const;               // 次に読むバイトのファイル位置(ビットの端数は切り捨て=バイト境界の前提)
    bool refill();                          // バッファを読み足す。もう無ければ false
    uint32_t readBits(unsigned n);          // n=0..32。データが尽きたら exhausted_ を立てて0を返す
    int32_t readSigned(unsigned n);
    uint32_t readUnary();
    void loadMore();                        // 貯めへ1〜4バイト足す(尽きたら0を足して exhausted_ を立てる)
    void alignByte(){ this->nbits_ -= this->nbits_ % 8; }

    // ---- 見出し ----
    bool parseMetadata();
    struct FrameHeader {
        uint32_t blocksize = 0;
        uint32_t rate = 0;
        uint8_t channels = 0;
        uint8_t assign = 0;         // 0〜7=独立 / 8=L+S / 9=S+R / 10=M+S
        uint8_t bps = 0;
        uint64_t number = 0;        // 固定ブロックならフレーム番号、可変なら先頭のサンプル番号
        bool variable = false;
        uint64_t firstSample(uint32_t fixed_block) const { return this->variable ? this->number : this->number * fixed_block; }
    };
    bool readFrameHeader(FrameHeader& h);   // 同期語から見出しのCRC-8まで。形が合わなければ false
    bool matchesStream(const FrameHeader& h) const;

    // ---- 展開 ----
    bool advance();                         // 次のブロックへ進む(ループなら頭へ戻る)。時間も数える
    bool decodeBlock();                     // 次のブロックを展開(block_*_ を埋める)。終わり/読めないなら false
    bool decodeFrameBody(const FrameHeader& h);
    bool decodeSubframe(int32_t* out, uint32_t n, unsigned bps);
    bool decodeResidual(int32_t* out, uint32_t n, unsigned order);
    bool resync(uint32_t from);             // from 以降で次のフレームの見出しを探し、その手前へ読み位置を置く
    void startAt(uint32_t pos, uint64_t sample);
    bool seekSample(uint64_t sample);
    bool seekBySeekTable(uint64_t sample);
    bool seekByBisect(uint64_t sample);
    bool probeFrame(uint32_t pos, FrameHeader& h, uint32_t& at);   // pos以降の最初のフレームの見出しと位置

    FsFile f_;
    bool open_ = false;
    bool loop_ = false;
    bool finished_ = false;
    bool stereo_out_ = false;
    uint8_t lanes_ = 1;
    Error error_ = Error::None;

    // STREAMINFO
    uint16_t channels_ = 0;
    uint16_t bps_ = 0;
    uint32_t rate_ = 0;
    uint16_t min_block_ = 0;
    uint16_t max_block_ = 0;
    uint64_t total_samples_ = 0;
    uint32_t audio_start_ = 0;      // 最初のフレームの位置
    uint32_t file_size_ = 0;
    uint32_t fixed_block_ = 0;      // 固定ブロック長のストリームの長さ(可変・不明なら0)
    uint32_t seek_pos_ = 0;         // SEEKTABLE の中身の位置(無ければ seek_count_ が0)
    uint32_t seek_count_ = 0;

    // 読み取り
    uint32_t buf_base_ = 0;         // buf_[0] のファイル位置
    size_t buf_pos_ = 0;
    size_t buf_len_ = 0;
    uint64_t acc_ = 0;              // ビットの貯め(下位 nbits_ ビットが未読)
    unsigned nbits_ = 0;
    bool exhausted_ = false;        // ファイルの終わりを越えて読もうとした

    // 今のブロック。pos_ が次に出すサンプル、block_first_ がその先頭のサンプル番号(1ch分)
    uint32_t block_len_ = 0;
    uint32_t block_pos_ = 0;
    uint64_t block_first_ = 0;
    uint64_t next_sample_ = 0;      // 次のブロックの先頭と期待するサンプル番号
    uint64_t skip_until_ = 0;       // シーク直後、これより前のサンプルは捨てる
    DecodeStats stats_;

    char path_[PICO_PATH_LEN + 1] = {0};     // reopen()用に覚えておく(長すぎるパスは覚えない=reopenできない)
    uint32_t out_rate_ = 22050;

    uint8_t buf_[kBufferBytes];
    int32_t ch_[2][kMaxBlock];
    PcmResampler rs_;
};
