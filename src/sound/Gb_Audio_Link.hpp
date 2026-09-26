#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

class GbApu;

// GBエミュ(1コア目)から音源チップ(GbApu、2コア目)へ、レジスタの書き込みを時刻付きで渡す列。
//
// エミュは1フレーム(70224クロック ≒ 16.74ms)ぶんを一気に走らせるので、書き込みをその場で
// 音源へ渡すと、フレームの中のどこで書いたかが失われる(1フレームに何度も音量を変える効果が潰れる)。
// そこで書き込みに「フレームの頭から何クロック目か」を付けて積み、2コア目はフレーム単位で取り出して、
// その時刻にあたるサンプルの位置でwrite()する。**音はエミュより約1フレーム遅れて鳴る**。
//
// - 1対1(積むのは1コア目、取り出すのは2コア目)なので、atomicの head/tail だけで足りる(ロック無し)
// - 取り出してよいのは「区切り」まで積み終えたものだけ(units)。フレームの途中までは読まない
// - 区切りは3種類: フレームの終わり / 始める(音源を電源投入時へ) / 止める(無音へ)
// - 満杯なら書き込みを捨てる(dropped())。区切りのために1つは空けておく
// - **エミュが止まったら(一時停止・ダイアログ等)音も止める**: 次のフレームが kStarveSamples 来なければ無音にする
//   (最後の音を鳴らし続けない)
// - 2コア目が追いつけず kMaxBacklog を超えて溜まったら、古いフレームは音を作らずに書き込みだけ当てて追いつく
class GbAudioLink {
public:
    static constexpr uint32_t kRingSize = 1024;     // 2のべき乗。1件4バイト
    static constexpr uint32_t kFrameCycles = 70224;
    static constexpr uint32_t kMaxBacklog = 4;      // これより多く溜まったら追いつく

    explicit GbAudioLink(uint32_t sample_rate);

    // ===== 1コア目 =====
    void begin();                                           // 音源を電源投入時の状態から始める
    bool write(uint32_t cycle, uint8_t reg, uint8_t val);   // reg は0xFF10からの位置。捨てたらfalse
    void endFrame();                                        // ここまでを1フレームとして渡す
    void end();                                             // 音を止める(ROMを閉じた)
    uint32_t dropped() const { return dropped_.load(std::memory_order_relaxed); }

    // ===== 2コア目 =====
    // n サンプルぶん時刻を進め、音を out へ足す。out が nullptr なら音は作らず時刻だけ進める
    void render(GbApu& apu, int16_t* out, size_t n);
    bool active() const { return active_; }                 // 始めてから止めるまで

private:
    enum Marker : uint8_t { kFrameEnd = 63, kBegin = 62, kEnd = 61 };

    static uint32_t Pack(uint32_t cycle, uint8_t reg, uint8_t val){
        return (cycle << 14) | ((uint32_t)(reg & 0x3F) << 8) | val;
    }
    static uint8_t RegOf(uint32_t e){ return (uint8_t)((e >> 8) & 0x3F); }
    static uint8_t ValOf(uint32_t e){ return (uint8_t)e; }
    static uint32_t CycleOf(uint32_t e){ return e >> 14; }

    bool push(uint32_t e, bool marker);
    uint32_t samplePos(uint32_t cycle) const;   // フレームの頭からのサンプル位置

    uint32_t ring_[kRingSize];
    std::atomic<uint32_t> head_{0};     // 積んだ数(1コア目だけが書く)
    std::atomic<uint32_t> tail_{0};     // 取り出した数(2コア目だけが書く)
    std::atomic<uint32_t> units_{0};    // 積み終えた区切りの数(1コア目だけが書く)
    std::atomic<uint32_t> dropped_{0};

    // ---- 2コア目だけ ----
    uint32_t rate_;
    uint32_t units_done_ = 0;
    bool     active_ = false;
    bool     in_frame_ = false;
    uint32_t frame_pos_ = 0;            // 今のフレームの中で何サンプル進んだか
    uint32_t frame_len_ = 0;
    uint64_t frac_ = 0;                 // クロック→サンプルの換算の端数(フレームをまたいで持ち越す)
    uint32_t idle_ = 0;                 // 次のフレームを待って作ったサンプル数
    uint32_t starve_samples_;
};
