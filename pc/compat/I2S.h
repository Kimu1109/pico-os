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
// ヘッドレス(SDL_VIDEODRIVER=dummy)で音の中身を確かめるなら SDL_AUDIODRIVER=disk で
// sdlaudio.raw へ書き出せる(SDL_DISKAUDIOFILE でファイル名を変えられる)。
//
// 読み取り側(Sound_Functions)と書き込み側(SDLの音声スレッド/Webではメインスレッド)の間は
// 1対1のリングバッファで、ロックを取らない。
#pragma once

#if __has_include(<SDL2/SDL.h>)
    #include <SDL2/SDL.h>
#else
    #include <SDL.h>
#endif

#include <atomic>
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
        want.samples = 256;     //リング(実機と同じ512サンプル)より小さく取る
        return want;
    }

    // 音声デバイスを開けるか(開けたらすぐ閉じる)
    inline bool ProbeDevice(){
        if(SDL_InitSubSystem(SDL_INIT_AUDIO) != 0){
            printf("[PC] 音声: SDLの音声を初期化できません(%s)。アンプ未接続として扱います\n", SDL_GetError());
            return false;
        }
        SDL_AudioSpec want = DesiredSpec(22050);
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
    explicit I2S(int /*direction*/ = OUTPUT){}
    // 終了時(SDLが先に片付いている可能性がある)にSDLを触らないよう、デストラクタでは何もしない

    bool setBCLK(int){ return true; }
    bool setDATA(int){ return true; }
    bool setBitsPerSample(int bps){ return bps == 16; }
    bool setBuffers(size_t buffers, size_t words, int32_t = 0){
        capacity_ = buffers * words;
#if defined(__EMSCRIPTEN__)
        //Webは2コア目の代わりにフレームごと(約16ms、ぶれあり)にしか書けないので、
        //実機より多めに溜めて途切れにくくする(その分、音が出るまで少し遅れる)
        if(capacity_ < 2048) capacity_ = 2048;
#endif
        return capacity_ > 0;
    }

    bool begin(long rate){
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

private:
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

    SDL_AudioDeviceID dev_ = 0;
    size_t capacity_ = 2048;
    std::vector<uint32_t> ring_;
    std::atomic<size_t> head_{0};
    std::atomic<size_t> tail_{0};
};
