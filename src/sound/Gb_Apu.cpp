#include "sound/Gb_Apu.hpp"

#include <cstring>

namespace {
    // 0xFF10からの位置
    enum Reg : uint8_t {
        NR10 = 0x00, NR11 = 0x01, NR12 = 0x02, NR13 = 0x03, NR14 = 0x04,
        NR21 = 0x06, NR22 = 0x07, NR23 = 0x08, NR24 = 0x09,
        NR30 = 0x0A, NR31 = 0x0B, NR32 = 0x0C, NR33 = 0x0D, NR34 = 0x0E,
        NR41 = 0x10, NR42 = 0x11, NR43 = 0x12, NR44 = 0x13,
        NR50 = 0x14, NR51 = 0x15, NR52 = 0x16,
        WAVE = 0x20,
    };

    // 矩形波のデューティ。8段のうち高い段のビット(bit n = n段目)
    constexpr uint8_t kDuty[4] = { 0x80, 0x81, 0xE1, 0x7E };   // 12.5% / 25% / 50% / 75%

    // 位相の増分がこれ以上(1サンプルで半周期以上)なら、サンプル周波数では表せない高さ
    constexpr uint64_t kUltrasonic = 1ull << 31;
    // 直流を落とすコンデンサの充電の係数(実機の0.999958を4194304Hz→22050Hzへ換算)
    constexpr float kHighPass = 0.992f;
}

void GbApu::Envelope::clock(){
    if(period == 0) return;
    if(timer > 0) timer--;
    if(timer != 0) return;
    timer = period;
    if(up){ if(volume < 15) volume++; }
    else  { if(volume > 0)  volume--; }
}

GbApu::GbApu(uint32_t sample_rate) : rate_(sample_rate ? sample_rate : 22050) {
    this->reset();
    this->setMasterVolume(100);
}

void GbApu::reset(){
    memset(this->regs_, 0, sizeof(this->regs_));
    for(auto& c : this->ch_) c = Channel();
    this->power_ = false;
    this->sweep_enabled_ = false;
    this->sweep_shadow_ = 0;
    this->sweep_timer_ = 0;
    this->wave_shift_ = 4;
    this->lfsr_ = 0x7FFF;
    this->lfsr_short_ = false;
    this->noise_frac_ = 0;
    this->noise_inc_ = 0;
    this->seq_acc_ = 0;
    this->seq_step_ = 0;
    this->hp_cap_ = 0.0f;
}

void GbApu::setMasterVolume(uint8_t volume){
    if(volume > 100) volume = 100;
    // 1チャンネルは直流を落とした後で最大±(15×8×2/2)=±120 → kChannelAmplitude へ
    this->gain_ = (int32_t)(kChannelAmplitude * 256 / 120) * volume / 100;
}

uint8_t GbApu::activeMask() const {
    uint8_t m = 0;
    for(int i = 0; i < kChannels; i++) if(this->ch_[i].enabled) m |= (uint8_t)(1u << i);
    return m;
}

void GbApu::updateInc(int ch){
    Channel& c = this->ch_[ch];
    const uint32_t period = 2048u - (c.freq & 0x7FF);
    // 矩形波は1周期8段で 131072/(2048-f) Hz、波形メモリは32段で 65536/(2048-f) Hz
    const uint64_t hz_num = (ch == 2) ? 65536ull : 131072ull;
    const uint64_t inc = (hz_num << 32) / ((uint64_t)period * this->rate_);
    c.inc = (inc >= kUltrasonic) ? 0 : (uint32_t)inc;   // 0 = 高すぎて表せない
}

void GbApu::updateNoiseRate(){
    const uint8_t nr43 = this->regs_[NR43];
    const uint8_t shift = nr43 >> 4;
    const uint8_t r = nr43 & 0x07;
    this->lfsr_short_ = (nr43 & 0x08) != 0;
    if(shift >= 14){
        this->noise_inc_ = 0;           // 実機でもLFSRが進まない
        return;
    }
    // LFSRが進む速さ = 262144 / (r × 2^shift) Hz(r=0は0.5とみなす)
    const uint64_t hz = 524288ull / ((r ? 2u * r : 1u) << shift);
    this->noise_inc_ = (uint32_t)((hz << 16) / this->rate_);
}

