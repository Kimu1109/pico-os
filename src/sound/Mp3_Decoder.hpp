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
// - Xing/Info に続くLAMEの見出し(LAME・ffmpeg等が書く)にエンコーダの遅延と終わりの詰め物があれば、展開した音の頭と終わりから削る
//   (継ぎ目の無音を無くす。ループの継ぎ目・曲の長さがサンプル単位で元の音と合う)。シークした後のその周は、終わりは削らない
//   (飛んだ先の位置が推定なので、削りすぎて音を切らないように。頭へ戻る(ループ)と元どおり削る)
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
    // LAMEの見出しの遅延と詰め物を読めたか(読めたら頭と終わりを削る)。値は見出しに書かれたまま(展開の遅延529は含まない)
    bool hasGapless() const { return this->has_gapless_; }
    uint16_t encoderDelay() const { return this->enc_delay_; }
    uint16_t encoderPadding() const { return this->enc_padding_; }

    // 展開の速さ(1フレームを mp3dec_decode_frame() で展開した時間。SDの読み込み・周波数の変換は含まない)。open() で0へ戻る
    struct DecodeStats {
        uint32_t frames = 0;        // 音の出たフレームの数
        uint64_t total_us = 0;
        uint32_t max_us = 0;
    };
    const DecodeStats& stats() const { return this->stats_; }
    // 1フレームが表す音の長さ(µs)。展開の時間をこれと比べると、1コア目のどれだけを使うかが分かる
    uint32_t frameAudioUs() const { return this->rate_ ? (uint32_t)((uint64_t)this->frame_samples_ * 1000000u / this->rate_) : 0; }

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
    uint32_t fullDurationMs() const;    // 削る前の長さ(シークの位置の換算に使う)
    void parseLameTag(const uint8_t* tag, size_t len);

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

    // 継ぎ目の無音を削る(LAMEの見出し)。位置は「展開した音」のサンプル(1チャンネル分)で数える
    bool has_gapless_ = false;
    uint16_t enc_delay_ = 0;
    uint16_t enc_padding_ = 0;
    uint32_t skip_start_ = 0;       // 頭から捨てる数(遅延+展開の遅延529)
    uint32_t play_end_ = 0;         // ここまでで終わる(全フレーム分−(詰め物−529))
    uint32_t dec_pos_ = 0;          // 次に出す展開した音の位置
    bool trim_end_ = true;          // 終わりを削ってよいか(位置が正確=頭から読んでいる)
    DecodeStats stats_;

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
