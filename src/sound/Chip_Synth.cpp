#include "sound/Chip_Synth.hpp"

using namespace ChipSynth;

Engine::Engine(uint32_t sample_rate) : rate_(sample_rate ? sample_rate : 22050) {}

void Engine::updateGain(Channel& c) const {
    c.gain = master_amp_ * c.volume / 15;
}

void Engine::setMasterVolume(uint8_t volume){
    if(volume > 100) volume = 100;
    master_ = volume;
    master_amp_ = kChannelAmplitude * volume / 100;
    for(Channel& c : ch_) updateGain(c);
}

void Engine::play(uint8_t ch, const Note& note){
    if(ch >= kChannels) return;
    Channel& c = ch_[ch];

    c.wave = (note.wave < Wave::kCount) ? note.wave : Wave::Pulse50;
    const bool noise = (c.wave == Wave::Noise || c.wave == Wave::NoiseShort);

    uint32_t f = note.freq_x16;
    if(noise){
        //ノイズは「1サンプルで何段進めるか」を16.16の固定小数で持つ(65536 = 1段)。
        //1サンプルに1段より速くは進めない(それ以上は聞こえ方が変わらない)
        if(f > rate_ * 16) f = rate_ * 16;
        c.inc = (uint32_t)(((uint64_t)f << 12) / rate_);
    }else{
        if(f > kMaxToneFreqX16(rate_)) f = kMaxToneFreqX16(rate_);
        //2^32 / 16 = 2^28(位相は2^32で1周期)
        c.inc = (uint32_t)(((uint64_t)f << 28) / rate_);
    }
    c.phase = 0;
    c.lfsr = 0x7FFF;

    c.volume = note.volume > 15 ? 15 : note.volume;
    int8_t env = note.envelope;
    if(env < -7) env = -7;
    if(env > 7) env = 7;
    c.env_dir = (env > 0) ? 1 : (env < 0 ? -1 : 0);
    const uint32_t period = (uint32_t)(env < 0 ? -env : env);
    c.env_step = period * rate_ / 64;
    c.env_count = c.env_step;

    c.infinite = (note.length_ms == 0);
    c.remaining = (uint32_t)((uint64_t)note.length_ms * rate_ / 1000);
    if(!c.infinite && c.remaining == 0) c.remaining = 1;

    updateGain(c);
    //音量0から下げる音は鳴らしても何も出ないので、最初から鳴っていない扱い
    c.active = !(c.volume == 0 && c.env_dir <= 0);
}

void Engine::stop(uint8_t ch){
    if(ch < kChannels) ch_[ch].active = false;
}

void Engine::stopAll(){
    for(Channel& c : ch_) c.active = false;
}

uint8_t Engine::activeMask() const {
    uint8_t m = 0;
    for(int i = 0; i < kChannels; i++) if(ch_[i].active) m |= (uint8_t)(1u << i);
    return m;
}

int32_t Engine::waveValue(Channel& c){
    const uint32_t p = c.phase;
    switch(c.wave){
        case Wave::Pulse12: return (p < 0x20000000u) ? 32767 : -32768;
        case Wave::Pulse25: return (p < 0x40000000u) ? 32767 : -32768;
        case Wave::Pulse50: return (p < 0x80000000u) ? 32767 : -32768;
        case Wave::Pulse75: return (p < 0xC0000000u) ? 32767 : -32768;
        case Wave::Triangle: {
            //前半で-32768→32767へ上がり、後半で下がる
            const uint32_t t = (p & 0x80000000u) ? ~p : p;   //0〜0x7FFFFFFF
            return (int32_t)(t >> 15) - 32768;
        }
        case Wave::Saw:
            return (int32_t)(p >> 16) - 32768;
        case Wave::Noise:
        case Wave::NoiseShort:
            return (c.lfsr & 1) ? -32768 : 32767;
        default:
            return 0;
    }
}

void Engine::stepEnvelope(Channel& c){
    if(c.env_dir == 0 || c.env_step == 0) return;
    if(--c.env_count > 0) return;
    c.env_count = c.env_step;
    if(c.env_dir < 0){
        if(c.volume > 0) c.volume--;
        if(c.volume == 0) c.active = false;    //消え切ったら終わり
    }else{
        if(c.volume < 15) c.volume++;
        else c.env_dir = 0;                     //上がり切ったらそのまま
    }
    updateGain(c);
}

void Engine::render(int16_t* out, size_t n){
    //何も鳴っていなければ無音を書くだけ(2コア目が空回りする時間を短くする)
    if(activeMask() == 0){
        if(out) for(size_t i = 0; i < n; i++) out[i] = 0;
        return;
    }

    for(size_t i = 0; i < n; i++){
        int32_t mix = 0;
        for(Channel& c : ch_){
            if(!c.active) continue;

            if(out) mix += (waveValue(c) * c.gain) >> 15;

            if(c.wave == Wave::Noise || c.wave == Wave::NoiseShort){
                //ノイズ: 積んだ段数が1(65536)を超えるたびにLFSRを1段進める。
                //ゲームボーイと同じく、下位2bitのXORを最上位(短い方はbit6にも)へ入れる
                c.phase += c.inc;
                if(c.phase >= 65536){
                    c.phase -= 65536;
                    const uint16_t bit = (uint16_t)((c.lfsr ^ (c.lfsr >> 1)) & 1);
                    c.lfsr = (uint16_t)((c.lfsr >> 1) | (bit << 14));
                    if(c.wave == Wave::NoiseShort) c.lfsr = (uint16_t)((c.lfsr & ~(1u << 6)) | (bit << 6));
                }
            }else{
                c.phase += c.inc;
            }

            stepEnvelope(c);
            if(!c.infinite && c.active){
                if(--c.remaining == 0) c.active = false;
            }
        }
        if(out){
            if(mix > 32767) mix = 32767;
            if(mix < -32768) mix = -32768;
            out[i] = (int16_t)mix;
        }
    }
}
