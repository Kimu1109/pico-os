#include "sound/Wav_Stream.hpp"

static_assert((WavStream::kRingSamples & (WavStream::kRingSamples - 1)) == 0, "kRingSamples は2のべき乗");

// ================================================================
// 1コア目
// ================================================================

uint32_t WavStream::effectiveTail() const {
    const uint32_t t = this->tail_.load(std::memory_order_acquire);
    const uint32_t f = this->flush_to_.load(std::memory_order_relaxed);   //書くのは自分
    return ((int32_t)(f - t) > 0) ? f : t;
}

uint32_t WavStream::freeSpace() const {
    const uint32_t used = this->head_.load(std::memory_order_relaxed) - this->effectiveTail();
    return kRingSamples - used;
}

size_t WavStream::push(const int16_t* src, size_t n){
    const uint32_t free = this->freeSpace();
    if(n > free) n = free;
    uint32_t h = this->head_.load(std::memory_order_relaxed);
    for(size_t i = 0; i < n; i++) this->ring_[(h + i) % kRingSamples] = src[i];
    this->head_.store(h + (uint32_t)n, std::memory_order_release);
    return n;
}

void WavStream::flush(){
    //「ここまで捨てて」を書いてから世代を進める(2コア目は世代を見てから位置を読む)
    this->flush_to_.store(this->head_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    this->flush_gen_.fetch_add(1, std::memory_order_release);
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
    const uint32_t f = this->flush_to_.load(std::memory_order_relaxed);
    const uint32_t t = this->tail_.load(std::memory_order_relaxed);
    if((int32_t)(f - t) > 0) this->tail_.store(f, std::memory_order_release);
    this->starving_ = false;
}

bool WavStream::hasData(){
    this->applyFlush();
    return this->head_.load(std::memory_order_acquire) != this->tail_.load(std::memory_order_relaxed);
}

void WavStream::renderAdd(int16_t* out, size_t n, uint8_t master){
    this->applyFlush();
    const uint32_t t = this->tail_.load(std::memory_order_relaxed);
    const uint32_t avail = this->head_.load(std::memory_order_acquire) - t;
    const size_t k = n < avail ? n : avail;

    if(out && k > 0){
        //音量は 0〜100 × 0〜100 = 0〜10000
        const int32_t gain = (int32_t)this->volume_.load(std::memory_order_acquire) * master;
        for(size_t i = 0; i < k; i++){
            int32_t v = out[i] + this->ring_[(t + i) % kRingSamples] * gain / 10000;
            if(v > 32767) v = 32767;
            if(v < -32768) v = -32768;
            out[i] = (int16_t)v;
        }
    }
    this->tail_.store(t + (uint32_t)k, std::memory_order_release);

    //積み続けるつもりなのに足りなかった = 1コア目の読み込みが間に合わなかった。
    //鳴らせない間(out==nullptr)は聞こえる途切れにならないので数えない
    if(!out) return;
    if(k < n && this->feeding_.load(std::memory_order_acquire)){
        if(!this->starving_) this->underruns_.fetch_add(1, std::memory_order_relaxed);
        this->starving_ = true;
    }else if(k == n){
        this->starving_ = false;
    }
}
