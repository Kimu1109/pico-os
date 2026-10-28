#include "hal/Mic_Functions.hpp"
#include "config/Hardware_Config.hpp"

// 載せない版(-DPICO_HW_MIC=0)は Mic_Functions_Off.cpp が代わりに入る
#if PICO_HW_MIC
#include "system/Config_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "storage/SD_Path.hpp"
#include "hal/Clock_Functions.hpp"
#include "sound/Mic_Dsp.hpp"
#include "sound/Wav_Writer.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"
#include "OS_Data.hpp"

#include <Arduino.h>
#include <I2S.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

    // ================================================================
    // 共有(1コア目 ⇔ 2コア目)
    // ================================================================

    // 1コア目→2コア目: 0=止める 1=聞く(メーター) 2=録音
    std::atomic<uint8_t>  want_mode{0};
    // 1コア目→2コア目: 開始に失敗した後、もう一度試すときに進める
    std::atomic<uint32_t> retry_epoch{0};
    // 1コア目→2コア目: 周波数・チャンネルを変えたら進める(cfg_* を書いてから)
    std::atomic<uint32_t> cfg_epoch{0};
    uint32_t cfg_rate  = MicFunctions::kDefaultRate;   // cfg_epoch の前に書く
    bool     cfg_right = false;
    std::atomic<int32_t>  gain_q8{256};
    // 1コア目→2コア目: 録音の列。want_mode=2 の前に置き、片付けまで消さない
    std::atomic<MicDsp::PcmRing*> ring_ptr{nullptr};

    // 2コア目→1コア目
    std::atomic<bool>     core1_running{false};
    std::atomic<bool>     core1_failed{false};
    std::atomic<bool>     core1_signal{false};
    std::atomic<int32_t>  core1_peak{0};
    std::atomic<uint32_t> core1_dropped{0};     // 列が溢れて捨てた数(録音を始めるとき1コア目が0にする)
    std::atomic<uint32_t> core1_overruns{0};
    // 2コア目が「列へ積むかもしれない」間は真。1コア目は want_mode を下ろした後、これが偽になるまで列を消さない
    // (2コア目は 真を書く→want_modeを読む→録音でなければ偽を書く、の順。どちらも順序一貫で読み書きする)
    std::atomic<bool>     rec_active{false};

    // ================================================================
    // 2コア目だけ
    // ================================================================

    I2S mic_i2s(INPUT, MIC_I2S_BCLK, MIC_I2S_DATA);
    MicDsp::Pipeline pipeline;
    bool     running = false;
    bool     failed = false;
    uint32_t failed_epoch = 0;
    uint32_t seen_cfg = 0xFFFFFFFFu;
    constexpr size_t kReadWords = 256;          // 1回に読むワード数(1KB)
    uint32_t word_buf[kReadWords + 1];          // +1 は前回の余りの1ワード
    int16_t  out_buf[kReadWords / 2];
    bool     has_pending = false;
    uint32_t pending_word = 0;

    void StopInput(){
        if(running) mic_i2s.end();
        running = false;
        has_pending = false;
        core1_running.store(false, std::memory_order_release);
        core1_signal.store(false, std::memory_order_release);
    }

    bool StartInput(){
        const uint32_t ep = retry_epoch.load(std::memory_order_acquire);
        if(failed && ep == failed_epoch) return false;
        mic_i2s.setBCLK(MIC_I2S_BCLK);
        mic_i2s.setDATA(MIC_I2S_DATA);
        mic_i2s.setBitsPerSample(32);
        mic_i2s.setBuffers(MicFunctions::kBufferCount, MicFunctions::kBufferWords);
        //スリープ中にクロックを下げていたら、I2Sの分周を計算する前に戻す
        ClockFunctions::EnsureFull();
        if(!mic_i2s.begin(MicDsp::kCaptureRate)){
            failed = true;
            failed_epoch = ep;
            core1_failed.store(true, std::memory_order_release);
            return false;
        }
        failed = false;
        running = true;
        has_pending = false;
        seen_cfg = 0xFFFFFFFFu;     //下で必ず設定し直す
        core1_failed.store(false, std::memory_order_release);
        core1_running.store(true, std::memory_order_release);
        return true;
    }

    // ================================================================
    // 1コア目だけ
    // ================================================================

    enum class Rec : uint8_t { Idle, Recording, Finishing };
    Rec rec_state = Rec::Idle;
    bool monitor = false;
    MicFunctions::StopReason stop_reason = MicFunctions::StopReason::None;
    MicFunctions::StopReason pending_reason = MicFunctions::StopReason::None;
    int gain_db = MicFunctions::kDefaultGainDb;
    WavWriter writer;
    MicDsp::PcmRing ring;
    int16_t* ring_storage = nullptr;
    FixedString<PICO_PATH_LEN> rec_path;
    uint32_t last_header_ms = 0;
    bool logged_running = false, logged_failed = false;
    uint32_t logged_dropped = 0;
    uint32_t pending_rate = 0;      // 録音の後片付けが終わったら戻す周波数(RestoreSampleRate)

    void ApplyWant(){
        const uint8_t m = (rec_state != Rec::Idle && rec_state != Rec::Finishing) ? 2 : (monitor || rec_state == Rec::Finishing ? 1 : 0);
        const uint8_t old = want_mode.exchange(m);
        //止まっていた所から使い始めるときは、前の失敗を忘れてもう一度試す
        if(old == 0 && m != 0) retry_epoch.fetch_add(1);
    }

    // 列を取り出して書く。max 個まで
    void Drain(uint32_t max){
        while(max > 0){
            uint32_t n = 0;
            const int16_t* p = ring.peek(n);
            if(n == 0) break;
            if(n > max) n = max;
            if(rec_state == Rec::Recording && !writer.failed()){
                if(!writer.write(p, n) && pending_reason == MicFunctions::StopReason::None){
                    pending_reason = writer.dataBytes() > WavWriter::kMaxDataBytes - n * 2
                        ? MicFunctions::StopReason::TooLong : MicFunctions::StopReason::WriteFailed;
                }
            }else if(!writer.failed()){
                writer.write(p, n);     //止めた後の残り(書けなくても理由は変えない)
            }
            ring.consume(n);
            max -= n;
        }
    }

    void BeginFinish(MicFunctions::StopReason reason){
        if(rec_state != Rec::Recording) return;
        stop_reason = reason;
        rec_state = Rec::Finishing;
        ApplyWant();
    }

    void CompleteFinish(){
        Drain(0xFFFFFFFFu);
        writer.finish();
        ring_ptr.store(nullptr);
        free(ring_storage);
        ring_storage = nullptr;
        rec_state = Rec::Idle;
        ApplyWant();
        if(pending_rate != 0){
            MicFunctions::SetSampleRate(pending_rate, false);
            pending_rate = 0;
        }
        const uint32_t ms = (uint32_t)((uint64_t)writer.dataBytes() / 2 * 1000 / (cfg_rate ? cfg_rate : 1));
        if(stop_reason == MicFunctions::StopReason::User){
            LOG_SYS_OK("録音: %s を保存しました(%lu.%lu秒)", rec_path.c_str(), (unsigned long)(ms / 1000), (unsigned long)(ms % 1000 / 100));
        }else{
            LOG_SYS_WARN("録音: %s を途中で止めました(%s)", rec_path.c_str(), MicFunctions::StopReasonToStr(stop_reason));
        }
    }

    void LoadConfig(){
        if(!OSData::SD_usable || !OSData::SD.exists(PICO_Path::FILE::CFG::SYS_MIC_CFG)) return;
        PICO_Config::ParseFile(PICO_Path::FILE::CFG::SYS_MIC_CFG,
            [](const char* key, const char* value){
                int v = 0;
                if(strcmp(key, "rate") == 0){
                    if(PICO_Config::ConfigValue::AsInt(value, v) && MicDsp::ValidRate((uint32_t)v)) cfg_rate = (uint32_t)v;
                    else LOG_SYS_WARN("mic.cfg: rate は 16000 / 24000 / 48000 です: %s", value);
                }else if(strcmp(key, "gain") == 0){
                    if(PICO_Config::ConfigValue::AsInt(value, v) && v >= MicDsp::kMinGainDb && v <= MicDsp::kMaxGainDb) gain_db = v;
                    else LOG_SYS_WARN("mic.cfg: gain は %d〜%d の整数(dB)です: %s", MicDsp::kMinGainDb, MicDsp::kMaxGainDb, value);
                }else if(strcmp(key, "channel") == 0){
                    if(strcmp(value, "left") == 0) cfg_right = false;
                    else if(strcmp(value, "right") == 0) cfg_right = true;
                    else LOG_SYS_WARN("mic.cfg: channel は left / right です: %s", value);
                }
            });
    }

    void SaveValue(const char* key, const char* value){
        if(!OSData::SD_usable) return;
        if(!OSData::SD.exists("/sys")) OSData::SD.mkdir("/sys");
        if(!PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_MIC_CFG, key, value)){
            LOG_SYS_WARN("mic.cfg へ %s を書けませんでした", key);
        }
    }
}

