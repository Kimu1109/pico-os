#pragma once

#include <cstdint>

// 再生の順番(ミュージックアプリの「順番通り / ミックス」と「リピート」)。
//
// 曲そのものは知らず、プレイリストの中の番号(0〜count-1)の並びだけを持つ。描画にもSDにも依存しないので、
// ホストテスト(script/host_test/play_queue_test.cpp)で振る舞いを確かめられる。
//
// - 順番通り: order[] は 0,1,2,… のまま。ミックス: order[] をシャッフルし、今の曲を先頭に置く
//   (ミックスへ切り替えた瞬間に今の曲が変わらないように)
// - リピート: Off = 最後まで行ったら止まる / All = 頭へ戻る(ミックスなら混ぜ直す) / One = 同じ曲を繰り返す
// - 曲が終わったとき(自動)は next(false)、利用者が「次へ」を押したら next(true)。
//   One でも「次へ」を押せば次の曲へ進む(よくある音楽アプリと同じ)
// - 確保はしない(固定長の配列)
class PlayQueue {
    public:
        static constexpr int kMax = 64;

        enum class Repeat : uint8_t { Off, All, One };

        // 曲の数を決め直し、順番を作り直す(今の位置は先頭)
        void reset(int count){
            count_ = count < 0 ? 0 : (count > kMax ? kMax : count);
            pos_ = 0;
            build(-1);
        }
        int count() const { return count_; }

        // track から鳴らし始める。ミックスなら track を先頭にして残りを混ぜ直す
        void start(int track){
            if(track < 0 || track >= count_) return;
            build(track);
            pos_ = posOf(track);
        }

        // 今の曲(無ければ-1)
        int current() const { return count_ > 0 ? order_[pos_] : -1; }
        // 並びの中の位置(0始まり。「3/12曲目」の表示用)
        int position() const { return pos_; }

        // 次の曲へ進めてその番号を返す。終わり(リピートOffで最後の曲の次)なら-1(位置は動かさない)
        int next(bool by_user){
            if(count_ <= 0) return -1;
            if(repeat_ == Repeat::One && !by_user) return current();
            if(pos_ + 1 < count_){
                pos_++;
                return current();
            }
            if(repeat_ == Repeat::Off) return -1;
            //一巡した。ミックスなら混ぜ直す(直前の曲がすぐ続かないよう、先頭には置かない)
            if(shuffle_ && count_ > 1){
                const int last = current();
                build(-1);
                if(order_[0] == last){
                    const int j = 1 + (int)(rand() % (uint32_t)(count_ - 1));
                    order_[0] = order_[j];
                    order_[j] = (uint8_t)last;
                }
            }
            pos_ = 0;
            return current();
        }

        // 前の曲へ戻してその番号を返す。先頭ならリピートAllのときだけ最後へ回り、それ以外は先頭のまま
        int prev(){
            if(count_ <= 0) return -1;
            if(pos_ > 0) pos_--;
            else if(repeat_ == Repeat::All) pos_ = count_ - 1;
            return current();
        }

        bool shuffle() const { return shuffle_; }
        // ミックスの入り切り。今の曲は変えずに、残りの並びだけを作り直す
        void setShuffle(bool on){
            if(on == shuffle_) return;
            shuffle_ = on;
            const int cur = current();
            build(cur);
            pos_ = cur >= 0 ? posOf(cur) : 0;
        }

        Repeat repeat() const { return repeat_; }
        void setRepeat(Repeat r){ repeat_ = r; }
        // Off → All → One → Off
        Repeat cycleRepeat(){
            repeat_ = repeat_ == Repeat::Off ? Repeat::All : (repeat_ == Repeat::All ? Repeat::One : Repeat::Off);
            return repeat_;
        }

        // 乱数の種(0は使わない)
        void seed(uint32_t s){ rng_ = s ? s : 0x9E3779B9u; }

        // 並びそのもの(テスト用)
        int orderAt(int i) const { return (i >= 0 && i < count_) ? order_[i] : -1; }

    private:
        // 順番を作る。ミックスなら first(-1で指定なし)を先頭に置いて残りを混ぜる
        void build(int first){
            for(int i = 0; i < count_; i++) order_[i] = (uint8_t)i;
            if(!shuffle_ || count_ <= 1) return;
            int begin = 0;
            if(first >= 0 && first < count_){
                order_[first] = 0;
                order_[0] = (uint8_t)first;
                begin = 1;
            }
            //Fisher-Yates
            for(int i = count_ - 1; i > begin; i--){
                const int j = begin + (int)(rand() % (uint32_t)(i - begin + 1));
                const uint8_t t = order_[i];
                order_[i] = order_[j];
                order_[j] = t;
            }
        }
        int posOf(int track) const {
            for(int i = 0; i < count_; i++) if(order_[i] == track) return i;
            return 0;
        }
        // xorshift32
        uint32_t rand(){
            rng_ ^= rng_ << 13;
            rng_ ^= rng_ >> 17;
            rng_ ^= rng_ << 5;
            return rng_;
        }

        uint8_t order_[kMax] = {};
        int count_ = 0;
        int pos_ = 0;
        bool shuffle_ = false;
        Repeat repeat_ = Repeat::Off;
        uint32_t rng_ = 0x9E3779B9u;
};
