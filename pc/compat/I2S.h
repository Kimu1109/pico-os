// arduino-picoのI2Sライブラリの代替(PC/Webビルド用)。SDLの音声出力へ流す。
//
// src/functions/Sound_Functions.cpp が使う分(setBCLK/setDATA/setBitsPerSample/setBuffers/
// begin/end/write(int32_t, bool))だけを持つ。1ワード = 上位16bitが左、下位16bitが右。
//
// アンプの検出(AUDIO_DETECTのピン)もここで引き受ける: PicoPcGpio::read_hook を差し込み、
// 「アンプが刺さっている」= 音声デバイスが開ける、として LOW を返す。
// 狙って状態を作りたいときは /sys/sound.cfg の pc-sound-state か環境変数で決める
// (環境変数が優先。Webは ?sound=disconnected):
//
//   pc-sound-state = auto | connected | disconnected
//     auto         … SDLで音声デバイスを開けたら接続(既定)
//     connected    … 常に接続(開けなければbegin()が失敗する)
//     disconnected … 常に未接続(アンプを外した状態の確認用)
//
//   PICOOS_SOUND_STATE=disconnected ./pc/build/picoos_pc
//
// ヘッドレス(SDL_VIDEODRIVER=offscreen)で音の中身を確かめるなら SDL_AUDIODRIVER=disk で
// sdlaudio.raw へ書き出せる(SDL_DISKAUDIOFILE でファイル名を変えられる)。
//
// 読み取り側(Sound_Functions)と書き込み側(SDLの音声スレッド/Webではメインスレッド)の間は
// 1対1のリングバッファで、ロックを取らない。
//
// **入力(INPUTで作ったもの。Mic_Functions のマイク)** は、実機のSPH0645と同じ形のワード
// (32bitの左右交互。値は上位18bit、右は0)を、begin() からの経過時間の分だけ返す。中身は環境変数で選ぶ:
//
//   PICOOS_MIC = tone    … 440Hzの試験音(-30dBFSほど)+少しの雑音+直流のずれ(既定)
//                silence … 雑音だけ(無音の部屋)
//                none    … 全部0(マイクがつながっていない状態。「信号なし」の確認用)
//                sdl     … PCのマイク(SDLの録音デバイス。開けなければ tone)
//
// 読まずに溜まった分がバッファ(setBuffers)を超えると、古い分を捨てて getOverflow() を立てる(実機のDMAと同じ)。
#pragma once

#if __has_include(<SDL2/SDL.h>)
    #include <SDL2/SDL.h>
#else
    #include <SDL.h>
#endif

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "Arduino.h"
#include "SdFat.h"      // PicoOsSdHost::root(sound.cfg を読むため)
#include "consts.hpp"   // AUDIO_DETECT

namespace PicoPcAudio {

    enum class Mode { Auto, Connected, Disconnected };

    inline Mode ParseMode(const char* v, Mode fallback){
        if(strcmp(v, "auto") == 0)          return Mode::Auto;
        if(strcmp(v, "connected") == 0)     return Mode::Connected;
        if(strcmp(v, "disconnected") == 0)  return Mode::Disconnected;
        printf("[PC] pc-sound-state の値が不明です: %s (autoとして扱います)\n", v);
        return fallback;
    }

    // sound.cfg の pc-sound-state を素のfopenで読む(compat/WiFi.h と同じ理由で自前)
    inline Mode LoadMode(){
        Mode mode = Mode::Auto;
        const std::string path = PicoOsSdHost::root + "/sys/sound.cfg";
        if(FILE* fp = fopen(path.c_str(), "rb")){
            char line[256];
            while(fgets(line, sizeof(line), fp)){
                char* p = line;
                while(*p == ' ' || *p == '\t') p++;
                if(*p == '#') continue;
                char* eq = strchr(p, '=');
                if(!eq) continue;
                *eq = '\0';
                for(char* t = eq - 1; t >= p && (*t == ' ' || *t == '\t'); t--) *t = '\0';
                char* val = eq + 1;
                while(*val == ' ' || *val == '\t') val++;
                for(size_t n = strlen(val); n > 0; n--){
                    const char c = val[n - 1];
                    if(c == '\r' || c == '\n' || c == ' ' || c == '\t') val[n - 1] = '\0';
                    else break;
                }
                if(strcmp(p, "pc-sound-state") == 0) mode = ParseMode(val, mode);
            }
            fclose(fp);
        }
        if(const char* v = getenv("PICOOS_SOUND_STATE")) mode = ParseMode(v, mode);
        return mode;
    }