uint16_t GbApu::sweepCalc(){
    const uint8_t shift = this->regs_[NR10] & 0x07;
    const bool negate = (this->regs_[NR10] & 0x08) != 0;
    const uint16_t delta = this->sweep_shadow_ >> shift;
    const int32_t f = negate ? (int32_t)this->sweep_shadow_ - delta : (int32_t)this->sweep_shadow_ + delta;
    if(f > 2047) this->ch_[0].enabled = false;
    return (uint16_t)(f < 0 ? 0 : f);
}

void GbApu::trigger(int ch){
    Channel& c = this->ch_[ch];
    c.enabled = c.dac;
    if(c.length == 0) c.length = (ch == 2) ? 256 : 64;
    if(ch == 2){
        c.phase = 0;
    }else{
        c.env.trigger();
    }
    if(ch == 3){
        this->lfsr_ = 0x7FFF;
        this->noise_frac_ = 0;
    }
    if(ch == 0){
        const uint8_t period = (this->regs_[NR10] >> 4) & 0x07;
        const uint8_t shift = this->regs_[NR10] & 0x07;
        this->sweep_shadow_ = c.freq;
        this->sweep_timer_ = period ? period : 8;
        this->sweep_enabled_ = (period != 0) || (shift != 0);
        if(shift) this->sweepCalc();
    }
}

void GbApu::powerOff(){
    // NR10〜NR51を消し、全チャンネルを止める(波形メモリは残る)
    memset(this->regs_, 0, NR52);
    for(auto& c : this->ch_) c = Channel();
    this->sweep_enabled_ = false;
    this->wave_shift_ = 4;
    this->power_ = false;
}

void GbApu::write(uint8_t reg, uint8_t val){
    if(reg >= 0x30) return;

    if(reg >= WAVE){
        this->regs_[reg] = val;
        return;
    }
    if(reg == NR52){
        const bool on = (val & 0x80) != 0;
        if(!on && this->power_) this->powerOff();
        else if(on && !this->power_){
            this->power_ = true;
            this->seq_step_ = 0;
        }
        this->regs_[NR52] = val & 0x80;
        return;
    }
    // 電源が切れている間は NR10〜NR51 への書き込みを受け付けない
    if(!this->power_ || reg > NR51) return;
    this->regs_[reg] = val;

    switch(reg){
        case NR11: case NR21: case NR41: {
            Channel& c = this->ch_[reg == NR11 ? 0 : (reg == NR21 ? 1 : 3)];
            c.duty = val >> 6;
            c.length = 64 - (val & 0x3F);
            break;
        }
        case NR31:
            this->ch_[2].length = 256 - val;
            break;
        case NR12: case NR22: case NR42: {
            Channel& c = this->ch_[reg == NR12 ? 0 : (reg == NR22 ? 1 : 3)];
            c.env.load(val);
            c.dac = (val & 0xF8) != 0;
            if(!c.dac) c.enabled = false;
            break;
        }
        case NR30:
            this->ch_[2].dac = (val & 0x80) != 0;
            if(!this->ch_[2].dac) this->ch_[2].enabled = false;
            break;
        case NR32: {
            static constexpr uint8_t kShift[4] = { 4, 0, 1, 2 };   // 無音 / 100% / 50% / 25%
            this->wave_shift_ = kShift[(val >> 5) & 3];
            break;
        }
        case NR43:
            this->updateNoiseRate();
            break;
        case NR13: case NR23: case NR33: {
            const int ch = (reg == NR13) ? 0 : (reg == NR23 ? 1 : 2);
            Channel& c = this->ch_[ch];
            c.freq = (uint16_t)((c.freq & 0x700) | val);
            this->updateInc(ch);
            break;
        }
        case NR14: case NR24: case NR34: case NR44: {
            const int ch = (reg == NR14) ? 0 : (reg == NR24 ? 1 : (reg == NR34 ? 2 : 3));
            Channel& c = this->ch_[ch];
            if(ch != 3){
                c.freq = (uint16_t)((c.freq & 0xFF) | ((val & 0x07) << 8));
                this->updateInc(ch);
            }
            c.length_enable = (val & 0x40) != 0;
            if(val & 0x80) this->trigger(ch);
            break;
        }
        default:
            break;  // NR10(スイープはトリガー時と刻みの時に読む)、NR50/NR51(合成の時に読む)
    }
}

void GbApu::clockLength(){
    for(auto& c : this->ch_){
        if(!c.length_enable || c.length == 0) continue;
        if(--c.length == 0) c.enabled = false;
    }
}

