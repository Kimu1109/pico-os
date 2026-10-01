#include "functions/Sound_Functions.hpp"
#include "functions/Config_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/Battery_Functions.hpp"
#include "storage/SD_Path.hpp"
#include "sound/Mml_Compiler.hpp"
#include "sound/Music_Player.hpp"
#include "sound/Gb_Apu.hpp"
#include "sound/Gb_Audio_Link.hpp"
#include "sound/Wav_Decoder.hpp"
#include "sound/Wav_Stream.hpp"
#include "gb/Gb_Audio_Sink.hpp"
#include "consts.hpp"
#include "OS_Data.hpp"

#include <Arduino.h>
#include <I2S.h>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <new>

using namespace SoundFunctions;

namespace {
    // ================================================================
    // 2つのコアで共有するもの(std::atomicだけ)
    // ================================================================

    std::atomic<bool>     ready{false};         // 1コア目のSetup()が済んだ
    std::atomic<bool>     want_run{false};      // 1コア目→2コア目: I2Sを動かしてほしい
    std::atomic<uint32_t> retry_epoch{0};       // 1コア目→2コア目: 増えたらbegin()の失敗を忘れて試し直す
    std::atomic<uint8_t>  master_volume{kDefaultVolume};
    // 1コア目→2コア目: バッテリー駆動中か(音割れ防止のkBatteryVolumeCapPercent頭打ちを掛けるかどうか)
    std::atomic<bool>     battery_cap_active{false};
    // 1コア目→2コア目: 省電力中(何も鳴っていなければI2Sを止めてゆっくり回る)
    std::atomic<bool>     power_save{false};

    std::atomic<bool>     core1_running{false}; // 2コア目→1コア目: I2Sが動いている
    std::atomic<bool>     core1_failed{false};  // 2コア目→1コア目: begin()に失敗した
    std::atomic<ChipSynth::ChannelMask> core1_active{0};  // 2コア目→1コア目: 鳴っているチャンネル
    std::atomic<uint32_t> core1_processed{0};   // 2コア目→1コア目: 音源へ渡し終えたコマンドの数
    std::atomic<bool>     core1_music{false};   // 2コア目→1コア目: 曲が鳴っている
    std::atomic<bool>     music_paused{false};  // 1コア目→2コア目: 曲を一時停止してほしい
    std::atomic<uint32_t> core1_music_ms{0};    // 2コア目→1コア目: 曲を鳴らし始めてからの時間(ms)
    std::atomic<uint8_t>  core1_gb_active{0};   // 2コア目→1コア目: GBの音源で鳴っているチャンネル

    // GBエミュの音源チップへの書き込みの列。最初にROMを起動したときに1コア目が確保して置き、以降は持ち続ける
    // (2コア目は置かれたのを見てから読む。解放しないので、読んでいる途中で消えることは無い)
    std::atomic<GbAudioLink*> gb_link{nullptr};
    // WAVの列。最初にWAVを鳴らすときに1コア目が確保して置き、以降は持ち続ける(gb_linkと同じ考え方)
    std::atomic<WavStream*> wav_stream{nullptr};

    // --- コマンドの列(1コア目が積み、2コア目が取り出す。1対1なのでロック無しで足りる) ---
    enum class CmdType : uint8_t { Play, Stop, StopAll, MusicPlay, MusicStop };
    struct Command {
        CmdType type;
        uint8_t ch;
        ChipSynth::Note note;
        const uint8_t* music = nullptr;     // MusicPlay: 演奏データ(置き場の片方)
        uint16_t music_size = 0;
    };
    Command queue[kCommandQueueSize];
    std::atomic<uint32_t> q_head{0};    // 積んだ数(1コア目だけが書く)
    std::atomic<uint32_t> q_tail{0};    // 取り出した数(2コア目だけが書く)
    std::atomic<uint32_t> dropped{0};

