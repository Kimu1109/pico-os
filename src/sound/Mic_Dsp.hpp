#pragma once
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

// I2Sマイク(SPH0645)の生のワードを、録音する16bitモノラルへ直す計算(2コア目で動く)。
// I2Sにも描画にもSDにも依存しないので、ホストテストでそのまま確かめられる。
//
//   32bitのワード(左,右,左,右…)→ 選んだチャンネル → 18bitの値(SPH0645は上位18bitだけが有効)
//   → 直流を落とす(SPH0645は直流のずれが大きい。一次のハイパス、約38Hz)
//   → 増幅(gain、dB)→ 間引き(48000Hz → 16000/24000Hz。窓付きsincのFIRで先に高い音を削る)
//   → 16bitへ頭打ち
//
// - 確保はしない(係数と履歴は固定長の配列)
// - 「信号が来ているか」も見る: マイクがつながっていない/電源が無いとDINは同じ値(多くは0)のまま変わらない。
//   SPH0645は無音でも下位のビットが揺れるので、kStillFrames の間ずっと同じ値なら「信号なし」とする
namespace MicDsp {

    // I2Sの周波数。SPH0645はBCLK 2.048〜4.096MHzが要る(32bit×2ch=64クロック/フレーム)ので、
    // 48000Hz(BCLK 3.072MHz)で受けて、録音する周波数へ間引く
    constexpr uint32_t kCaptureRate = 48000;
    constexpr int      kMaxDecim    = 3;            // 16000Hz
    constexpr int      kMaxTaps     = 8 * kMaxDecim + 1;
    constexpr uint32_t kStillFrames = kCaptureRate / 2;     // 0.5秒同じ値なら「信号なし」
    constexpr int      kMinGainDb   = 0;
    constexpr int      kMaxGainDb   = 36;
    constexpr double   kPi          = 3.14159265358979323846;

    // 録音できる周波数か(48000を割り切れて、間引きがkMaxDecim以下)
    inline bool ValidRate(uint32_t rate){
        return rate == 16000 || rate == 24000 || rate == 48000;
    }

    // dB → 倍率(1/256単位)。0dBで256
    inline int32_t GainQ8(int db){
        if(db < kMinGainDb) db = kMinGainDb;
        if(db > kMaxGainDb) db = kMaxGainDb;
        return (int32_t)lroundf(256.0f * powf(10.0f, (float)db / 20.0f));
    }

    class Pipeline {
    public:
        // out_rate: ValidRate()の値。right=true なら右チャンネル(SELを3V3にしたマイク)を使う
        bool configure(uint32_t out_rate, bool right){
            if(!ValidRate(out_rate)) return false;
            decim_ = (int)(kCaptureRate / out_rate);
            right_ = right;
            taps_ = (decim_ == 1) ? 1 : 8 * decim_ + 1;
            if(taps_ == 1){
                coef_[0] = 32768;
            }else{
                //窓付きsinc(ハミング)。切れ目は出力のナイキスト周波数の9割
                const double fc = 0.45 / decim_;           // 入力の周波数に対する割合
                const int mid = taps_ / 2;
                double h[kMaxTaps];
                double sum = 0;
                for(int i = 0; i < taps_; i++){
                    const int n = i - mid;
                    const double sinc = (n == 0) ? 2.0 * fc : sin(2.0 * kPi * fc * n) / (kPi * n);
                    const double win = 0.54 - 0.46 * cos(2.0 * kPi * i / (taps_ - 1));
                    h[i] = sinc * win;
                    sum += h[i];
                }
                //合計を1(32768)にそろえる(直流の大きさが変わらないように)
                int32_t total = 0;
                for(int i = 0; i < taps_; i++){
                    coef_[i] = (int32_t)lround(h[i] / sum * 32768.0);
                    total += coef_[i];
                }
                coef_[mid] += 32768 - total;
            }
            reset();
            return true;
        }

        void reset(){
            for(int i = 0; i < kMaxTaps; i++) hist_[i] = 0;
            hist_pos_ = 0;
            phase_ = 0;
            dc_x1_ = 0;
            dc_y_q8_ = 0;
            primed_ = false;
            last_raw_ = 0;
            still_ = 0;
            peak_ = 0;
        }

        void setGainQ8(int32_t g){ gain_q8_ = g; }
        int decimation() const { return decim_; }

