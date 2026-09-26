#include "sound/Gb_Audio_Link.hpp"
#include "sound/Gb_Apu.hpp"

GbAudioLink::GbAudioLink(uint32_t sample_rate)
    : rate_(sample_rate ? sample_rate : 22050),
      starve_samples_((sample_rate ? sample_rate : 22050) / 20) {}   // 50ms ≒ 3フレーム

// ================================================================
// 1コア目
// ================================================================

bool GbAudioLink::push(uint32_t e, bool marker){
    const uint32_t h = this->head_.load(std::memory_order_relaxed);
    const uint32_t used = h - this->tail_.load(std::memory_order_acquire);
    //区切りのために1つは空けておく(書き込みを捨てても、フレームの区切りは崩さない)
    if(used + (marker ? 1u : 2u) > kRingSize){
        this->dropped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    this->ring_[h % kRingSize] = e;
    this->head_.store(h + 1, std::memory_order_release);
    if(marker) this->units_.fetch_add(1, std::memory_order_release);
    return true;
}

void GbAudioLink::begin(){ this->push(Pack(0, kBegin, 0), true); }
void GbAudioLink::end(){ this->push(Pack(0, kEnd, 0), true); }
void GbAudioLink::endFrame(){ this->push(Pack(kFrameCycles, kFrameEnd, 0), true); }

bool GbAudioLink::write(uint32_t cycle, uint8_t reg, uint8_t val){
    if(reg >= 0x30) return false;
    if(cycle >= kFrameCycles) cycle = kFrameCycles - 1;
    return this->push(Pack(cycle, reg, val), false);
}

// ================================================================
// 2コア目
// ================================================================

uint32_t GbAudioLink::samplePos(uint32_t cycle) const {
    return (uint32_t)(((uint64_t)cycle * this->rate_ + this->frac_) / GbApu::kCpuHz);
}

void GbAudioLink::render(GbApu& apu, int16_t* out, size_t n){
    size_t i = 0;
    while(i < n){
        uint32_t tail = this->tail_.load(std::memory_order_relaxed);

        if(!this->in_frame_){
            const uint32_t units = this->units_.load(std::memory_order_acquire);
            if(this->units_done_ == units){
                // 次のフレームがまだ来ていない。しばらくは今の音を鳴らし続け(1コア目の揺れを吸収する)、
                // 長く来なければエミュが止まったとみなして無音にする
                if(!this->active_ || this->idle_ >= this->starve_samples_) return;
                size_t k = n - i;
                if(k > this->starve_samples_ - this->idle_) k = this->starve_samples_ - this->idle_;
                apu.renderAdd(out ? out + i : nullptr, k);
                this->idle_ += (uint32_t)k;
                i += k;
                continue;
            }

            const uint32_t e = this->ring_[tail % kRingSize];
            const uint8_t reg = RegOf(e);
            if(reg == kBegin || reg == kEnd){
                apu.reset();
                if(reg == kBegin) apu.write(0x16, 0x80);    // 電源を入れた状態で始める(起動直後のGBと同じ)
                this->active_ = (reg == kBegin);
                this->frac_ = 0;
                this->idle_ = 0;
                this->tail_.store(++tail, std::memory_order_release);
                this->units_done_++;
                continue;
            }

            // フレームを始める
            const uint64_t total = (uint64_t)kFrameCycles * this->rate_ + this->frac_;
            this->frame_len_ = (uint32_t)(total / GbApu::kCpuHz);
            this->frame_pos_ = 0;
            this->in_frame_ = true;
            this->idle_ = 0;

            if(units - this->units_done_ > kMaxBacklog){
                // 追いつけていない。このフレームは音を作らずに書き込みだけ当てる
                for(;;){
                    const uint32_t x = this->ring_[tail % kRingSize];
                    tail++;
                    if(RegOf(x) == kFrameEnd) break;
                    if(this->active_) apu.write(RegOf(x), ValOf(x));
                }
                this->tail_.store(tail, std::memory_order_release);
                this->units_done_++;
                this->in_frame_ = false;
                this->frac_ = total % GbApu::kCpuHz;
                continue;
            }
        }

        // 今の位置までに書かれたものを当てる
        uint32_t next_pos = this->frame_len_;
        bool at_end = false;
        for(;;){
            const uint32_t x = this->ring_[tail % kRingSize];
            const uint8_t reg = RegOf(x);
            if(reg == kFrameEnd){ at_end = true; break; }
            const uint32_t pos = this->samplePos(CycleOf(x));
            if(pos > this->frame_pos_){ next_pos = pos < this->frame_len_ ? pos : this->frame_len_; break; }
            if(this->active_) apu.write(reg, ValOf(x));
            tail++;
        }
        this->tail_.store(tail, std::memory_order_release);

        if(at_end && this->frame_pos_ >= this->frame_len_){
            this->tail_.store(tail + 1, std::memory_order_release);
            this->units_done_++;
            this->in_frame_ = false;
            this->frac_ = ((uint64_t)kFrameCycles * this->rate_ + this->frac_) % GbApu::kCpuHz;
            continue;
        }

        size_t k = next_pos - this->frame_pos_;
        if(k > n - i) k = n - i;
        if(this->active_) apu.renderAdd(out ? out + i : nullptr, k);
        this->frame_pos_ += (uint32_t)k;
        i += k;
    }
}