    bool Push(const Command& cmd){
        const uint32_t h = q_head.load(std::memory_order_relaxed);
        if(h - q_tail.load(std::memory_order_acquire) >= kCommandQueueSize){
            dropped.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        queue[h % kCommandQueueSize] = cmd;
        q_head.store(h + 1, std::memory_order_release);
        return true;
    }

    // ================================================================
    // 1コア目だけが触るもの
    // ================================================================

    Output output = Output::Auto;
    bool connected = false;             // 採用済みの状態
    bool raw_last = false;              // 直近の読み取り
    uint8_t raw_stable = 0;             // raw_last が何回続いたか
    unsigned long last_detect_ms = 0;
    bool logged_running = false;        // ログを出した時点の core1_running
    bool logged_failed = false;

    // --- 曲(1コア目側) ---
    // 演奏データの置き場は2つ。2コア目が片方を読んでいる間に、もう片方へ次の曲を書く。
    // 最初に曲を鳴らすときに確保し、以降は持ち続ける(曲を使わないならRAMを使わない)
    struct MusicWork {
        uint8_t slots[2][kMusicDataBytes];
        MmlCompiler compiler;
    };
    MusicWork* music_work = nullptr;
    int music_current = -1;                 // 2コア目へ最後に渡した置き場(止めたら-1)
    uint32_t slot_release_seq[2] = {0, 0};  // この数のコマンドが処理されたら、その置き場は空く
    uint32_t music_cmd_seq = 0;             // 最後に積んだ曲のコマンドが何番目か
    bool music_cmd_play = false;            // それが「鳴らす」だったか
    FixedString<PICO_STR_M> music_title;
    uint32_t music_total_ms = 0;
    bool music_loops = false;

    // --- WAV(1コア目側) ---
    WavDecoder* wav_decoder = nullptr;      //最初にWAVを鳴らすときに確保し、以降は持ち続ける
    FixedString<PICO_STR_M> wav_title;
    uint64_t wav_pushed = 0;                // 列へ積んだサンプル数(鳴らし始め/飛んだ位置から数える)
    uint32_t wav_duration_ms = 0;
    bool wav_loop = false;
    bool wav_loaded = false;                // 鳴らしたWAVが今も有効(WavStop()するまで。読み終えても true)
    uint32_t logged_wav_underruns = 0;
    unsigned long logged_wav_ms = 0;

    bool ReadDetectPin(){
        return digitalRead(AUDIO_DETECT) == LOW;
    }

    void LoadConfig(){
        if(!OSData::SD_usable) return;
        //無いのが普通(既定値で動く)。ParseFile()は開けないとFAILを出すので先に確かめる
        if(!OSData::SD.exists(PICO_Path::FILE::CFG::SYS_SOUND_CFG)) return;

        PICO_Config::ParseFile(PICO_Path::FILE::CFG::SYS_SOUND_CFG,
            [](const char* key, const char* value){
                if(strcmp(key, "output") == 0){
                    if(strcmp(value, "auto") == 0)      output = Output::Auto;
                    else if(strcmp(value, "off") == 0)  output = Output::Off;
                    else LOG_SYS_WARN("sound.cfg: output の値が不明です: %s", value);
                }else if(strcmp(key, "volume") == 0){
                    int v = 0;
                    if(PICO_Config::ConfigValue::AsInt(value, v)) SetVolume(v);
                    else LOG_SYS_WARN("sound.cfg: volume は0〜100の整数です: %s", value);
                }
            }
        );
    }

    void PublishWant(){
        want_run.store(connected && output == Output::Auto, std::memory_order_release);
    }

    void Detect(unsigned long now_ms, bool force){
        if(!force && now_ms - last_detect_ms < kDetectIntervalMs) return;
        last_detect_ms = now_ms;

        const bool raw = ReadDetectPin();
        if(raw == raw_last){
            if(raw_stable < 255) raw_stable++;
        }else{
            raw_last = raw;
            raw_stable = 1;
        }

        if(force || (raw_stable >= kDetectStableCount && raw != connected)){
            if(raw != connected){
                if(raw) LOG_SYS_OK("Sound: アンプを検出しました");
                else    LOG_SYS_WARN("Sound: アンプが外れました(音は出ません)");
                //刺し直したら、前回のbegin()の失敗を忘れて試し直す
                retry_epoch.fetch_add(1, std::memory_order_release);
            }
            connected = raw;
            PublishWant();
        }
    }

    // 2コア目が変えた状態をログへ出す(2コア目はログを出せないため)
    void LogCore1Changes(){
        const bool running = core1_running.load(std::memory_order_acquire);
        const bool failed = core1_failed.load(std::memory_order_acquire);
        if(running != logged_running){
            logged_running = running;
            if(running) LOG_SYS_OK("Sound: 音声出力を開始しました");
            else        LOG_SYS_MSG("Sound: 音声出力を止めました");
        }
        if(failed != logged_failed){
            logged_failed = failed;
            if(failed) LOG_SYS_FAIL("Sound: I2Sを開始できませんでした");
        }
    }

    // WAVを読んで列へ積む(最大 max_samples)。読み終えたら読み取り係を閉じる
    void FeedWav(uint32_t max_samples){
        WavStream* wav = wav_stream.load(std::memory_order_relaxed);
        if(!wav || !wav_decoder || !wav_decoder->isOpen()) return;
        int16_t tmp[256];
        uint32_t want = wav->freeSpace();
        if(want > max_samples) want = max_samples;
        while(want > 0){
            const size_t n = wav_decoder->read(tmp, want < 256 ? want : 256);
            wav_pushed += wav->push(tmp, n);
            want -= (uint32_t)n;
            if(wav_decoder->finished()){
                wav_decoder->close();
                wav->setFeeding(false);     //ここから先は鳴らし切るだけ(足りなくても途切れとは数えない)
                break;
            }
            if(n == 0) break;
        }
    }

    // ================================================================
    // 2コア目だけが触るもの
    // ================================================================

    I2S i2s(OUTPUT);
    ChipSynth::Engine engine(kSampleRate);
    GbApu gb_apu(kSampleRate);
    MusicPlayer player(kSampleRate);
    ChipSynth::ChannelMask borrowed = 0;   // 効果音が借りているチャンネル(曲はここに触らない)

    bool running = false;
    bool failed = false;
    uint32_t seen_epoch = 0;
    uint32_t processed = 0;
    uint8_t eff_volume = kDefaultVolume;    // 全体の音量(バッテリーの頭打ち込み)。WAVへ掛ける

    // I2Sへ書く前の一時置き場
    constexpr size_t kChunk = 64;
    int16_t chunk[kChunk];
    size_t chunk_pos = 0;
    size_t chunk_len = 0;

    // 鳴らせない間の時間の進め方
    unsigned long last_step_ms = 0;
    uint32_t sample_frac = 0;           // ms×サンプル周波数 の1000未満の端数

    // 左右とも同じ値を1ワードへ詰める(arduino-picoのwrite16()と同じ並び: 上位が左)
    uint32_t PackStereo(int16_t s){
        const uint32_t u = (uint16_t)s;
        return (u << 16) | u;
    }

    void StartOutput(unsigned long now_ms){
        i2s.setBCLK(AUDIO_I2S_BCLK);    // LRCLKは自動でBCLK+1
        i2s.setDATA(AUDIO_I2S_DATA);
        i2s.setBitsPerSample(16);
        i2s.setBuffers(kBufferCount, kBufferWords);
        if(!i2s.begin(kSampleRate)){
            failed = true;
            core1_failed.store(true, std::memory_order_release);
            return;
        }
        digitalWrite(AUDIO_SHUTDOWN, HIGH);
        running = true;
        chunk_pos = chunk_len = 0;
        last_step_ms = now_ms;
        core1_running.store(true, std::memory_order_release);
    }

    void StopOutput(unsigned long now_ms){
        digitalWrite(AUDIO_SHUTDOWN, LOW);
        if(running){
            i2s.end();
            running = false;
            core1_running.store(false, std::memory_order_release);
        }
        //作ったが送れなかった分は捨てる。ここから先は時間で進める
        chunk_pos = chunk_len = 0;
        last_step_ms = now_ms;
        sample_frac = 0;
    }

    // 何か鳴っている(鳴っていることになっている)か。省電力中に止めてよいかの判断に使う
    bool AnythingSounding(){
        if(engine.activeMask() != 0 || (player.playing() && !player.paused())) return true;
        GbAudioLink* link = gb_link.load(std::memory_order_acquire);
        if(link && link->active()) return true;
        WavStream* wav = wav_stream.load(std::memory_order_acquire);
        return wav && wav->hasData();
    }

    void ApplyRunState(unsigned long now_ms){
        const uint32_t epoch = retry_epoch.load(std::memory_order_acquire);
        if(epoch != seen_epoch){
            seen_epoch = epoch;
            failed = false;
            core1_failed.store(false, std::memory_order_release);
        }
        bool want = want_run.load(std::memory_order_acquire) && !failed;
        //省電力中は、何も鳴っていない間だけI2Sとアンプを止める(鳴らす要求が来れば次の周回で動き直す)
        if(want && power_save.load(std::memory_order_acquire) && !AnythingSounding()) want = false;
        if(want && !running)        StartOutput(now_ms);
        else if(!want && running)   StopOutput(now_ms);
    }

    bool DrainCommands(){
        bool any = false;
        uint32_t t = q_tail.load(std::memory_order_relaxed);
        const uint32_t h = q_head.load(std::memory_order_acquire);
        while(t != h){
            const Command cmd = queue[t % kCommandQueueSize];
            t++;
            q_tail.store(t, std::memory_order_release);
            switch(cmd.type){
                case CmdType::Play:
                    //効果音。曲が鳴っていても、このチャンネルを借りて鳴らす
                    engine.play(cmd.ch, cmd.note);
                    borrowed |= (ChipSynth::ChannelMask)(1u << cmd.ch);
                    break;
                case CmdType::Stop:
                    //効果音を止める。曲が使っているチャンネルは止めない
                    if((borrowed & (1u << cmd.ch)) || !player.playing()) engine.stop(cmd.ch);
                    borrowed &= (ChipSynth::ChannelMask)~(1u << cmd.ch);
                    break;
                case CmdType::StopAll:
                    //効果音を全部止める。曲が鳴っていなければ全チャンネル
                    if(!player.playing()) engine.stopAll();
                    else for(int ch = 0; ch < kChannels; ch++) if(borrowed & (1u << ch)) engine.stop((uint8_t)ch);
                    borrowed = 0;
                    break;
                case CmdType::MusicPlay:
                    player.setBorrowed(borrowed);
                    player.start(engine, cmd.music, cmd.music_size);
                    break;
                case CmdType::MusicStop:
                    player.setBorrowed(borrowed);
                    player.stop(engine);
                    break;
            }
            processed++;
            any = true;
        }
        return any;
    }

    // バッファの空いている分だけ作って書き込む。1サンプルでも書けたらtrue
    bool Pump(){
        bool wrote = false;
        //1回に書くのはバッファ全体まで(溜まっていれば数サンプルで抜ける)
        const uint32_t kMaxPerCall = (uint32_t)kBufferWords * kBufferCount;
        for(uint32_t i = 0; i < kMaxPerCall; i++){
            if(chunk_pos == chunk_len){
                player.render(engine, chunk, kChunk);
                if(GbAudioLink* link = gb_link.load(std::memory_order_acquire)) link->render(gb_apu, chunk, kChunk);
                if(WavStream* wav = wav_stream.load(std::memory_order_acquire)) wav->renderAdd(chunk, kChunk, eff_volume);
                chunk_pos = 0;
                chunk_len = kChunk;
            }
            if(i2s.write((int32_t)PackStereo(chunk[chunk_pos]), false) == 0) break;   //満杯
            chunk_pos++;
            wrote = true;
        }
        return wrote;
    }

    // 鳴らせない間も、鳴っていることになっている音は時間どおりに進める
    void AdvanceByTime(unsigned long now_ms){
        const uint64_t acc = (uint64_t)(now_ms - last_step_ms) * kSampleRate + sample_frac;
        last_step_ms = now_ms;
        sample_frac = (uint32_t)(acc % 1000);
        player.render(engine, nullptr, (size_t)(acc / 1000));
        if(GbAudioLink* link = gb_link.load(std::memory_order_acquire)) link->render(gb_apu, nullptr, (size_t)(acc / 1000));
        if(WavStream* wav = wav_stream.load(std::memory_order_acquire)) wav->renderAdd(nullptr, (size_t)(acc / 1000), 0);
    }
}

// ================================================================
// 1コア目
// ================================================================

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

