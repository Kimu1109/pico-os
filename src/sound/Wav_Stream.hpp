#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

// WAVの再生で、1コア目(SDから読んで直したサンプル)から2コア目(音源の出力へ足す)へ渡す列。
//
// - 1対1(積むのは1コア目、取り出すのは2コア目)なので atomic の head/tail だけで足りる(ロック無し)
// - 中身は音源と同じモノラル16bit(出力の周波数)。kRingSamples(12288 = 24KB。44100Hzで約280ms、22050Hzで約560ms)ぶん先読みできる。
//   2のべき乗ではないので、head/tail は 0〜kWrap-1 を巡る数で持つ(32bitの数を桁あふれさせると添字が飛ぶため)
//   **1コア目がこれより長く止まると(TLSのハンドシェイク等)音が途切れる**(途切れた回数は underruns())
// - **捨てる(flush)は1コア目からでも安全**: 捨てたい位置(その時点のhead)と世代を書くだけで、
//   2コア目が次に読むときに tail をそこまで進める。止める/別の曲へ切り替えるときに使う
// - 音量(0〜100)は2コア目が足すときに掛ける(全体の音量とは別。両方掛かる)
class WavStream {
public:
    static constexpr uint32_t kRingSamples = 12288;
    // head/tail の巡る範囲。kRingSamples の倍数で 2^31 に近いもの(捨てる位置 flush_to_ は古いまま残るので、
    // 範囲が狭いと tail が一巡したときに「まだ先にある」と取り違える。2^31 なら44100Hzで約13時間)
    static constexpr uint32_t kWrap = kRingSamples * (0x80000000u / kRingSamples);

    // ===== 1コア目 =====
    uint32_t freeSpace() const;                     // 今積める数
    size_t push(const int16_t* src, size_t n);      // 積めた数を返す
    void flush();                                   // 積んだものを全部捨てる
    bool buffered() const;                          // まだ鳴らしていないものが残っているか
    void setFeeding(bool feeding){ this->feeding_.store(feeding, std::memory_order_release); }
    void setVolume(uint8_t v){ this->volume_.store(v > 100 ? 100 : v, std::memory_order_release); }
    // 一時停止: 2コア目が取り出さなくなる(積むのは続くので、列が満杯になったら読み取りも止まる)
    void setPaused(bool paused){ this->paused_.store(paused, std::memory_order_release); }
    bool paused() const { return this->paused_.load(std::memory_order_acquire); }
    // まだ鳴らしていない(積んだが取り出されていない)サンプル数。再生位置の計算に使う
    uint32_t bufferedSamples() const;
    uint32_t underruns() const { return this->underruns_.load(std::memory_order_relaxed); }

    // ===== 2コア目 =====
    // n サンプルぶん取り出し、master(0〜100)と自分の音量を掛けて out へ足す。
    // out が nullptr なら足さずに捨てる(鳴らせない間も時間どおりに進める)
    void renderAdd(int16_t* out, size_t n, uint8_t master);
    bool hasData();                                 // 鳴らすものがあるか(省電力の判断用)

private:
    void applyFlush();          // 2コア目: 捨てる要求を当てる
    // 1コア目から見た tail(捨てた分を含めて進めたもの)。2コア目が捨てる位置を追い越していたら、
    // 捨てる位置を tail へ寄せ直す(古いまま残すと、tail が巡る範囲の半分を進んだときに「先にある」と取り違える)
    uint32_t effectiveTail() const;
    static uint32_t Dist(uint32_t a, uint32_t b){ return (a + kWrap - b) % kWrap; }     // a が b より何個先か
    static uint32_t Add(uint32_t a, uint32_t n){ return (a + n) % kWrap; }
    // f が t より先か(巡る範囲の半分までを「先」とみなす。2コア目が止まっていると、捨てる位置は列の長さより先へ進みうる)
    static bool Ahead(uint32_t f, uint32_t t){ const uint32_t d = Dist(f, t); return d > 0 && d < kWrap / 2; }

    int16_t ring_[kRingSamples];
    std::atomic<uint32_t> head_{0};         // 積んだ数(1コア目だけが書く)
    std::atomic<uint32_t> tail_{0};         // 取り出した数(2コア目だけが書く)
    mutable std::atomic<uint32_t> flush_to_{0};     // 1コア目: ここまで捨ててほしい
    std::atomic<uint32_t> flush_gen_{0};    // 1コア目: 捨てる要求の世代
    std::atomic<bool>     feeding_{false};  // 1コア目: まだ積み続けるつもりか(途切れの数え方に使う)
    std::atomic<bool>     paused_{false};
    std::atomic<uint8_t>  volume_{100};
    std::atomic<uint32_t> underruns_{0};

    uint32_t seen_gen_ = 0;     // 2コア目だけ
    bool starving_ = false;     // 2コア目だけ: いま途切れているか(1回の途切れを1回と数える)
};