// ================================================================
// 1コア目
// ================================================================

void MicFunctions::Setup(){
    LoadConfig();
    gain_q8.store(MicDsp::GainQ8(gain_db));
    cfg_epoch.fetch_add(1);
}

void MicFunctions::Update(){
    //2コア目の状態をログへ(2コア目はログを出せない)
    const bool run = core1_running.load(std::memory_order_acquire);
    const bool fail = core1_failed.load(std::memory_order_acquire);
    if(run != logged_running){
        logged_running = run;
        if(run) LOG_SYS_OK("マイク: I2Sを開始しました(%luHz で受けて %luHz へ)", (unsigned long)MicDsp::kCaptureRate, (unsigned long)cfg_rate);
    }
    if(fail != logged_failed){
        logged_failed = fail;
        if(fail) LOG_SYS_FAIL("マイク: I2Sを開始できませんでした(PIO/DMAが足りない?)");
    }

    if(rec_state == Rec::Recording){
        if(fail && !run) pending_reason = StopReason::InputFailed;
        if(ring.size() >= kMinDrainSamples) Drain(kMaxDrainSamples);
        const uint32_t now = millis();
        if(now - last_header_ms >= kHeaderUpdateMs){
            last_header_ms = now;
            writer.updateHeader();
            //取りこぼしはここで5秒ごとにまとめて1回だけ出す
            const uint32_t dropped = core1_dropped.load(std::memory_order_relaxed);
            if(dropped != logged_dropped){
                LOG_SYS_WARN("録音: 取りこぼし %lu サンプル(SDの書き込みが追いつかない?)", (unsigned long)dropped);
                logged_dropped = dropped;
            }
        }
        if(pending_reason != StopReason::None){
            const StopReason r = pending_reason;
            pending_reason = StopReason::None;
            BeginFinish(r);
        }
    }

    if(rec_state == Rec::Finishing){
        //2コア目が列へ積まなくなるのを待ってから残りを書いて閉じる
        Drain(kMaxDrainSamples);
        if(!rec_active.load()) CompleteFinish();
    }
}

