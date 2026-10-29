#pragma once
#include <cstddef>
#include <cstdint>

// 16bitのサンプル列(1ch か左右交互の2ch)を out_rate Hz へ直す係(1コア目専用。WavDecoder・Mp3Decoder が持つ)。
// 元のサンプルは source(ctx, frame) で1つずつ受け取る(読み切ったら false を返す)。
//
// - 線形補間(16.16の固定小数点)。**下げるとき(44.1kHz→22050Hz、48kHz→44.1kHz等)は先に低域通過フィルタ**
//   (ハミング窓のFIR、元の周波数で回す)を掛け、out_rate の半分より上の音を削る。削らずに間引くと、
//   その音が聞こえる帯域へ折り返して濁るため。上げるとき・同じ周波数のときは掛けない(入れたのと同じ値が出る)
// - 同じ周波数なら入れたのと同じ数だけ出す(最後の1サンプルも出してから終える)
class PcmResampler {
public:
    struct Frame { int16_t v[2] = {0, 0}; };    // 左右(1chでは v[0] だけが意味を持つ)
    using Source = bool (*)(void* ctx, Frame& out);

    // 低域通過フィルタのタップ数の上限(奇数。2*kMaxHalfTaps+1)。比が大きい(96kHz等)と頭打ちになり、少し甘くなる
    static constexpr uint16_t kMaxHalfTaps = 31;
    static constexpr uint16_t kMaxTaps = 2 * kMaxHalfTaps + 1;

    // 周波数・チャンネル数(1 or 2)・読み元を決めて、途中の状態を捨てる。フィルタの係数もここで作る
    void setup(uint32_t in_rate, uint32_t out_rate, uint8_t lanes, Source source, void* ctx);
    // 途中の状態だけ捨てる(飛んだとき。周波数とフィルタはそのまま)
    void reset();

    // 最大 max フレーム(lanes が2なら out は max*2 個)を書き、書いたフレーム数を返す。
    // 読み元が終わり、残りも出し切ったら finished() が true になる
    size_t read(int16_t* out, size_t max);
    bool finished() const { return this->finished_; }

private:
    bool nextSource(Frame& v);
    void designFilter(uint32_t in_rate, uint32_t out_rate);
    void pushHistory(const Frame& x);
    int16_t filterOut(int lane) const;
    bool pull(Frame& v){ return this->source_ && this->source_(this->ctx_, v); }

    Source source_ = nullptr;
    void* ctx_ = nullptr;
    uint8_t lanes_ = 1;
    bool finished_ = false;

    // 周波数の変換(16.16)。s0_とs1_の間を frac_ の位置で補間する
    uint32_t step_ = 0x10000;
    uint32_t frac_ = 0;
    Frame s0_;
    Frame s1_;
    bool primed_ = false;
    bool ending_ = false;      // 元のサンプルを読み切った(最後の1つを出している)

    // 低域通過フィルタ(下げるときだけ)。係数はQ15で左右対称、合計は32768ちょうど(一定の値はそのまま出る)。
    // 出す値は履歴の真ん中(half_ だけ前)のサンプルを中心にした値なので、頭は最初の値で埋め、
    // 終わりは最後の値を half_ 回足して出し切る(入れた数と同じ数を出す)
    uint16_t half_ = 0;                 // 0ならフィルタ無し
    int16_t taps_[kMaxHalfTaps + 1];    // taps_[0] が真ん中、taps_[k] が真ん中から k 離れた所
    Frame   hist_[2 * kMaxTaps];        // 同じ値を2か所へ書き、どこからでも連続で読めるようにした輪
    uint16_t hist_pos_ = 0;
    uint16_t flush_left_ = 0;
    Frame last_raw_;
    bool filt_ready_ = false;
};
