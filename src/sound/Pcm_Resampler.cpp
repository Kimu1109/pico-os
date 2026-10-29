#include "sound/Pcm_Resampler.hpp"

#include <cmath>

void PcmResampler::setup(uint32_t in_rate, uint32_t out_rate, uint8_t lanes, Source source, void* ctx){
    if(out_rate == 0) out_rate = 22050;
    this->source_ = source;
    this->ctx_ = ctx;
    this->lanes_ = lanes == 2 ? 2 : 1;
    this->step_ = (uint32_t)(((uint64_t)in_rate << 16) / out_rate);
    if(this->step_ == 0) this->step_ = 1;
    this->designFilter(in_rate, out_rate);
    this->reset();
}

void PcmResampler::reset(){
    this->frac_ = 0;
    this->primed_ = false;
    this->ending_ = false;
    this->filt_ready_ = false;
    this->finished_ = false;
}

// 下げるときの低域通過フィルタの係数を作る。切れ目は out_rate の半分の9割(22050Hzなら約9.9kHzで-6dB)。
// タップ数は比の8倍ほど(44.1kHz→22050Hzで33)。ハミング窓なので、通す帯域と削る帯域の間は約 3.3/タップ数
void PcmResampler::designFilter(uint32_t in_rate, uint32_t out_rate){
    this->half_ = 0;
    if(in_rate <= out_rate) return;

    const float ratio = (float)in_rate / (float)out_rate;
    int half = (int)ceilf(ratio * 8.0f);
    if(half > kMaxHalfTaps) half = kMaxHalfTaps;
    const float fc = 0.45f / ratio;     //元の周波数で数えた切れ目(1サンプルあたりの周期)
    const float kPi = 3.14159265f;

    float h[kMaxHalfTaps + 1];
    float total = 0;
    for(int k = 0; k <= half; k++){
        const float x = 2.0f * kPi * fc * (float)k;
        const float sinc = k == 0 ? 2.0f * fc : sinf(x) / (kPi * (float)k);
        const float window = 0.54f + 0.46f * cosf(kPi * (float)k / (float)(half + 1));
        h[k] = sinc * window;
        total += k == 0 ? h[k] : 2.0f * h[k];
    }
    //Q15へ。丸めの誤差は真ん中へ寄せ、合計をちょうど32768にする(一定の値が変わらないように)
    int32_t sum = 0;
    for(int k = 1; k <= half; k++){
        this->taps_[k] = (int16_t)lroundf(h[k] / total * 32768.0f);
        sum += 2 * this->taps_[k];
    }
    this->taps_[0] = (int16_t)(32768 - sum);
    this->half_ = (uint16_t)half;
}

void PcmResampler::pushHistory(const Frame& x){
    const uint16_t n = 2 * this->half_ + 1;
    this->hist_[this->hist_pos_] = x;
    this->hist_[this->hist_pos_ + n] = x;
    if(++this->hist_pos_ >= n) this->hist_pos_ = 0;
}

int16_t PcmResampler::filterOut(int lane) const {
    //hist_[hist_pos_] が一番古く、そこから n 個が古い順に並んでいる。左右対称なので両端から足し合わせる
    const Frame* p = this->hist_ + this->hist_pos_;
    int32_t acc = (int32_t)this->taps_[0] * p[this->half_].v[lane];
    for(uint16_t k = 1; k <= this->half_; k++){
        acc += (int32_t)this->taps_[k] * ((int32_t)p[this->half_ - k].v[lane] + p[this->half_ + k].v[lane]);
    }
    acc = (acc + 16384) >> 15;
    if(acc > 32767) acc = 32767;
    if(acc < -32768) acc = -32768;
    return (int16_t)acc;
}

// 元のサンプルを1つ(フィルタを掛けるならその後の値を)取り出す
bool PcmResampler::nextSource(Frame& v){
    if(this->half_ == 0) return this->pull(v);

    Frame x;
    if(!this->filt_ready_){
        //頭: 履歴を最初の値で埋め、その後 half_ 個を読んで最初の値を真ん中に置く
        if(!this->pull(x)) return false;
        this->hist_pos_ = 0;
        for(uint16_t i = 0; i < 2 * this->half_ + 1; i++) this->pushHistory(x);
        this->last_raw_ = x;
        this->flush_left_ = this->half_;
        for(uint16_t i = 0; i < this->half_; i++){
            if(this->pull(x)) this->last_raw_ = x;
            else this->flush_left_--;       //短いファイルは出し切りの分を先に使う
            this->pushHistory(this->last_raw_);
        }
        this->filt_ready_ = true;
        for(int l = 0; l < this->lanes_; l++) v.v[l] = this->filterOut(l);
        v.v[1] = this->lanes_ == 2 ? v.v[1] : v.v[0];
        return true;
    }
    if(this->pull(x)) this->last_raw_ = x;
    else if(this->flush_left_ > 0) this->flush_left_--;    //終わり: 最後の値で残りを出し切る
    else return false;
    this->pushHistory(this->last_raw_);
    for(int l = 0; l < this->lanes_; l++) v.v[l] = this->filterOut(l);
    v.v[1] = this->lanes_ == 2 ? v.v[1] : v.v[0];
    return true;
}

size_t PcmResampler::read(int16_t* out, size_t max){
    if(this->finished_) return 0;

    if(!this->primed_){
        if(!this->nextSource(this->s0_)){ this->finished_ = true; return 0; }
        if(!this->nextSource(this->s1_)){
            this->s1_ = this->s0_;
            this->ending_ = true;
        }
        this->primed_ = true;
    }

    const int lanes = this->lanes_;
    size_t n = 0;
    while(n < max){
        while(this->frac_ >= 0x10000){
            this->frac_ -= 0x10000;
            this->s0_ = this->s1_;
            if(!this->nextSource(this->s1_)){
                //最後の1つも出してから終える(同じ周波数なら入れたのと同じ数だけ出る)
                if(this->ending_){
                    this->finished_ = true;
                    return n;
                }
                this->ending_ = true;
                this->s1_ = this->s0_;
            }
        }
        //差は最大65535、frac>>1 は最大32767 なので積は int32 に収まる
        for(int l = 0; l < lanes; l++){
            const int32_t d = (int32_t)this->s1_.v[l] - this->s0_.v[l];
            out[n * lanes + l] = (int16_t)(this->s0_.v[l] + ((d * (int32_t)(this->frac_ >> 1)) >> 15));
        }
        n++;
        this->frac_ += this->step_;
    }
    return n;
}