    //ここから2コア目が動き出す
    ready.store(true, std::memory_order_release);
}

void SoundFunctions::UpdateAt(unsigned long now_ms){
    Detect(now_ms, false);
    LogCore1Changes();

    //VSYSがUSBの5VでなくLiPoセルの電圧になるバッテリー駆動中は、アンプの出力ヘッドルームが
    //下がり音割れする(実機で確認済み)。BatteryFunctionsの直近のサンプル(60秒間隔)で判定する
    battery_cap_active.store(
        BatteryFunctions::HasSample() && !BatteryFunctions::IsExternallyPowered(),
        std::memory_order_relaxed
    );

    FeedWav(kWavMaxPerUpdate);
    const uint32_t wu = WavUnderruns();
    if(wu != logged_wav_underruns && now_ms - logged_wav_ms >= 1000){
        LOG_SYS_WARN("Sound: WAVの読み込みが間に合わず%lu回途切れました", (unsigned long)(wu - logged_wav_underruns));
        logged_wav_underruns = wu;
        logged_wav_ms = now_ms;
    }

    const uint32_t d = dropped.load(std::memory_order_relaxed);
    static uint32_t logged_dropped = 0;
    if(d != logged_dropped){
        LOG_SYS_WARN("Sound: 要求が多すぎて%lu件捨てました", (unsigned long)(d - logged_dropped));
        logged_dropped = d;
    }

    //GBの音: 書き込みを捨てたら知らせる(捨て続けるときにログで埋まらないよう1秒に1回まで)
    static uint32_t logged_gb_dropped = 0;
    static unsigned long logged_gb_ms = 0;
    const uint32_t gd = GbDroppedWrites();
    if(gd != logged_gb_dropped && now_ms - logged_gb_ms >= 1000){
        LOG_SYS_WARN("Sound: GBの音の書き込みが多すぎて%lu件捨てました", (unsigned long)(gd - logged_gb_dropped));
        logged_gb_dropped = gd;
        logged_gb_ms = now_ms;
    }
}

