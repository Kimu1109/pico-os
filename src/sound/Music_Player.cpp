#include "sound/Music_Player.hpp"
#include "sound/Note_Name.hpp"

using namespace MusicData;

namespace {
    // 1ティックの境目で1トラックが続けて読んでよい命令の数(壊れたデータで固まらないための安全網)
    constexpr int kMaxOpsPerFetch = 256;
}

bool MusicPlayer::start(ChipSynth::Engine& engine, const uint8_t* data, size_t size){
    stop(engine);
    if(!data || size < kHeaderBytes || data[0] != kMagic0 || data[1] != kMagic1 || data[2] != kVersion){
        return false;
    }
    data_ = data;
    size_ = size;
    tempo_ = ReadU16(data + 4);
    if(tempo_ < 30 || tempo_ > 300) tempo_ = 120;
    acc_ = 0;
    bool any = false;
    for(int ch = 0; ch < kChannels; ch++){
        Track& t = tracks_[ch];
        t = Track();
        const uint16_t off = ReadU16(data + 8 + ch * 2);
        if(off == 0 || off >= size) continue;
        t.active = true;
        t.pc = off;
        any = true;
    }
    playing_ = any;
    //0ティック目の出来事(最初の音符)をすぐに鳴らす
    for(int ch = 0; ch < kChannels; ch++) if(tracks_[ch].active) fetch(engine, ch);
    return true;
}

void MusicPlayer::silence(ChipSynth::Engine& engine, int ch){
    Track& t = tracks_[ch];
    if(t.sounding && !(borrowed_ & (1u << ch))) engine.stop((uint8_t)ch);
    t.sounding = false;
}

void MusicPlayer::stop(ChipSynth::Engine& engine){
    if(playing_){
        for(int ch = 0; ch < kChannels; ch++) silence(engine, ch);
    }
    playing_ = false;
    data_ = nullptr;
}