    inline SDL_AudioSpec DesiredSpec(int rate){
        SDL_AudioSpec want{};
        want.freq = rate;
        want.format = AUDIO_S16SYS;
        want.channels = 2;
        want.samples = 512;     //リング(実機と同じ1024サンプル)より小さく取る
        return want;
    }

    // 音声デバイスを開けるか(開けたらすぐ閉じる)
    inline bool ProbeDevice(){
        if(SDL_InitSubSystem(SDL_INIT_AUDIO) != 0){
            printf("[PC] 音声: SDLの音声を初期化できません(%s)。アンプ未接続として扱います\n", SDL_GetError());
            return false;
        }
        SDL_AudioSpec want = DesiredSpec(44100);
        SDL_AudioSpec have{};
        const SDL_AudioDeviceID dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
        if(dev == 0){
            printf("[PC] 音声: 音声デバイスを開けません(%s)。アンプ未接続として扱います\n", SDL_GetError());
            return false;
        }
        SDL_CloseAudioDevice(dev);
        printf("[PC] 音声: %s で出力します\n", SDL_GetCurrentAudioDriver());
        return true;
    }

    // 「アンプが刺さっているか」は起動後に1回だけ決める(実機のように抜き差しはしない)
    inline bool AmpConnected(){
        static const bool connected = [](){
            switch(LoadMode()){
                case Mode::Connected:    return true;
                case Mode::Disconnected: return false;
                case Mode::Auto:         break;
            }
            return ProbeDevice();
        }();
        return connected;
    }

    inline int ReadHook(int pin){
        if(pin != AUDIO_DETECT) return -1;
        return AmpConnected() ? LOW : HIGH;
    }

    // このヘッダを取り込んだ時点(=Sound_Functions.cppの静的初期化)でフックを差し込む
    inline const bool hook_installed = (PicoPcGpio::read_hook = &ReadHook, true);
}

class I2S {
public:
    explicit I2S(int direction = OUTPUT, int /*bclk*/ = 26, int /*data*/ = 28) : input_(direction == INPUT){}
    // 終了時(SDLが先に片付いている可能性がある)にSDLを触らないよう、デストラクタでは何もしない

    bool setBCLK(int){ return true; }
    bool setDATA(int){ return true; }
    bool setDIN(int){ return true; }
    bool setBitsPerSample(int bps){ return input_ ? bps == 32 : bps == 16; }
    bool setBuffers(size_t buffers, size_t words, int32_t = 0){
        capacity_ = buffers * words;
#if defined(__EMSCRIPTEN__)
        //Webは2コア目の代わりにフレームごと(約16ms、ぶれあり)にしか書けないので、
        //実機より多めに溜めて途切れにくくする(その分、音が出るまで少し遅れる)
        //(44100Hzで1フレーム約735サンプル。ぶれを見て4096=約93ms)
        if(capacity_ < 4096) capacity_ = 4096;
#endif
        return capacity_ > 0;
    }