SoundFunctions::State SoundFunctions::GetState(){
    if(!connected) return State::Disconnected;
    if(output == Output::Off) return State::Muted;
    return core1_running.load(std::memory_order_acquire) ? State::Active : State::Disconnected;
}

bool SoundFunctions::IsConnected(){ return connected; }
bool SoundFunctions::IsAvailable(){ return GetState() == State::Active; }

SoundFunctions::Output SoundFunctions::GetOutput(){ return output; }
void SoundFunctions::SetOutput(Output o){
    output = o;
    retry_epoch.fetch_add(1, std::memory_order_release);
    PublishWant();
}

uint8_t SoundFunctions::GetVolume(){ return master_volume.load(std::memory_order_relaxed); }
void SoundFunctions::SetVolume(int v){
    if(v < 0) v = 0;
    if(v > 100) v = 100;
    master_volume.store((uint8_t)v, std::memory_order_release);
}
void SoundFunctions::SetPowerSave(bool enable){ power_save.store(enable, std::memory_order_release); }
bool SoundFunctions::IsPowerSave(){ return power_save.load(std::memory_order_acquire); }

bool SoundFunctions::IsBatteryVolumeCapActive(){ return battery_cap_active.load(std::memory_order_relaxed); }

bool SoundFunctions::Play(uint8_t ch, const ChipSynth::Note& note){
    if(ch >= kChannels) return false;
    return Push(Command{CmdType::Play, ch, note});
}

