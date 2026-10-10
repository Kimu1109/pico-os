#pragma once
#include <cstddef>
#include <cstdint>
#include <SdFat.h>

// SD上の .wav(RIFF/WAVE)を読み、音源と同じ「モノラル16bit・out_rate Hz」のサンプル列へ直す係(1コア目専用)。
//
// - 対応: 整数PCM 8/16/24/32bit、浮動小数点 32bit(format 3)、WAVE_FORMAT_EXTENSIBLE(中身がそのどちらか)。
//   チャンネルは1〜8で、全チャンネルの平均をとってモノラルにする。
//   ADPCM・μ-law等の圧縮形式と64bit浮動小数点は Unsupported で断る
// - サンプリング周波数は線形補間で out_rate へ直す。**下げるとき(44.1kHz→22050Hz等)は先に低域通過フィルタ
//   (ハミング窓のFIR、元の周波数で回す)を掛け、out_rate の半分より上の音を削る**。削らずに間引くと、
//   その音が聞こえる帯域へ折り返して濁るため。上げるとき・同じ周波数のときは掛けない(入れたのと同じ値が出る)
// - **ファイル全体をRAMへ載せない**。1KBの読み込みバッファで少しずつ読む
// - fmt/data以外のチャンク(LIST等)は読み飛ばす。dataの長さがファイルの残りより長い
//   (録音途中で切れた/長さ未記入の0xFFFFFFFF)ときは、ファイルの終わりまでを使う
class WavDecoder {
public:
    enum class Error : uint8_t { None, NoSd, OpenFailed, NotWav, Unsupported, NoData };

    static constexpr size_t kBufferBytes = 1024;
    static constexpr uint16_t kMaxChannels = 8;
    // 低域通過フィルタのタップ数の上限(奇数。2*kMaxHalfTaps+1)。比が大きい(96kHz等)と頭打ちになり、少し甘くなる
    static constexpr uint16_t kMaxHalfTaps = 31;
    static constexpr uint16_t kMaxTaps = 2 * kMaxHalfTaps + 1;

    WavDecoder() = default;
    ~WavDecoder(){ this->close(); }
    WavDecoder(const WavDecoder&) = delete;
    WavDecoder& operator=(const WavDecoder&) = delete;

    // 開いて見出しを読む。失敗したら false(error()/errorText()で理由)
    bool open(const char* path, uint32_t out_rate);
    void close();
    bool isOpen() const { return this->open_; }

    // 再生位置(元のファイルの時刻 ms)へ飛ぶ。開いていなければ(読み終えて閉じた後は)開き直してから飛ぶ。
    // 範囲外は終わりぎわへ丸める。周波数の変換の途中状態は捨てる。失敗したら false
    bool seekMs(uint32_t ms);
    bool canReopen() const { return this->path_[0] != '\0'; }
    bool reopen(){ return this->path_[0] != '\0' && this->open(this->path_, this->out_rate_); }

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
    bool rawSource(int16_t& v);
    void designFilter();
    void pushHistory(int16_t x);
    int16_t filterOut() const;
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

    char path_[128] = {0};      // reopen()用に覚えておく(長すぎるパスは覚えない=reopenできない)
    uint32_t out_rate_ = 22050;

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

    // 低域通過フィルタ(下げるときだけ)。係数はQ15で左右対称、合計は32768ちょうど(一定の値はそのまま出る)。
    // 出す値は履歴の真ん中(half_ だけ前)のサンプルを中心にした値なので、頭は最初の値で埋め、
    // 終わりは最後の値を half_ 回足して出し切る(入れた数と同じ数を出す)
    uint16_t half_ = 0;                 // 0ならフィルタ無し
    int16_t taps_[kMaxHalfTaps + 1];    // taps_[0] が真ん中、taps_[k] が真ん中から k 離れた所
    int16_t hist_[2 * kMaxTaps];        // 同じ値を2か所へ書き、どこからでも連続で読めるようにした輪
    uint16_t hist_pos_ = 0;
    uint16_t flush_left_ = 0;
    int16_t last_raw_ = 0;
    bool filt_ready_ = false;
};