    bool begin(long rate){
        if(input_) return beginInput(rate);
        if(dev_ != 0) return true;
        if(SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return false;

        //リングは1つ空けて満杯を見分けるので+1
        ring_.assign(capacity_ + 1, 0);
        head_.store(0);
        tail_.store(0);

        SDL_AudioSpec want = PicoPcAudio::DesiredSpec((int)rate);
        want.callback = &I2S::Callback;
        want.userdata = this;
        SDL_AudioSpec have{};
        //周波数や形式が違うデバイスでも、SDLが変換するので要求どおりで受け取る
        dev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
        if(dev_ == 0){
            printf("[PC] 音声: 音声デバイスを開けません(%s)\n", SDL_GetError());
            return false;
        }
        SDL_PauseAudioDevice(dev_, 0);
        return true;
    }

    bool end(){
        in_running_ = false;
        if(cap_dev_ != 0){
            SDL_CloseAudioDevice(cap_dev_);
            cap_dev_ = 0;
        }
        if(dev_ != 0){
            SDL_CloseAudioDevice(dev_);     //コールバックが終わるまで待ってから戻る
            dev_ = 0;
        }
        ring_.clear();
        return true;
    }

    // 1ワード書く。満杯なら0(syncは実機と違い常に待たない。Sound_Functionsはfalseしか使わない)
    size_t write(int32_t val, bool /*sync*/){
        if(dev_ == 0 || ring_.empty()) return 0;
        const size_t h = head_.load(std::memory_order_relaxed);
        const size_t next = (h + 1) % ring_.size();
        if(next == tail_.load(std::memory_order_acquire)) return 0;
        ring_[h] = (uint32_t)val;
        head_.store(next, std::memory_order_release);
        return 4;
    }

    // ===== 入力 =====

    int available(){
        if(!in_running_) return 0;
        return (int)(pendingWords() * 4);
    }

    // 読めるだけ読む(待たない)。読んだバイト数
    size_t read(uint8_t* buf, size_t size){
        if(!in_running_ || (size & 3)) return 0;
        size_t n = size / 4;
        const uint64_t have = pendingWords();
        if(n > have) n = (size_t)have;
        for(size_t i = 0; i < n; i++){
            const uint32_t w = nextWord();
            memcpy(buf + i * 4, &w, 4);
        }
        return n * 4;
    }

    bool getOverflow(){ return overflow_.exchange(false); }

private:
    enum class MicSource { Tone, Silence, None, Sdl };

    bool beginInput(long rate){
        in_rate_ = rate > 0 ? (uint32_t)rate : 48000;
        in_words_done_ = 0;
        in_phase_ = 0;
        in_noise_ = 12345;
        overflow_.store(false);
        mic_src_ = MicSource::Tone;
        if(const char* v = getenv("PICOOS_MIC")){
            if(strcmp(v, "silence") == 0) mic_src_ = MicSource::Silence;
            else if(strcmp(v, "none") == 0) mic_src_ = MicSource::None;
            else if(strcmp(v, "sdl") == 0) mic_src_ = MicSource::Sdl;
            else if(strcmp(v, "tone") != 0) printf("[PC] PICOOS_MIC の値が不明です: %s (toneとして扱います)\n", v);
        }
        if(mic_src_ == MicSource::Sdl && !openCapture()) mic_src_ = MicSource::Tone;
        in_start_ = std::chrono::steady_clock::now();
        in_running_ = true;
        return true;
    }

    bool openCapture(){
        if(cap_dev_ != 0) return true;
        if(SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return false;
        cap_ring_.assign(1u << 15, 0);
        cap_head_.store(0);
        cap_tail_.store(0);
        SDL_AudioSpec want{};
        want.freq = (int)in_rate_;
        want.format = AUDIO_S16SYS;
        want.channels = 1;
        want.samples = 512;
        want.callback = &I2S::CaptureCallback;
        want.userdata = this;
        SDL_AudioSpec have{};
        cap_dev_ = SDL_OpenAudioDevice(nullptr, 1, &want, &have, 0);
        if(cap_dev_ == 0){
            printf("[PC] マイク: 録音デバイスを開けません(%s)。試験音にします\n", SDL_GetError());
            return false;
        }
        printf("[PC] マイク: PCの録音デバイスから受けます\n");
        SDL_PauseAudioDevice(cap_dev_, 0);
        return true;
    }

    static void CaptureCallback(void* userdata, Uint8* stream, int len){
        I2S* self = static_cast<I2S*>(userdata);
        const int16_t* in = reinterpret_cast<const int16_t*>(stream);
        const int n = len / 2;
        const uint32_t mask = (uint32_t)self->cap_ring_.size() - 1;
        uint32_t h = self->cap_head_.load(std::memory_order_relaxed);
        const uint32_t t = self->cap_tail_.load(std::memory_order_acquire);
        for(int i = 0; i < n; i++){
            if(h - t >= self->cap_ring_.size()){ self->overflow_.store(true); break; }
            self->cap_ring_[h & mask] = in[i];
            h++;
        }
        self->cap_head_.store(h, std::memory_order_release);
    }

    // まだ読んでいないワード数。溜まりすぎていたら古い分を捨てる(実機のDMAの溢れ)
    uint64_t pendingWords(){
        if(mic_src_ == MicSource::Sdl){
            const uint32_t frames = cap_head_.load(std::memory_order_acquire) - cap_tail_.load(std::memory_order_relaxed);
            return (uint64_t)frames * 2 + (in_words_done_ & 1);   //左を読んで右がまだなら1つ多い
        }
        const auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - in_start_).count();
        const uint64_t due = (uint64_t)us * in_rate_ / 1000000 * 2;
        if(due > in_words_done_ + capacity_){
            const uint64_t skip = (due - capacity_ - in_words_done_) & ~(uint64_t)1;
            for(uint64_t i = 0; i < skip / 2; i++) synthFrame();     //位相を進める
            in_words_done_ += skip;
            overflow_.store(true);
        }
        return due > in_words_done_ ? due - in_words_done_ : 0;
    }

    int32_t synthFrame(){
        in_noise_ = in_noise_ * 1103515245u + 12345u;
        const int32_t noise = (int32_t)((in_noise_ >> 16) & 15) - 8;     //18bitで±8
        int32_t v = noise - 2000;                                       //SPH0645らしい直流のずれ
        if(mic_src_ == MicSource::Tone){
            v += (int32_t)lround(4000.0 * sin(in_phase_));              //18bitの約-30dBFS
            in_phase_ += 2.0 * 3.14159265358979 * 440.0 / in_rate_;
            if(in_phase_ > 2.0 * 3.14159265358979) in_phase_ -= 2.0 * 3.14159265358979;
        }
        return v;
    }

    uint32_t nextWord(){
        const bool right = (in_words_done_ & 1) != 0;
        in_words_done_++;
        if(right) return 0;     //SELをGNDにしたマイクは右を出さない
        switch(mic_src_){
            case MicSource::None: return 0;
            case MicSource::Sdl: {
                const uint32_t t = cap_tail_.load(std::memory_order_relaxed);
                const int16_t s = cap_ring_[t & (uint32_t)(cap_ring_.size() - 1)];
                cap_tail_.store(t + 1, std::memory_order_release);
                return (uint32_t)((int32_t)s * 65536);
            }
            default: break;
        }
        return (uint32_t)(synthFrame() * 16384);
    }

    static void Callback(void* userdata, Uint8* stream, int len){
        I2S* self = static_cast<I2S*>(userdata);
        int16_t* out = reinterpret_cast<int16_t*>(stream);
        const int frames = len / (int)(sizeof(int16_t) * 2);
        size_t t = self->tail_.load(std::memory_order_relaxed);
        const size_t h = self->head_.load(std::memory_order_acquire);
        for(int i = 0; i < frames; i++){
            if(t == h){
                //足りなければ無音(実機のI2Sも同じく無音を流す)
                out[i * 2] = 0;
                out[i * 2 + 1] = 0;
                continue;
            }
            const uint32_t w = self->ring_[t];
            out[i * 2]     = (int16_t)(w >> 16);
            out[i * 2 + 1] = (int16_t)(w & 0xFFFF);
            t = (t + 1) % self->ring_.size();
        }
        self->tail_.store(t, std::memory_order_release);
    }

    bool input_ = false;
    // 入力
    bool in_running_ = false;
    uint32_t in_rate_ = 48000;
    uint64_t in_words_done_ = 0;
    double in_phase_ = 0;
    uint32_t in_noise_ = 12345;
    MicSource mic_src_ = MicSource::Tone;
    std::chrono::steady_clock::time_point in_start_;
    std::atomic<bool> overflow_{false};
    SDL_AudioDeviceID cap_dev_ = 0;
    std::vector<int16_t> cap_ring_;
    std::atomic<uint32_t> cap_head_{0};
    std::atomic<uint32_t> cap_tail_{0};

    SDL_AudioDeviceID dev_ = 0;
    size_t capacity_ = 2048;
    std::vector<uint32_t> ring_;
    std::atomic<size_t> head_{0};
    std::atomic<size_t> tail_{0};
};