void SoundFunctions::Stop(uint8_t ch){
    if(ch >= kChannels) return;
    Push(Command{CmdType::Stop, ch, {}});
}

void SoundFunctions::StopAll(){
    Push(Command{CmdType::StopAll, 0, {}});
}

void SoundFunctions::Beep(uint16_t freq_hz, uint16_t duration_ms){
    if(freq_hz == 0 || duration_ms == 0){
        Stop(0);
        return;
    }
    ChipSynth::Note n;
    n.wave = ChipSynth::Wave::Pulse50;
    n.freq_x16 = (uint32_t)freq_hz * 16;
    n.volume = 15;
    n.length_ms = duration_ms;
    Play(0, n);
}

bool SoundFunctions::IsPlaying(){
    //2コア目は「鳴っているチャンネル」を書いてから「渡し終えた数」を書く。
    //渡し終えた数が積んだ数に追いついていれば、読んだチャンネルはその後の状態
    const uint32_t done = core1_processed.load(std::memory_order_acquire);
    const uint32_t sent = q_head.load(std::memory_order_relaxed);
    if(done != sent) return true;
    return core1_active.load(std::memory_order_acquire) != 0;
}

ChipSynth::ChannelMask SoundFunctions::ActiveChannels(){ return core1_active.load(std::memory_order_acquire); }
uint32_t SoundFunctions::DroppedCommands(){ return dropped.load(std::memory_order_relaxed); }

// ---- 曲 ----

