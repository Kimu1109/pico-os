#include "functions/Sound_Functions.hpp"
#include "functions/Config_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "storage/SD_Path.hpp"
#include "consts.hpp"
#include "OS_Data.hpp"

#include <Arduino.h>
#include <I2S.h>
#include <cstring>

namespace {
    I2S i2s(OUTPUT);

    SoundFunctions::Output output = SoundFunctions::Output::Auto;
    uint8_t volume = SoundFunctions::kDefaultVolume;

    // --- 検出 ---
    bool connected = false;             // 採用済みの状態
    bool raw_last = false;              // 直近の読み取り
    uint8_t raw_stable = 0;             // raw_last が何回続いたか
    unsigned long last_detect_ms = 0;

    // --- 出力 ---
    bool running = false;               // I2Sが動いている
    bool start_failed = false;          // begin()に失敗した(次に状態が変わるまで試し直さない)
    bool has_pending = false;           // 作ったが書き込めなかった1サンプル
    uint32_t pending_word = 0;

    // --- 鳴っていない間の時間の進め方 ---
    unsigned long last_update_ms = 0;
    uint32_t sample_frac = 0;           // ms×サンプル周波数 の1000未満の端数

    // --- 矩形波(動作確認用の音源) ---
    uint32_t tone_phase = 0;
    uint32_t tone_phase_inc = 0;
    uint32_t tone_remaining = 0;        // 残りサンプル数。0なら鳴っていない

    bool ReadDetectPin(){
        return digitalRead(AUDIO_DETECT) == LOW;
    }

    // 左右とも同じ値を1ワードへ詰める(arduino-picoのwrite16()と同じ並び: 上位が左)
    uint32_t PackStereo(int16_t s){
        const uint32_t u = (uint16_t)s;
        return (u << 16) | u;
    }

    void LoadConfig(){
        if(!OSData::SD_usable) return;
        //無いのが普通(既定値で動く)。ParseFile()は開けないとFAILを出すので先に確かめる
        if(!OSData::SD.exists(PICO_Path::FILE::CFG::SYS_SOUND_CFG)) return;

        PICO_Config::ParseFile(PICO_Path::FILE::CFG::SYS_SOUND_CFG,
            [](const char* key, const char* value){
                if(strcmp(key, "output") == 0){
                    if(strcmp(value, "auto") == 0)      output = SoundFunctions::Output::Auto;
                    else if(strcmp(value, "off") == 0)  output = SoundFunctions::Output::Off;
                    else LOG_SYS_WARN("sound.cfg: output の値が不明です: %s", value);
                }else if(strcmp(key, "volume") == 0){
                    int v = 0;
                    if(PICO_Config::ConfigValue::AsInt(value, v)) SoundFunctions::SetVolume(v);
                    else LOG_SYS_WARN("sound.cfg: volume は0〜100の整数です: %s", value);
                }
            }
        );
    }

    bool ShouldRun(){
        return connected && output == SoundFunctions::Output::Auto && !start_failed;
    }

    void StartOutput(unsigned long now_ms){
        i2s.setBCLK(AUDIO_I2S_BCLK);    // LRCLKは自動でBCLK+1
        i2s.setDATA(AUDIO_I2S_DATA);
        i2s.setBitsPerSample(16);
        i2s.setBuffers(SoundFunctions::kBufferCount, SoundFunctions::kBufferWords);
        if(!i2s.begin(SoundFunctions::kSampleRate)){
            LOG_SYS_FAIL("Sound: I2Sを開始できませんでした");
            start_failed = true;
            return;
        }
        digitalWrite(AUDIO_SHUTDOWN, HIGH);
        running = true;
        has_pending = false;
        last_update_ms = now_ms;
        LOG_SYS_OK("Sound: 音声出力を開始しました");
    }

    void StopOutput(unsigned long now_ms){
        digitalWrite(AUDIO_SHUTDOWN, LOW);
        if(running){
            i2s.end();
            running = false;
            LOG_SYS_MSG("Sound: 音声出力を止めました");
        }
        has_pending = false;
        //ここから先は時間で進める(止めた瞬間を起点にする)
        last_update_ms = now_ms;
        sample_frac = 0;
    }

    // 状態に合わせてI2Sを動かす/止める
    void ApplyState(unsigned long now_ms){
        const bool want = ShouldRun();
        if(want && !running)        StartOutput(now_ms);
        else if(!want && running)   StopOutput(now_ms);
    }

    // バッファの空いている分だけサンプルを作って書き込む
    void Pump(){
        //1回に書くのは最大でバッファ全体まで(溜まっていれば数十サンプルで抜ける)
        const uint32_t kMaxPerCall = (uint32_t)SoundFunctions::kBufferWords * SoundFunctions::kBufferCount;
        for(uint32_t i = 0; i < kMaxPerCall; i++){
            if(!has_pending){
                pending_word = PackStereo(SoundFunctions::NextSample());
                has_pending = true;
            }
            if(i2s.write((int32_t)pending_word, false) == 0) break;   //満杯
            has_pending = false;
        }
    }