// 次の音符/休符/終わりまで命令を読む
void MusicPlayer::fetch(ChipSynth::Engine& engine, int ch){
    Track& t = tracks_[ch];
    for(int guard = 0; guard < kMaxOpsPerFetch; guard++){
        if(t.pc >= size_){ t.active = false; silence(engine, ch); return; }
        const uint8_t op = data_[t.pc++];
        switch(op){
            case Op::Note: {
                if(t.pc + 3 > size_){ t.active = false; return; }
                const uint8_t midi = data_[t.pc];
                const uint16_t len = ReadU16(data_ + t.pc + 1);
                t.pc += 3;
                t.remaining = len;
                //q: 長さの q/8 だけ鳴らす(8なら切らない)。短すぎて0になる音符も1ティックは鳴らす
                t.gate = (t.gate_q >= 8) ? 0 : (uint32_t)len * t.gate_q / 8;
                if(t.gate_q < 8 && t.gate == 0) t.gate = 1;

                ChipSynth::Note n;
                n.wave = (ChipSynth::Wave)t.wave;
                float f = NoteName::MidiToFreq(midi);
                //ノイズは音符の高さ×16を「ザー」の粗さにする(MUSIC_FORMAT.md)
                if(n.wave == ChipSynth::Wave::Noise || n.wave == ChipSynth::Wave::NoiseShort) f *= 16.0f;
                n.freq_x16 = (uint32_t)(f * 16.0f + 0.5f);
                n.volume = t.volume;
                n.envelope = t.envelope;
                n.length_ms = 0;    //止めるのはこちら(q)の仕事
                if(!(borrowed_ & (1u << ch))) engine.play((uint8_t)ch, n);
                t.sounding = true;
                if(len == 0) continue;  //長さ0の音符は無い(読み取りが弾く)が、念のため次へ
                return;
            }
            case Op::Rest: {
                if(t.pc + 2 > size_){ t.active = false; return; }
                const uint16_t len = ReadU16(data_ + t.pc);
                t.pc += 2;
                silence(engine, ch);
                t.remaining = len;
                t.gate = 0;
                if(len == 0) continue;
                return;
            }
            case Op::Wave:     t.wave = data_[t.pc++]; break;
            case Op::Volume:   t.volume = data_[t.pc++]; break;
            case Op::Envelope: t.envelope = (int8_t)data_[t.pc++]; break;
            case Op::Gate:     t.gate_q = data_[t.pc++]; break;
            case Op::Tempo: {
                const uint16_t bpm = ReadU16(data_ + t.pc);
                t.pc += 2;
                if(bpm >= 30 && bpm <= 300) tempo_ = bpm;
                break;
            }
            case Op::LoopBegin: {
                const uint8_t n = data_[t.pc++];
                if(t.depth >= kMaxLoopDepth){ t.active = false; silence(engine, ch); return; }
                t.loops[t.depth].start = t.pc;
                t.loops[t.depth].remaining = n;
                t.depth++;
                break;
            }
            case Op::LoopBreak: {
                const uint16_t target = ReadU16(data_ + t.pc);
                t.pc += 2;
                if(t.depth > 0 && t.loops[t.depth - 1].remaining <= 1){
                    t.depth--;
                    t.pc = target;
                }
                break;
            }
            case Op::LoopEnd:
                if(t.depth > 0){
                    Loop& l = t.loops[t.depth - 1];
                    if(l.remaining > 1){
                        l.remaining--;
                        t.pc = l.start;
                    }else{
                        t.depth--;
                    }
                }
                break;
            case Op::Segno:
                t.segno = t.pc;
                break;
            case Op::SaveState:
                t.saved_wave = t.wave; t.saved_volume = t.volume;
                t.saved_envelope = t.envelope; t.saved_q = t.gate_q;
                break;
            case Op::RestoreState:
                t.wave = t.saved_wave; t.volume = t.saved_volume;
                t.envelope = t.saved_envelope; t.gate_q = t.saved_q;
                break;
            case Op::End:
            default:
                if(op == Op::End && t.segno != 0){
                    t.pc = t.segno;
                    t.depth = 0;
                    break;
                }
                //終わり(または知らない命令)
                t.active = false;
                silence(engine, ch);
                return;
        }
    }
    //命令を読みすぎた(音符の無いループ等)。このトラックだけ止める
    t.active = false;
    silence(engine, ch);
}

void MusicPlayer::tick(ChipSynth::Engine& engine){
    bool any = false;
    for(int ch = 0; ch < kChannels; ch++){
        Track& t = tracks_[ch];
        if(!t.active) continue;
        if(t.gate > 0 && --t.gate == 0) silence(engine, ch);
        if(t.remaining > 0) t.remaining--;
        if(t.remaining == 0) fetch(engine, ch);
        if(t.active) any = true;
    }
    if(!any) playing_ = false;
}

uint32_t MusicPlayer::samplesToNextTick() const {
    const uint64_t threshold = (uint64_t)rate_ * 60;
    const uint64_t step = (uint64_t)tempo_ * kTicksPerQuarter;
    const uint64_t left = threshold > acc_ ? threshold - acc_ : 0;
    uint64_t s = (left + step - 1) / step;
    if(s == 0) s = 1;
    return (uint32_t)s;
}

void MusicPlayer::render(ChipSynth::Engine& engine, int16_t* out, size_t n){
    while(n > 0){
        if(!playing_){
            engine.render(out, n);
            return;
        }
        size_t k = samplesToNextTick();
        if(k > n) k = n;
        engine.render(out, k);
        if(out) out += k;
        n -= k;

        const uint64_t threshold = (uint64_t)rate_ * 60;
        acc_ += (uint64_t)k * tempo_ * kTicksPerQuarter;
        while(playing_ && acc_ >= threshold){
            acc_ -= threshold;
            tick(engine);
        }
    }
}