void MicFunctions::SetMonitor(bool on){
    monitor = on;
    ApplyWant();
}

MicFunctions::Input MicFunctions::InputState(){
    if(want_mode.load() == 0) return Input::Off;
    if(core1_running.load(std::memory_order_acquire)) return Input::Running;
    if(core1_failed.load(std::memory_order_acquire)) return Input::Failed;
    return Input::Starting;
}

int32_t MicFunctions::TakePeak(){ return core1_peak.exchange(0); }
bool MicFunctions::HasSignal(){
    return core1_running.load(std::memory_order_acquire) && core1_signal.load(std::memory_order_acquire);
}

bool MicFunctions::NextRecordingPath(char* out, size_t cap){
    if(!out || cap == 0 || !OSData::SD_usable) return false;
    const char* dir = PICO_Path::DIR::RECORDINGS;
    if(!OSData::SD.exists(dir)) OSData::SD.mkdir(dir);
    for(int i = 1; i <= 9999; i++){
        snprintf(out, cap, "%srec_%04d.wav", dir, i);
        if(!OSData::SD.exists(out)) return true;
    }
    return false;
}

MicFunctions::RecError MicFunctions::StartRecording(const char* path){
    if(rec_state != Rec::Idle) return RecError::Busy;
    if(!OSData::SD_usable) return RecError::NoSd;
    char auto_path[PICO_PATH_LEN + 1];
    if(!path){
        if(!NextRecordingPath(auto_path, sizeof(auto_path))) return RecError::NoName;
        path = auto_path;
    }
    ring_storage = (int16_t*)malloc(kRingSamples * sizeof(int16_t));
    if(!ring_storage) return RecError::NoMemory;
    if(!writer.begin(path, cfg_rate)){
        free(ring_storage);
        ring_storage = nullptr;
        return RecError::OpenFailed;
    }
    rec_path.assign(path);
    ring.attach(ring_storage, kRingSamples);
    core1_dropped.store(0);
    core1_overruns.store(0);
    logged_dropped = 0;
    stop_reason = StopReason::None;
    pending_reason = StopReason::None;
    last_header_ms = millis();
    ring_ptr.store(&ring);
    rec_state = Rec::Recording;
    ApplyWant();
    LOG_SYS_MSG("録音: %s へ録音します(%luHz、+%ddB)", rec_path.c_str(), (unsigned long)cfg_rate, gain_db);
    return RecError::None;
}

void MicFunctions::StopRecording(){ BeginFinish(StopReason::User); }
bool MicFunctions::IsRecording(){ return rec_state != Rec::Idle; }
bool MicFunctions::IsFinishing(){ return rec_state == Rec::Finishing; }
const char* MicFunctions::RecordingPath(){ return rec_path.c_str(); }
uint32_t MicFunctions::RecordedMs(){
    return (uint32_t)((uint64_t)writer.dataBytes() / 2 * 1000 / (cfg_rate ? cfg_rate : 1));
}
uint32_t MicFunctions::DroppedSamples(){ return core1_dropped.load(std::memory_order_relaxed); }
uint32_t MicFunctions::Overruns(){ return core1_overruns.load(std::memory_order_relaxed); }
MicFunctions::StopReason MicFunctions::LastStopReason(){ return stop_reason; }