    void Detect(unsigned long now_ms, bool force){
        if(!force && now_ms - last_detect_ms < SoundFunctions::kDetectIntervalMs) return;
        last_detect_ms = now_ms;

        const bool raw = ReadDetectPin();
        if(raw == raw_last){
            if(raw_stable < 255) raw_stable++;
        }else{
            raw_last = raw;
            raw_stable = 1;
        }

        if(force || (raw_stable >= SoundFunctions::kDetectStableCount && raw != connected)){
            if(raw != connected){
                if(raw) LOG_SYS_OK("Sound: アンプを検出しました");
                else    LOG_SYS_WARN("Sound: アンプが外れました(音は出ません)");
            }
            connected = raw;
            start_failed = false;   //刺し直したら試し直す
        }
    }
}

void SoundFunctions::Setup(){ SetupAt(millis()); }
void SoundFunctions::Update(){ UpdateAt(millis()); }

void SoundFunctions::SetupAt(unsigned long now_ms){
    pinMode(AUDIO_DETECT, INPUT_PULLUP);
    pinMode(AUDIO_SHUTDOWN, OUTPUT);
    digitalWrite(AUDIO_SHUTDOWN, LOW);

    LoadConfig();

    //起動時は待たずに今の値を採用する(起動直後から鳴らせるように)
    raw_last = ReadDetectPin();
    raw_stable = 1;
    Detect(now_ms, true);
    if(!connected) LOG_SYS_MSG("Sound: アンプが見つかりません(音は出ません)");

    last_update_ms = now_ms;
    ApplyState(now_ms);
}

void SoundFunctions::UpdateAt(unsigned long now_ms){
    Detect(now_ms, false);

    //鳴らせない間も、鳴っていることになっている音は時間どおりに進める。
    //このフレームで開始する場合も、開始する時刻までは先に進めておく
    if(!running){
        const uint64_t acc = (uint64_t)(now_ms - last_update_ms) * kSampleRate + sample_frac;
        last_update_ms = now_ms;
        sample_frac = (uint32_t)(acc % 1000);
        Advance((uint32_t)(acc / 1000));
    }

    ApplyState(now_ms);

    //鳴らせる間は、送ったサンプルの分だけ進む(時間ではなくI2Sが引き取った量が時計になる)
    if(running){
        Pump();
        last_update_ms = now_ms;
    }
}

SoundFunctions::State SoundFunctions::GetState(){
    if(!connected) return State::Disconnected;
    if(output == Output::Off) return State::Muted;
    return running ? State::Active : State::Disconnected;
}

bool SoundFunctions::IsConnected(){ return connected; }
bool SoundFunctions::IsAvailable(){ return GetState() == State::Active; }

SoundFunctions::Output SoundFunctions::GetOutput(){ return output; }
void SoundFunctions::SetOutput(Output o){
    output = o;
    start_failed = false;
    ApplyState(last_update_ms);
}

uint8_t SoundFunctions::GetVolume(){ return volume; }
void SoundFunctions::SetVolume(int v){
    if(v < 0) v = 0;
    if(v > 100) v = 100;
    volume = (uint8_t)v;
}

void SoundFunctions::Beep(uint16_t freq_hz, uint16_t duration_ms){
    if(freq_hz == 0 || duration_ms == 0){
        StopAll();
        return;
    }
    //ナイキスト周波数を超えると折り返して別の音になるので頭打ちにする
    if(freq_hz > kSampleRate / 2) freq_hz = kSampleRate / 2;
    tone_phase = 0;
    tone_phase_inc = (uint32_t)(((uint64_t)freq_hz << 32) / kSampleRate);
    tone_remaining = (uint32_t)((uint64_t)duration_ms * kSampleRate / 1000);
}

void SoundFunctions::StopAll(){
    tone_remaining = 0;
}

bool SoundFunctions::IsPlaying(){ return tone_remaining > 0; }

int16_t SoundFunctions::NextSample(){
    if(tone_remaining == 0) return 0;
    tone_remaining--;
    const int32_t amp = (int32_t)kMaxAmplitude * volume / 100;
    const int16_t s = (tone_phase & 0x80000000u) ? (int16_t)-amp : (int16_t)amp;
    tone_phase += tone_phase_inc;
    return s;
}

void SoundFunctions::Advance(uint32_t samples){
    if(samples >= tone_remaining){
        tone_remaining = 0;
        return;
    }
    tone_remaining -= samples;
    tone_phase += tone_phase_inc * samples;
}