namespace {
    bool SetError(MmlResult* result, const char* msg){
        if(result){
            *result = MmlResult();
            result->message.assign(msg);
        }
        return false;
    }

    bool CompileAndPlay(MmlLineSource& src, MmlResult* result, const char* fallback_title){
        if(!music_work){
            music_work = (MusicWork*)malloc(sizeof(MusicWork));
            if(!music_work) return SetError(result, "曲を読むためのメモリが足りません");
            new (&music_work->compiler) MmlCompiler();
        }

        //2コア目が読んでいない置き場を選ぶ
        const uint32_t done = core1_processed.load(std::memory_order_acquire);
        int slot = -1;
        for(int s = 0; s < 2; s++){
            if(s == music_current) continue;
            if((int32_t)(done - slot_release_seq[s]) >= 0){ slot = s; break; }
        }
        if(slot < 0) return SetError(result, "前の曲の片付けが済んでいません。少し待ってから試してください");

        MmlResult local;
        MmlResult& r = result ? *result : local;
        if(!music_work->compiler.compile(src, music_work->slots[slot], kMusicDataBytes, r)) return false;

        Command cmd{CmdType::MusicPlay, 0, {}};
        music_paused.store(false, std::memory_order_release);
        cmd.music = music_work->slots[slot];
        cmd.music_size = r.size;
        if(!Push(cmd)){
            r.ok = false;
            r.message.assign("要求が多すぎて曲を鳴らせませんでした");
            return false;
        }
        const uint32_t seq = q_head.load(std::memory_order_relaxed);
        if(music_current >= 0) slot_release_seq[music_current] = seq;
        music_current = slot;
        //使っている置き場は music_current で除外する。空く時期は止めるか差し替えたときに決まる
        music_cmd_seq = seq;
        music_cmd_play = true;

        music_total_ms = MusicPlayer::MeasureMs(cmd.music, cmd.music_size, &music_loops);
        music_title.assign(r.title.empty() ? fallback_title : r.title.c_str());
        if(!r.warning.empty()) LOG_SYS_WARN("Sound: %s", r.warning.c_str());
        return true;
    }
}

bool SoundFunctions::MusicPlayFile(const char* path, MmlResult* result){
    MmlFileSource src(path);
    if(!src.ok()) return SetError(result, "曲のファイルを開けません");
    const char* slash = strrchr(path, '/');
    return CompileAndPlay(src, result, slash ? slash + 1 : path);
}

bool SoundFunctions::MusicPlayText(const char* text, size_t len, MmlResult* result){
    MmlTextSource src(text, len);
    return CompileAndPlay(src, result, "");
}

void SoundFunctions::MusicStop(){
    if(music_current < 0) return;
    music_paused.store(false, std::memory_order_release);
    if(!Push(Command{CmdType::MusicStop, 0, {}})) return;
    const uint32_t seq = q_head.load(std::memory_order_relaxed);
    slot_release_seq[music_current] = seq;
    music_current = -1;
    music_cmd_seq = seq;
    music_cmd_play = false;
}

bool SoundFunctions::MusicPlaying(){
    const uint32_t done = core1_processed.load(std::memory_order_acquire);
    if((int32_t)(done - music_cmd_seq) < 0) return music_cmd_play;
    return core1_music.load(std::memory_order_acquire);
}

const char* SoundFunctions::MusicTitle(){ return music_title.c_str(); }

bool SoundFunctions::MusicPause(bool pause){
    if(!MusicPlaying()) return false;
    music_paused.store(pause, std::memory_order_release);
    return true;
}

bool SoundFunctions::MusicPaused(){
    return music_paused.load(std::memory_order_acquire) && MusicPlaying();
}

uint32_t SoundFunctions::MusicElapsedMs(){
    //2コア目が「鳴らす」を受け取るまでは、前の曲の値が残っている
    const uint32_t done = core1_processed.load(std::memory_order_acquire);
    if((int32_t)(done - music_cmd_seq) < 0) return 0;
    return core1_music_ms.load(std::memory_order_acquire);
}

uint32_t SoundFunctions::MusicTotalMs(bool* loops){
    if(loops) *loops = music_loops;
    return music_total_ms;
}

// ---- ゲームボーイの音 ----

