#pragma once
#include <cstddef>
#include <cstdint>
#include <SdFat.h>

#include "minimp3_pico.h"
#include "sound/Pcm_File_Decoder.hpp"
#include "sound/Pcm_Resampler.hpp"

// SD上の .mp3(MPEG-1/2/2.5 Layer III)を minimp3 で読み、音源と同じ「16bit・out_rate Hz」のサンプル列へ直す係(1コア目専用)。
// 出力は既定でモノラル(左右の平均)。setStereoOutput(true) ならステレオのファイルを左右交互で出す。
//
// - **ファイル全体をRAMへ載せない**。3KBの読み込みバッファへ少しずつ読み、1フレーム(最大1152サンプル)ずつ展開する
// - 頭のID3v2タグ・終わりのID3v1タグ(128バイト)は飛ばす。最初のフレームが Xing/Info(VBRの見出し)なら
//   フレーム数とシーク表(TOC)を読み、そのフレーム自体は鳴らさない。VBRI(Fraunhofer)はフレーム数だけ
// - 長さはフレーム数×1フレームのサンプル数。見出しが無ければ最初のフレームのビットレートから数える(CBRの前提。VBRだとずれる)
// - シークはTOCがあればそれで、無ければバイト位置の比例で飛び、次のフレームの頭から読み直す(ビットの貯金は捨てる)
// - エンコーダの頭の遅延・終わりの詰め物(LAMEの見出し)は削らない(曲の頭と終わりに数十msの無音が残る)
// - 周波数は PcmResampler で out_rate へ直す。途中で周波数・チャンネル数が変わるファイルは最初のフレームの値のまま扱う
//
// 大きさは約31KB(minimp3の状態と作業場所・展開したフレーム・読み込みバッファ)。SoundFunctions は .mp3 を鳴らす間だけ確保する
class Mp3Decoder : public PcmFileDecoder {
public:
    enum class Error : uint8_t { None, NoSd, OpenFailed, NotMp3, NoData };

    static constexpr size_t kBufferBytes = 3072;       // 1フレーム(320kbpsで約1.4KB)+次の見出しが必ず入る大きさ
    static constexpr size_t kScratchBytes = 16256;     // minimp3_pico_scratch_bytes(64bitの値。実機はこれより小さい)
    static constexpr uint32_t kMaxScanBytes = 65536;   // 頭からこれだけ探してフレームが無ければ NotMp3

    Mp3Decoder() = default;
    ~Mp3Decoder() override { this->close(); }
    Mp3Decoder(const Mp3Decoder&) = delete;
    Mp3Decoder& operator=(const Mp3Decoder&) = delete;

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

    // 見出しの中身(open()が成功した後)。bitsPerSample() は展開した後の16
    uint16_t channels() const override { return this->channels_; }
    uint16_t bitsPerSample() const override { return 16; }
    uint32_t sampleRate() const override { return this->rate_; }
    uint32_t durationMs() const override;
    uint16_t bitrateKbps() const { return this->bitrate_kbps_; }    // 最初のフレームの値
    bool hasToc() const { return this->has_toc_; }

private:
    using Frame = PcmResampler::Frame;
    static bool Source(void* ctx, Frame& v){ return static_cast<Mp3Decoder*>(ctx)->rawSource(v); }
    bool rawSource(Frame& v);
    bool decodeNext();
    void topUp();
    void restartAt(uint32_t pos);
    bool fail(Error e);
    bool parseHeader();
    bool findFrame(uint32_t from, mp3dec_frame_info_t& info, int& samples, uint32_t& frame_at);
    bool parseVbrHeader(const uint8_t* frame, size_t len);
    uint32_t seekOffset(uint32_t ms) const;

    FsFile f_;
    bool open_ = false;
    bool loop_ = false;
    bool finished_ = false;
    bool stereo_out_ = false;
    bool eof_ = false;          // 読み込みがファイル(音のデータ)の終わりまで来た
    uint8_t lanes_ = 1;
    Error error_ = Error::None;

    uint16_t channels_ = 0;
    uint32_t rate_ = 0;
    uint16_t bitrate_kbps_ = 0;
    uint16_t frame_samples_ = 0;    // 1フレームのサンプル数(MPEG-1は1152、2/2.5は576)
    uint32_t data_start_ = 0;       // 最初に鳴らすフレーム(ID3v2とXing/Infoの後)
    uint32_t data_end_ = 0;         // 音のデータの終わり(ID3v1の前)
    uint32_t file_pos_ = 0;         // 次にファイルから読む位置
    uint32_t total_frames_ = 0;     // Xing/Info/VBRI のフレーム数(無ければ0)
    bool has_toc_ = false;
    uint8_t toc_[100];              // Xing の TOC: toc_[i] は i% の時刻の位置(/256 × 長さ)

    char path_[128] = {0};          // reopen()用に覚えておく(長すぎるパスは覚えない=reopenできない)
    uint32_t out_rate_ = 22050;

    uint8_t buf_[kBufferBytes];
    size_t buf_pos_ = 0;
    size_t buf_len_ = 0;

    // 展開したフレーム(左右交互)。pcm_pos_ が次に出すサンプル(フレーム単位)
    int16_t pcm_[MINIMP3_MAX_SAMPLES_PER_FRAME];
    uint16_t pcm_count_ = 0;
    uint16_t pcm_pos_ = 0;
    uint8_t pcm_ch_ = 1;

    mp3dec_t dec_;
    alignas(8) uint8_t scratch_[kScratchBytes];
    PcmResampler rs_;
};