        // ワードを左右の組で pairs 組受け取り、out へ最大 out_cap 個書く。書いた数を返す
        // (out_cap は pairs / decimation() 以上あること。足りない分は捨てる)
        size_t process(const uint32_t* words, size_t pairs, int16_t* out, size_t out_cap){
            size_t n_out = 0;
            for(size_t i = 0; i < pairs; i++){
                const uint32_t raw = words[i * 2 + (right_ ? 1 : 0)];
                if(raw == last_raw_){
                    if(still_ < kStillFrames) still_++;
                }else{
                    last_raw_ = raw;
                    still_ = 0;
                }
                //上位18bit(符号付き)
                const int32_t s = (int32_t)raw >> 14;
                if(!primed_){
                    //最初の値を直流とみなして始める(始まりのドンという音を出さない)
                    dc_x1_ = s;
                    primed_ = true;
                }
                //直流を落とす: y = x - x1 + a*y1(a ≒ 0.995、48kHzで約38Hz)。yは1/256単位
                dc_y_q8_ = (int32_t)((int64_t)(s - dc_x1_) * 256 + (((int64_t)dc_y_q8_ * 32604) >> 15));
                dc_x1_ = s;
                //増幅して16bitの尺度へ(18bit → 16bitで>>2、yの1/256で>>8、gainの1/256で>>8)
                const int32_t v = (int32_t)(((int64_t)dc_y_q8_ * gain_q8_) >> 18);

                hist_[hist_pos_] = v;
                hist_pos_ = (hist_pos_ + 1 == taps_) ? 0 : hist_pos_ + 1;
                if(++phase_ < decim_) continue;
                phase_ = 0;

                int64_t acc = 0;
                int idx = hist_pos_;        //一番古いもの
                for(int t = 0; t < taps_; t++){
                    acc += (int64_t)hist_[idx] * coef_[t];
                    idx = (idx + 1 == taps_) ? 0 : idx + 1;
                }
                int32_t y = (int32_t)(acc >> 15);
                if(y > 32767) y = 32767;
                if(y < -32768) y = -32768;
                const int32_t a = y < 0 ? -y : y;
                if(a > peak_) peak_ = a;
                if(n_out < out_cap) out[n_out++] = (int16_t)y;
            }
            return n_out;
        }

        // 前に読んでからの一番大きい振幅(0〜32768)を返して0へ戻す
        int32_t takePeak(){ const int32_t p = peak_; peak_ = 0; return p; }
        // 同じ値が kStillFrames 続いていないか
        bool hasSignal() const { return still_ < kStillFrames; }

    private:
        int decim_ = 3;
        int taps_ = 1;
        bool right_ = false;
        int32_t gain_q8_ = 256;
        int32_t coef_[kMaxTaps] = {32768};
        int32_t hist_[kMaxTaps] = {};
        int hist_pos_ = 0;
        int phase_ = 0;
        int32_t dc_x1_ = 0;
        int32_t dc_y_q8_ = 0;
        bool primed_ = false;
        uint32_t last_raw_ = 0;
        uint32_t still_ = 0;
        int32_t peak_ = 0;
    };

    // 2コア目(書く)→ 1コア目(SDへ読む)の、ロック無しの1対1の列(int16)。大きさは2のべき乗
    class PcmRing {
    public:
        // storage は呼び出し側が持つ(capacity 個、2のべき乗)
        void attach(int16_t* storage, uint32_t capacity){
            buf_ = storage;
            mask_ = capacity - 1;
            head_.store(0, std::memory_order_relaxed);
            tail_.store(0, std::memory_order_relaxed);
        }
        uint32_t capacity() const { return mask_ + 1; }

        // 2コア目: 入るだけ書く。書けた数を返す(残りは捨てる)
        uint32_t push(const int16_t* src, uint32_t n){
            const uint32_t h = head_.load(std::memory_order_relaxed);
            const uint32_t t = tail_.load(std::memory_order_acquire);
            const uint32_t room = capacity() - (h - t);
            if(n > room) n = room;
            for(uint32_t i = 0; i < n; i++) buf_[(h + i) & mask_] = src[i];
            head_.store(h + n, std::memory_order_release);
            return n;
        }

        // 1コア目: 溜まっている数
        uint32_t size() const {
            return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_relaxed);
        }
        // 1コア目: 続いている所の先頭と個数(輪の終わりで切れる)
        const int16_t* peek(uint32_t& n) const {
            const uint32_t t = tail_.load(std::memory_order_relaxed);
            const uint32_t avail = head_.load(std::memory_order_acquire) - t;
            const uint32_t off = t & mask_;
            const uint32_t to_end = capacity() - off;
            n = avail < to_end ? avail : to_end;
            return buf_ + off;
        }
        void consume(uint32_t n){
            tail_.store(tail_.load(std::memory_order_relaxed) + n, std::memory_order_release);
        }

    private:
        int16_t* buf_ = nullptr;
        uint32_t mask_ = 0;
        std::atomic<uint32_t> head_{0};
        std::atomic<uint32_t> tail_{0};
    };
}