namespace {
    class GbSink : public GbAudioSink {
    public:
        void begin() override {
            GbAudioLink* link = gb_link.load(std::memory_order_relaxed);
            if(!link){
                void* mem = malloc(sizeof(GbAudioLink));
                if(!mem){
                    LOG_SYS_FAIL("Sound: GBの音のためのメモリ(%uB)が足りません(音は出ません)", (unsigned)sizeof(GbAudioLink));
                    return;
                }
                link = new (mem) GbAudioLink(kSampleRate);
                gb_link.store(link, std::memory_order_release);
            }
            link->begin();
        }
        void write(uint32_t cycle, uint8_t reg, uint8_t val) override {
            if(GbAudioLink* link = gb_link.load(std::memory_order_relaxed)) link->write(cycle, reg, val);
        }
        void endFrame() override {
            if(GbAudioLink* link = gb_link.load(std::memory_order_relaxed)) link->endFrame();
        }
        void end() override {
            if(GbAudioLink* link = gb_link.load(std::memory_order_relaxed)) link->end();
        }
        uint8_t activeChannels() override {
            return core1_gb_active.load(std::memory_order_acquire);
        }
    };
    GbSink gb_sink;
}

GbAudioSink* SoundFunctions::GbAudio(){ return &gb_sink; }

uint32_t SoundFunctions::GbDroppedWrites(){
    GbAudioLink* link = gb_link.load(std::memory_order_relaxed);
    return link ? link->dropped() : 0;
}

// ---- WAV ----

bool SoundFunctions::WavPlay(const char* path, bool loop, uint8_t volume, const char** error, WavInfo* info){
    if(error) *error = "";
    WavStream* wav = wav_stream.load(std::memory_order_relaxed);
    if(!wav){
        void* mem = malloc(sizeof(WavStream));
        if(!mem){
            if(error) *error = "WAVを鳴らすためのメモリが足りません";
            return false;
        }
        wav = new (mem) WavStream();
        wav_stream.store(wav, std::memory_order_release);
    }
    if(!wav_decoder){
        void* mem = malloc(sizeof(WavDecoder));
        if(!mem){
            if(error) *error = "WAVを鳴らすためのメモリが足りません";
            return false;
        }
        wav_decoder = new (mem) WavDecoder();
    }

    //見出しを先に別の係で確かめると読み取り係が2つ要るので、読めなかったら今のWAVも止まる
    //(読み取り係は1つで、開き直した時点で前のファイルは閉じるため)
    wav->setFeeding(false);
    wav->setPaused(false);
    wav->flush();
    wav_pushed = 0;
    wav_duration_ms = 0;
    wav_loaded = false;
    wav_loop = loop;
    if(!wav_decoder->open(path, kSampleRate)){
        if(error) *error = wav_decoder->errorText();
        return false;
    }
    wav_decoder->setLoop(loop);
    if(info){
        info->channels = wav_decoder->channels();
        info->bits = wav_decoder->bitsPerSample();
        info->sample_rate = wav_decoder->sampleRate();
        info->duration_ms = wav_decoder->durationMs();
    }
    wav_duration_ms = wav_decoder->durationMs();
    wav_loaded = true;
    const char* slash = strrchr(path, '/');
    wav_title.assign(slash ? slash + 1 : path);

    wav->setVolume(volume);
    wav->setFeeding(true);
    FeedWav(kWavPrefillSamples);    //鳴らし始めの途切れを防ぐため、先にまとめて読んでおく
    return true;
}

void SoundFunctions::WavStop(){
    WavStream* wav = wav_stream.load(std::memory_order_relaxed);
    if(!wav) return;
    wav->setFeeding(false);
    wav->setPaused(false);
    wav->flush();
    wav_loaded = false;
    if(wav_decoder) wav_decoder->close();
}

bool SoundFunctions::WavPause(bool pause){
    WavStream* wav = wav_stream.load(std::memory_order_relaxed);
    if(!wav || !WavPlaying()) return false;
    wav->setPaused(pause);
    return true;
}

bool SoundFunctions::WavPaused(){
    WavStream* wav = wav_stream.load(std::memory_order_relaxed);
    return wav && wav->paused() && WavPlaying();
}

uint32_t SoundFunctions::WavDurationMs(){ return wav_duration_ms; }

