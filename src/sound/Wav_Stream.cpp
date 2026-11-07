#include "sound/Wav_Stream.hpp"
#include "config/Hardware_Config.hpp"

#include <cstdint>

static_assert(WavStream::kWrap % WavStream::kRingSamples == 0, "kWrap は kRingSamples の倍数(添字を % kRingSamples で引くため)");

// ================================================================
// 1コア目
// ================================================================

uint32_t WavStream::effectiveTail() const {
    const uint32_t t = this->tail_.load(std::memory_order_acquire);
    const uint32_t f = this->flush_to_.load(std::memory_order_relaxed);   //書くのは自分
    if(Ahead(f, t)) return f;
    //2コア目が捨て終えて追い越した(世代は変えないので、2コア目が捨て直すことは無い)
    if(f != t) this->flush_to_.store(t, std::memory_order_relaxed);
    return t;
}

uint32_t WavStream::freeSpace() const {
    const uint32_t used = Dist(this->head_.load(std::memory_order_relaxed), this->effectiveTail());
    return kRingSamples - used;
}

size_t WavStream::push(const PcmSample* src, size_t n){
    const uint32_t free = this->freeSpace();
    if(n > free) n = free;
    if(this->channels() == 2) n &= ~(size_t)1;     //ステレオは左右の組で積む
    uint32_t h = this->head_.load(std::memory_order_relaxed);
    for(size_t i = 0; i < n; i++) this->ring_[(h + i) % kRingSamples] = src[i];
    this->head_.store(Add(h, (uint32_t)n), std::memory_order_release);
    return n;
}

void WavStream::flush(){
    //「ここまで捨てて」を書いてから世代を進める(2コア目は世代を見てから位置を読む)
    this->flush_to_.store(this->head_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    this->flush_gen_.fetch_add(1, std::memory_order_release);
}

void WavStream::setStereo(bool stereo){
    const uint8_t c = stereo ? 2 : 1;
    this->chs_.store(c, std::memory_order_relaxed);
    this->chs_pending_.store(c, std::memory_order_release);
    this->flush();      //2コア目が次に読むとき、捨てるのと同時に形を取り込む
}

uint32_t WavStream::bufferedSamples() const {
    return Dist(this->head_.load(std::memory_order_relaxed), this->effectiveTail()) / this->channels();
}

bool WavStream::buffered() const {
    return this->head_.load(std::memory_order_relaxed) != this->effectiveTail();
}

// ================================================================
// 2コア目
// ================================================================

void WavStream::applyFlush(){
    const uint32_t gen = this->flush_gen_.load(std::memory_order_acquire);
    if(gen == this->seen_gen_) return;
    this->seen_gen_ = gen;
    this->rd_chs_ = this->chs_pending_.load(std::memory_order_acquire);
    const uint32_t f = this->flush_to_.load(std::memory_order_relaxed);
    const uint32_t t = this->tail_.load(std::memory_order_relaxed);
    if(Ahead(f, t)) this->tail_.store(f, std::memory_order_release);
    this->starving_ = false;
}

bool WavStream::hasData(){
    this->applyFlush();
    //一時停止中は鳴らすものが無い扱い(省電力でI2Sを止めてよい)
    if(this->paused_.load(std::memory_order_acquire)) return false;
    return this->head_.load(std::memory_order_acquire) != this->tail_.load(std::memory_order_relaxed);
}

size_t WavStream::takeable(size_t n, uint32_t& t){
    this->applyFlush();
    //一時停止中は取り出さない(時間も進めない。再開した位置から続く)
    if(this->paused_.load(std::memory_order_acquire)) return SIZE_MAX;
    t = this->tail_.load(std::memory_order_relaxed);
    const uint32_t avail = Dist(this->head_.load(std::memory_order_acquire), t) / this->rd_chs_;   //フレーム
    return n < avail ? n : avail;
}

void WavStream::taken(uint32_t t, size_t k, size_t n, bool has_out){
    this->tail_.store(Add(t, (uint32_t)(k * this->rd_chs_)), std::memory_order_release);
    //積み続けるつもりなのに足りなかった = 1コア目の読み込みが間に合わなかった。
    //鳴らせない間(out==nullptr)は聞こえる途切れにならないので数えない
    if(!has_out) return;
    if(k < n && this->feeding_.load(std::memory_order_acquire)){
        if(!this->starving_) this->underruns_.fetch_add(1, std::memory_order_relaxed);
        this->starving_ = true;
    }else if(k == n){
        this->starving_ = false;
    }
}

void WavStream::renderAdd(int16_t* out, size_t n, uint8_t master, bool stereo){
    uint32_t t = 0;
    const size_t k = this->takeable(n, t);
    if(k == SIZE_MAX) return;
    const uint32_t chs = this->rd_chs_;

    if(out && k > 0){
        //音量は 0〜100 × 0〜100 = 0〜10000
        const int32_t gain = (int32_t)this->volume_.load(std::memory_order_acquire) * master;
        for(size_t i = 0; i < k; i++){
            int32_t w[2];
            if(chs == 2){
                w[0] = this->ring_[(t + i * 2) % kRingSamples] * gain / 10000;
                w[1] = this->ring_[(t + i * 2 + 1) % kRingSamples] * gain / 10000;
            }else{
                w[0] = w[1] = this->ring_[(t + i) % kRingSamples] * gain / 10000;
            }
            if(stereo){
                for(int c = 0; c < 2; c++){
                    int32_t v = out[i * 2 + c] + w[c];
                    if(v > 32767) v = 32767;
                    if(v < -32768) v = -32768;
                    out[i * 2 + c] = (int16_t)v;
                }
            }else{
                int32_t v = out[i] + (w[0] + w[1]) / 2;
                if(v > 32767) v = 32767;
                if(v < -32768) v = -32768;
                out[i] = (int16_t)v;
            }
        }
    }
    this->taken(t, k, n, out != nullptr);
}

#if PICO_HW_SOUND_24BIT
void WavStream::renderAddWide(int32_t* out, size_t n, uint8_t master){
    uint32_t t = 0;
    const size_t k = this->takeable(n, t);
    if(k == SIZE_MAX) return;
    const uint32_t chs = this->rd_chs_;
    if(out && k > 0){
        //24bitの桁×10000 は32bitを超えるので64bitで掛ける
        const int64_t gain = (int64_t)this->volume_.load(std::memory_order_acquire) * master;
        for(size_t i = 0; i < k; i++){
            int64_t w[2];
            if(chs == 2){
                w[0] = this->ring_[(t + i * 2) % kRingSamples] * gain / 10000;
                w[1] = this->ring_[(t + i * 2 + 1) % kRingSamples] * gain / 10000;
            }else{
                w[0] = w[1] = this->ring_[(t + i) % kRingSamples] * gain / 10000;
            }
            for(int c = 0; c < 2; c++) out[i * 2 + c] = PcmWide::Clamp24((int64_t)out[i * 2 + c] + w[c]);
        }
    }
    this->taken(t, k, n, out != nullptr);
}
#endif