uint32_t MicFunctions::SampleRate(){ return cfg_rate; }
void MicFunctions::RestoreSampleRate(uint32_t rate){
    if(!MicDsp::ValidRate(rate)) return;
    if(rec_state == Rec::Idle) SetSampleRate(rate, false);
    else pending_rate = rate;
}

bool MicFunctions::SetSampleRate(uint32_t rate, bool persist){
    if(!MicDsp::ValidRate(rate) || rec_state != Rec::Idle) return false;
    if(rate != cfg_rate){
        cfg_rate = rate;
        cfg_epoch.fetch_add(1);
    }
    if(persist){
        char buf[12];
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)rate);
        SaveValue("rate", buf);
    }
    return true;
}

int MicFunctions::GainDb(){ return gain_db; }
void MicFunctions::SetGainDb(int db, bool persist){
    if(db < MicDsp::kMinGainDb) db = MicDsp::kMinGainDb;
    if(db > MicDsp::kMaxGainDb) db = MicDsp::kMaxGainDb;
    gain_db = db;
    gain_q8.store(MicDsp::GainQ8(db));
    if(persist){
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", db);
        SaveValue("gain", buf);
    }
}

bool MicFunctions::RightChannel(){ return cfg_right; }

const char* MicFunctions::RecErrorToStr(RecError e){
    switch(e){
        case RecError::None:       return "";
        case RecError::NoSd:       return "SDカードが使えません";
        case RecError::Busy:       return "もう録音しています";
        case RecError::NoMemory:   return "メモリが足りません";
        case RecError::OpenFailed: return "録音のファイルを作れません";
        case RecError::NoName:     return "録音の番号が尽きました(rec_9999まで)";
        case RecError::NoDevice:   return "このファームはマイクなしでビルドされています";
    }
    return "";
}

const char* MicFunctions::StopReasonToStr(StopReason r){
    switch(r){
        case StopReason::None:        return "";
        case StopReason::User:        return "止めました";
        case StopReason::WriteFailed: return "SDへ書けませんでした";
        case StopReason::InputFailed: return "マイクのI2Sを開始できませんでした";
        case StopReason::TooLong:     return "ファイルの上限(2GB)に達しました";
    }
    return "";
}

// ================================================================
// 2コア目
// ================================================================

bool MicFunctions::Core1Busy(){ return running; }

bool MicFunctions::LoopCore1(){
    //rec_active を先に立ててから want_mode を読む(1コア目が列を消すのと入れ違わないように)
    rec_active.store(true);
    const uint8_t want = want_mode.load();
    MicDsp::PcmRing* rq = (want == 2) ? ring_ptr.load() : nullptr;
    if(!rq) rec_active.store(false);

    if(want == 0){
        if(running) StopInput();
        return false;
    }
    if(!running && !StartInput()) return false;

    const uint32_t ce = cfg_epoch.load(std::memory_order_acquire);
    if(ce != seen_cfg){
        seen_cfg = ce;
        pipeline.configure(cfg_rate, cfg_right);
    }
    pipeline.setGainQ8(gain_q8.load(std::memory_order_relaxed));

    bool busy = false;
    //1回の呼び出しで約10ms分まで(出力の音源を待たせすぎない)
    for(int iter = 0; iter < 4; iter++){
        const int avail = mic_i2s.available();
        size_t want_words = (size_t)(avail > 0 ? avail : 0) / 4;
        if(want_words > kReadWords) want_words = kReadWords;
        if(want_words == 0) break;
        const size_t base = has_pending ? 1 : 0;
        if(has_pending) word_buf[0] = pending_word;
        const size_t got = mic_i2s.read((uint8_t*)(word_buf + base), want_words * 4) / 4;
        if(got == 0) break;
        busy = true;
        const size_t total = base + got;
        const size_t pairs = total / 2;
        has_pending = (total & 1) != 0;
        if(has_pending) pending_word = word_buf[total - 1];

        const size_t n = pipeline.process(word_buf, pairs, out_buf, sizeof(out_buf) / sizeof(out_buf[0]));
        if(rq && n > 0){
            const uint32_t pushed = rq->push(out_buf, (uint32_t)n);
            if(pushed < n) core1_dropped.fetch_add((uint32_t)n - pushed, std::memory_order_relaxed);
        }
    }
    if(mic_i2s.getOverflow()) core1_overruns.fetch_add(1, std::memory_order_relaxed);

    const int32_t p = pipeline.takePeak();
    if(p > core1_peak.load(std::memory_order_relaxed)) core1_peak.store(p, std::memory_order_relaxed);
    core1_signal.store(pipeline.hasSignal(), std::memory_order_release);
    return busy;
}

#endif // PICO_HW_MIC
