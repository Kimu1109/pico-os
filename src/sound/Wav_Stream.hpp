#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

// WAVの再生で、1コア目(SDから読んで直したサンプル)から2コア目(音源の出力へ足す)へ渡す列。
//
// - 1対1(積むのは1コア目、取り出すのは2コア目)なので atomic の head/tail だけで足りる(ロック無し)
// - 中身は音源と同じモノラル16bit・22050Hz。kRingSamples(8192 ≒ 370ms・16KB)ぶん先読みできる。
//   **1コア目がこれより長く止まると(TLSのハンドシェイク等)音が途切れる**(途切れた回数は underruns())
// - **捨てる(flush)は1コア目からでも安全**: 捨てたい位置(その時点のhead)と世代を書くだけで、
//   2コア目が次に読むときに tail をそこまで進める。止める/別の曲へ切り替えるときに使う
// - 音量(0〜100)は2コア目が足すときに掛ける(全体の音量とは別。両方掛かる)
class WavStream {
public:
    static constexpr uint32_t kRingSamples = 8192;     // 2のべき乗

    // ===== 1コア目 =====
    uint32_t freeSpace() const;                     // 今積める数
    size_t push(const int16_t* src, size_t n);      // 積めた数を返す
    void flush();                                   // 積んだものを全部捨てる
    bool buffered() const;                          // まだ鳴らしていないものが残っているか
    void setFeeding(bool feeding){ this->feeding_.store(feeding, std::memory_order_release); }
    void setVolume(uint8_t v){ this->volume_.store(v > 100 ? 100 : v, std::memory_order_release); }
    uint32_t underruns() const { return this->underruns_.load(std::memory_order_relaxed); }

    // ===== 2コア目 =====
    // n サンプルぶん取り出し、master(0〜100)と自分の音量を掛けて out へ足す。
    // out が nullptr なら足さずに捨てる(鳴らせない間も時間どおりに進める)
    void renderAdd(int16_t* out, size_t n, uint8_t master);
    bool hasData();                                 // 鳴らすものがあるか(省電力の判断用)

private:
    void applyFlush();          // 2コア目: 捨てる要求を当てる
    uint32_t effectiveTail() const;     // 1コア目から見た tail(捨てた分を含めて進めたもの)

    int16_t ring_[kRingSamples];
    std::atomic<uint32_t> head_{0};         // 積んだ数(1コア目だけが書く)
    std::atomic<uint32_t> tail_{0};         // 取り出した数(2コア目だけが書く)
    std::atomic<uint32_t> flush_to_{0};     // 1コア目: ここまで捨ててほしい
    std::atomic<uint32_t> flush_gen_{0};    // 1コア目: 捨てる要求の世代
    std::atomic<bool>     feeding_{false};  // 1コア目: まだ積み続けるつもりか(途切れの数え方に使う)
    std::atomic<uint8_t>  volume_{100};
    std::atomic<uint32_t> underruns_{0};

    uint32_t seen_gen_ = 0;     // 2コア目だけ
    bool starving_ = false;     // 2コア目だけ: いま途切れているか(1回の途切れを1回と数える)
};