uint32_t SoundFunctions::WavPositionMs(){
    WavStream* wav = wav_stream.load(std::memory_order_relaxed);
    if(!wav) return 0;
    const uint64_t buffered = wav->bufferedSamples();
    const uint64_t played = wav_pushed > buffered ? wav_pushed - buffered : 0;
    uint64_t ms = played * 1000 / kSampleRate;
    if(wav_duration_ms > 0){
        if(wav_loop) ms %= wav_duration_ms;
        else if(ms > wav_duration_ms) ms = wav_duration_ms;
    }
    return (uint32_t)ms;
}

bool SoundFunctions::WavSeekMs(uint32_t ms){
    WavStream* wav = wav_stream.load(std::memory_order_relaxed);
    if(!wav || !wav_decoder || wav_duration_ms == 0) return false;
    if(ms >= wav_duration_ms) ms = wav_duration_ms - 1;
    if(!wav_loaded) return false;

    wav->setFeeding(false);
    wav->flush();
    if(!wav_decoder->seekMs(ms)){
        //飛べなければ止める(読み取り係が中途半端な状態で残らないように)
        wav_decoder->close();
        return false;
    }
    wav_pushed = (uint64_t)ms * kSampleRate / 1000;
    wav->setFeeding(true);
    FeedWav(kWavPrefillSamples);
    return true;
}

bool SoundFunctions::WavPlaying(){
    WavStream* wav = wav_stream.load(std::memory_order_relaxed);
    if(!wav) return false;
    return (wav_decoder && wav_decoder->isOpen()) || wav->buffered();
}

const char* SoundFunctions::WavTitle(){ return wav_title.c_str(); }

uint32_t SoundFunctions::WavUnderruns(){
    WavStream* wav = wav_stream.load(std::memory_order_relaxed);
    return wav ? wav->underruns() : 0;
}

// ================================================================
// 2コア目
// ================================================================

void SoundFunctions::SetupCore1(){}
bool SoundFunctions::LoopCore1(){ return Core1StepAt(millis()); }

unsigned long SoundFunctions::IdleDelayMs(){
    //省電力中でI2Sも止まっていれば長く休む。動いている間は今までどおり1ms(バッファ切れを避ける)
    if(power_save.load(std::memory_order_acquire) && !core1_running.load(std::memory_order_acquire)){
        return kPowerSaveIdleDelayMs;
    }
    return 1;
}

bool SoundFunctions::Core1StepAt(unsigned long now_ms){
    //1コア目のSetup()(設定の読み込みと最初の検出)が済むまでは何もしない
    if(!ready.load(std::memory_order_acquire)){
        last_step_ms = now_ms;
        return false;
    }

    //鳴らせなかった間の時間を先に進めておく(このあとI2Sを開始する場合も、開始する時刻までは
    //鳴っていたことにする。新しく受け取る要求はこの時間に含めない)
    if(!running) AdvanceByTime(now_ms);

    bool busy = DrainCommands();
    //一時停止の要求を曲へ当てる(DrainCommands()で新しい曲が始まったときは、その曲も止まった状態にできる)
    player.setPaused(engine, music_paused.load(std::memory_order_acquire));
    //効果音が鳴り終わったチャンネルは曲へ返す(曲は次の音符から鳴らす)
    borrowed &= engine.activeMask();
    player.setBorrowed(borrowed);
    uint8_t vol = master_volume.load(std::memory_order_acquire);
    //バッテリー駆動中は設定値そのものは変えず、音源へ渡す値だけ頭打ちする(音割れ対策)
    if(battery_cap_active.load(std::memory_order_acquire) && vol > kBatteryVolumeCapPercent){
        vol = kBatteryVolumeCapPercent;
    }
    eff_volume = vol;
    if(vol != engine.masterVolume()){
        engine.setMasterVolume(vol);
        gb_apu.setMasterVolume(vol);
    }

    ApplyRunState(now_ms);

    if(running){
        busy |= Pump();
        last_step_ms = now_ms;
    }

    //この順(チャンネル→渡し終えた数)で書く。IsPlaying()参照
    core1_active.store(engine.activeMask(), std::memory_order_release);
    core1_music_ms.store(player.elapsedMs(), std::memory_order_release);
    core1_music.store(player.playing(), std::memory_order_release);
    {
        GbAudioLink* link = gb_link.load(std::memory_order_acquire);
        core1_gb_active.store((link && link->active()) ? gb_apu.activeMask() : 0, std::memory_order_release);
    }
    core1_processed.store(processed, std::memory_order_release);
    return busy;
}