void GbApu::clockSweep(){
    if(this->sweep_timer_ > 0) this->sweep_timer_--;
    if(this->sweep_timer_ != 0) return;
    const uint8_t period = (this->regs_[NR10] >> 4) & 0x07;
    this->sweep_timer_ = period ? period : 8;
    if(!this->sweep_enabled_ || period == 0) return;

    const uint16_t f = this->sweepCalc();
    const uint8_t shift = this->regs_[NR10] & 0x07;
    if(f <= 2047 && shift != 0){
        this->sweep_shadow_ = f;
        this->ch_[0].freq = f;
        this->regs_[NR13] = (uint8_t)(f & 0xFF);
        this->regs_[NR14] = (uint8_t)((this->regs_[NR14] & 0xF8) | (f >> 8));
        this->updateInc(0);
        this->sweepCalc();      // もう1回計算して、次で溢れるなら今止める(実機と同じ)
    }
}

void GbApu::clockEnvelope(){
    this->ch_[0].env.clock();
    this->ch_[1].env.clock();
    this->ch_[3].env.clock();
}

void GbApu::renderAdd(int16_t* out, size_t n){
    for(size_t i = 0; i < n; i++){
        // ---- フレームシーケンサー(512Hz)----
        this->seq_acc_ += 512;
        while(this->seq_acc_ >= this->rate_){
            this->seq_acc_ -= this->rate_;
            if(this->power_){
                switch(this->seq_step_){
                    case 0: case 4: this->clockLength(); break;
                    case 2: case 6: this->clockLength(); this->clockSweep(); break;
                    case 7: this->clockEnvelope(); break;
                    default: break;
                }
            }
            this->seq_step_ = (uint8_t)((this->seq_step_ + 1) & 7);
        }

        // ---- 各チャンネルのDACへの入力(0〜15)----
        int d[kChannels] = {0, 0, 0, 0};
        for(int ch = 0; ch < 2; ch++){
            Channel& c = this->ch_[ch];
            if(!c.enabled) continue;
            if(c.inc == 0){
                d[ch] = c.env.volume / 2;  // 聞こえない高さ。平均の高さだけ出す(直流なので消える)
                continue;
            }
            d[ch] = ((kDuty[c.duty] >> (c.phase >> 29)) & 1) ? c.env.volume : 0;
            c.phase += c.inc;
        }
        {
            Channel& c = this->ch_[2];
            if(c.enabled){
                const uint32_t idx = c.phase >> 27;             // 0〜31
                const uint8_t b = this->regs_[WAVE + idx / 2];
                const uint8_t nib = (idx & 1) ? (b & 0x0F) : (b >> 4);
                d[2] = (this->wave_shift_ >= 4) ? 0 : (nib >> this->wave_shift_);
                c.phase += c.inc;
            }
        }
        {
            Channel& c = this->ch_[3];
            if(c.enabled){
                this->noise_frac_ += this->noise_inc_;
                uint32_t steps = this->noise_frac_ >> 16;
                this->noise_frac_ &= 0xFFFF;
                if(steps > 64) steps = 64;
                while(steps--){
                    const uint16_t bit = (uint16_t)((this->lfsr_ ^ (this->lfsr_ >> 1)) & 1);
                    this->lfsr_ = (uint16_t)((this->lfsr_ >> 1) | (bit << 14));
                    if(this->lfsr_short_) this->lfsr_ = (uint16_t)((this->lfsr_ & ~(1u << 6)) | (bit << 6));
                }
                d[3] = (this->lfsr_ & 1) ? 0 : c.env.volume;
            }
        }

        // ---- 左右への振り分け(NR51)と左右の音量(NR50)。モノラルなので足す ----
        int32_t mix = 0;
        if(this->power_){
            const uint8_t nr51 = this->regs_[NR51];
            const uint8_t nr50 = this->regs_[NR50];
            int32_t l = 0, r = 0;
            for(int ch = 0; ch < kChannels; ch++){
                if(nr51 & (0x10 << ch)) l += d[ch];
                if(nr51 & (0x01 << ch)) r += d[ch];
            }
            mix = l * (((nr50 >> 4) & 7) + 1) + r * ((nr50 & 7) + 1);
        }
        const float x = (float)mix;
        const float y = x - this->hp_cap_;
        this->hp_cap_ = x - y * kHighPass;
        if(!out) continue;

        const int32_t s = (int32_t)(y * (float)this->gain_) / 256 + out[i];
        out[i] = (int16_t)(s > 32767 ? 32767 : (s < -32768 ? -32768 : s));
    }
}
