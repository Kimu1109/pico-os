// マイク(SPH0645)の入力と録音の検証。
//
// 前半は計算(sound/Mic_Dsp.hpp): 18bitの取り出し・チャンネル・直流を落とす・増幅・間引き(高い音が折り返さないこと)・
// 信号の有無・列(PcmRing)。
// 後半は MicFunctions の配線: 2コア目(LoopCore1)と1コア目(Update)を1本のスレッドで交互に呼ぶ。
// I2Sの入力は stubs/I2S.h の rx へテストがワードを積み、録ったWAVは stubs/SdFat.h の HostSd::files で見る。
#include "hal/Mic_Functions.hpp"
#include "hal/Clock_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "sound/Mic_Dsp.hpp"
#include "sound/Wav_Writer.hpp"
#include "OS_Data.hpp"

#include <Arduino.h>
#include <I2S.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

// SPH0645と同じ形のワード: 18bitの値を上位へ詰め、下位14bitは0
static uint32_t Word18(int32_t v){ return (uint32_t)(v * 16384); }

// 48kHzの正弦波(18bitで振幅amp、直流dc)を左右どちらかに入れた frames 組
static std::vector<uint32_t> Tone(double hz, double amp, int32_t dc, int frames, bool right, int start = 0){
    std::vector<uint32_t> w;
    w.reserve(frames * 2);
    for(int i = 0; i < frames; i++){
        const int32_t v = dc + (int32_t)lround(amp * sin(2.0 * M_PI * hz * (start + i) / 48000.0));
        w.push_back(right ? 0 : Word18(v));
        w.push_back(right ? Word18(v) : 0);
    }
    return w;
}

static std::vector<int16_t> Run(MicDsp::Pipeline& p, const std::vector<uint32_t>& w){
    std::vector<int16_t> out(w.size());
    const size_t n = p.process(w.data(), w.size() / 2, out.data(), out.size());
    out.resize(n);
    return out;
}

static double Rms(const std::vector<int16_t>& v, size_t from){
    double s = 0;
    for(size_t i = from; i < v.size(); i++) s += (double)v[i] * v[i];
    return sqrt(s / (double)(v.size() - from));
}
static double Mean(const std::vector<int16_t>& v, size_t from){
    double s = 0;
    for(size_t i = from; i < v.size(); i++) s += v[i];
    return s / (double)(v.size() - from);
}
static int Crossings(const std::vector<int16_t>& v, size_t from){
    int n = 0;
    for(size_t i = from + 1; i < v.size(); i++) if(v[i - 1] < 0 && v[i] >= 0) n++;
    return n;
}

static void TestDsp(){
    MicDsp::Pipeline p;
    check(!p.configure(22050, false), "dsp: 48000で割り切れない周波数は断る");
    check(!p.configure(8000, false), "dsp: 間引きが大きすぎる周波数は断る");
    check(p.configure(16000, false) && p.decimation() == 3, "dsp: 16000Hzは3分の1");

    //1kHz・振幅20000(18bit)・直流-3000。増幅0dB → 16bitで振幅5000、直流は消える
    p.setGainQ8(MicDsp::GainQ8(0));
    std::vector<int16_t> out = Run(p, Tone(1000, 20000, -3000, 48000, false));
    check(out.size() == 16000, "dsp: 48000組 → 16000サンプル");
    const double rms = Rms(out, 4000);
    check(fabs(rms - 5000 / sqrt(2.0)) < 5000 / sqrt(2.0) * 0.03, "dsp: 18bit → 16bitで振幅が1/4");
    check(fabs(Mean(out, 4000)) < 20, "dsp: 直流のずれが落ちる");
    const int zc = Crossings(out, 4000);
    check(zc >= 748 && zc <= 752, "dsp: 1kHzのまま(0.75秒で750回)");
    check(p.hasSignal(), "dsp: 値が変わっていれば信号あり");
    const int32_t peak = p.takePeak();
    check(peak > 4800 && peak < 5300, "dsp: 山は振幅ほど");
    check(p.takePeak() == 0, "dsp: 山は読むと0へ戻る");

    //+12dB は約4倍
    p.configure(16000, false);
    p.setGainQ8(MicDsp::GainQ8(12));
    out = Run(p, Tone(1000, 2000, 0, 48000, false));
    const double rms12 = Rms(out, 4000);
    check(fabs(rms12 / (500 / sqrt(2.0)) - 3.98) < 0.15, "dsp: +12dBで約4倍");

    //大きすぎる音は16bitで頭打ち(折り返さない)
    p.configure(48000, false);
    p.setGainQ8(MicDsp::GainQ8(36));
    out = Run(p, Tone(500, 100000, 0, 4800, false));
    int16_t mx = 0, mn = 0;
    for(int16_t v : out){ if(v > mx) mx = v; if(v < mn) mn = v; }
    check(mx == 32767 && mn == -32768, "dsp: 頭打ちする");

    //16kHzの出力のナイキスト(8kHz)より上の12kHzは、間引く前のFIRで削られる
    p.configure(16000, false);
    p.setGainQ8(MicDsp::GainQ8(0));
    out = Run(p, Tone(12000, 100000, 0, 48000, false));
    const double alias = Rms(out, 4000);
    check(alias < 25000 / sqrt(2.0) * 0.03, "dsp: 12kHzは-30dBより小さくなる(折り返さない)");
    //通したい3kHzはほとんど削られない
    p.configure(16000, false);
    out = Run(p, Tone(3000, 20000, 0, 48000, false));
    check(Rms(out, 4000) > 5000 / sqrt(2.0) * 0.85, "dsp: 3kHzは通る");

    //右チャンネル(SELを3V3にしたマイク)
    p.configure(24000, true);
    p.setGainQ8(MicDsp::GainQ8(0));
    out = Run(p, Tone(1000, 20000, 0, 24000, true));
    check(out.size() == 12000 && Rms(out, 3000) > 3000, "dsp: 右チャンネルを選べる");
    //左は0のまま → 0.5秒で「信号なし」
    p.configure(24000, false);
    Run(p, Tone(1000, 20000, 0, 23000, true));
    check(p.hasSignal(), "dsp: 0.5秒までは信号ありのまま");
    Run(p, Tone(1000, 20000, 0, 2000, true));
    check(!p.hasSignal(), "dsp: 値が0.5秒変わらなければ信号なし");
}

static void TestRing(){
    MicDsp::PcmRing r;
    int16_t store[8];
    r.attach(store, 8);
    int16_t a[6] = {1, 2, 3, 4, 5, 6};
    check(r.push(a, 6) == 6 && r.size() == 6, "列: 積める");
    uint32_t n = 0;
    const int16_t* p = r.peek(n);
    check(n == 6 && p[0] == 1 && p[5] == 6, "列: 先頭から読める");
    r.consume(4);
    check(r.push(a, 6) == 6, "列: 輪をまたいで積める");
    check(r.push(a, 6) == 0, "列: 満杯なら捨てる");
    p = r.peek(n);
    check(n == 4 && p[0] == 5, "列: 輪の終わりで切れる");
    r.consume(n);
    p = r.peek(n);
    check(n == 4 && p[0] == 3 && p[3] == 6, "列: 輪の頭から続きを読める");
}

// ================================================================
// MicFunctions
// ================================================================

static I2S* In(){ return I2S::last_input; }

static void Feed(const std::vector<uint32_t>& w){ for(uint32_t v : w) In()->rx.push_back(v); }

// 2コア目と1コア目を交互に回す
static void Step(int times = 1){
    for(int i = 0; i < times; i++){
        MicFunctions::LoopCore1();
        MicFunctions::Update();
    }
}

static uint32_t Le32(const std::string& s, size_t at){
    return (uint8_t)s[at] | ((uint8_t)s[at + 1] << 8) | ((uint8_t)s[at + 2] << 16) | ((uint32_t)(uint8_t)s[at + 3] << 24);
}

static void TestRecording(){
    HostSd::files.clear();
    OSData::SD_usable = true;
    PicoHostClock::now = 1000;
    MicFunctions::Setup();
    check(In() != nullptr, "録音: 入力のI2Sがある");
    check(MicFunctions::InputState() == MicFunctions::Input::Off, "録音: 使うまでI2Sは止まっている");
    Step();
    check(!In()->running, "録音: 何も頼まなければ開始しない");

    //メーター。スリープ中にクロックを下げていても(ClockFunctions)、I2Sを始める前に通常へ戻す
    ClockFunctions::Setup();
    ClockFunctions::Request(3);
    ClockFunctions::Apply(!MicFunctions::Core1Busy());
    check(ClockFunctions::Divider() == 3, "メーター: (クロックを下げておく)");
    MicFunctions::SetMonitor(true);
    check(MicFunctions::InputState() == MicFunctions::Input::Starting, "メーター: 2コア目が開始するまでStarting");
    Step();
    check(ClockFunctions::Divider() == 1 && MicFunctions::Core1Busy(), "メーター: I2Sを始める前にクロックを戻す");
    ClockFunctions::Request(1);
    check(In()->running && In()->bps == 32 && In()->sample_rate == 48000, "メーター: 48000Hz・32bitで開始");
    check(In()->bclk == MIC_I2S_BCLK && In()->data == MIC_I2S_DATA, "メーター: GP26/GP28");
    check(MicFunctions::InputState() == MicFunctions::Input::Running, "メーター: Running");
    Feed(Tone(1000, 20000, -3000, 4800, false));
    Step(8);
    check(MicFunctions::TakePeak() > 1000, "メーター: 山が読める");
    check(MicFunctions::HasSignal(), "メーター: 信号あり");
    MicFunctions::SetMonitor(false);
    Step();
    check(!In()->running, "メーター: やめるとI2Sを返す");

    //録音(メーター無しでも始められる)
    check(MicFunctions::StartRecording() == MicFunctions::RecError::None, "録音: 始められる");
    check(strcmp(MicFunctions::RecordingPath(), "/recordings/rec_0001.wav") == 0, "録音: 空いている番号の名前");
    check(MicFunctions::StartRecording() == MicFunctions::RecError::Busy, "録音: 2つ同時には録れない");
    check(!MicFunctions::SetSampleRate(48000), "録音: 録音中は周波数を変えられない");
    Step();
    check(In()->running, "録音: I2Sを開始");
    //ワードを奇数個ずつ届けても左右の組がずれない
    const std::vector<uint32_t> tone = Tone(1000, 20000, -3000, 48000, false);
    size_t pos = 0;
    const size_t chunks[] = {3, 1, 255, 2, 77, 1001};
    int ci = 0;
    while(pos < tone.size()){
        size_t n = chunks[ci++ % 6];
        if(pos + n > tone.size()) n = tone.size() - pos;
        for(size_t i = 0; i < n; i++) In()->rx.push_back(tone[pos + i]);
        pos += n;
        MicFunctions::LoopCore1();
        if(ci % 8 == 0) MicFunctions::Update();
    }
    Step(4);
    check(MicFunctions::RecordedMs() >= 900, "録音: 1秒ぶん書かれる(溜まった分は1コア目が書く)");
    MicFunctions::StopRecording();
    check(MicFunctions::IsFinishing(), "録音: 止めると後片付け中");
    MicFunctions::Update();
    check(MicFunctions::IsRecording(), "録音: 2コア目が手放すまでは閉じない");
    Step(2);
    check(!MicFunctions::IsRecording(), "録音: 2コア目が手放したら閉じる");
    check(MicFunctions::LastStopReason() == MicFunctions::StopReason::User, "録音: 理由は利用者");
    check(!In()->running, "録音: メーターも無ければI2Sを返す");

    const std::string& f = HostSd::files["/recordings/rec_0001.wav"];
    check(f.size() == 44 + 32000, "録音: 1秒=16000サンプル=32000バイト");
    check(f.compare(0, 4, "RIFF") == 0 && f.compare(8, 8, "WAVEfmt ") == 0 && f.compare(36, 4, "data") == 0, "WAV: 見出し");
    check(Le32(f, 4) == f.size() - 8 && Le32(f, 40) == f.size() - 44, "WAV: 大きさの欄が書き直されている");
    check(Le32(f, 24) == 16000 && Le32(f, 28) == 32000, "WAV: 16000Hz");
    std::vector<int16_t> pcm((f.size() - 44) / 2);
    memcpy(pcm.data(), f.data() + 44, pcm.size() * 2);
    const int zc = Crossings(pcm, 4000);
    check(zc >= 748 && zc <= 752, "WAV: 1kHzのまま(左右がずれていない)");
    check(fabs(Mean(pcm, 4000)) < 50, "WAV: 直流が落ちている");
    //既定の+18dBで 5000×7.94 ≒ 39700 → 頭打ち
    int16_t mx = 0;
    for(int16_t v : pcm) if(v > mx) mx = v;
    check(mx == 32767, "WAV: 既定の増幅(+18dB)が掛かる");

    //次は2番
    check(MicFunctions::StartRecording() == MicFunctions::RecError::None
        && strcmp(MicFunctions::RecordingPath(), "/recordings/rec_0002.wav") == 0, "録音: 番号が進む");
    Step();
    //1コア目が止まっている間に列(16384)より多く届くと取りこぼす
    Feed(Tone(440, 1000, 0, 3 * 20000, false));
    for(int i = 0; i < 400; i++) MicFunctions::LoopCore1();
    check(MicFunctions::DroppedSamples() == 20000 - MicFunctions::kRingSamples, "録音: 溢れた分を数える");
    In()->overflow = true;
    MicFunctions::LoopCore1();
    check(MicFunctions::Overruns() == 1, "録音: DMAの溢れを数える");
    //5秒たつと見出しを書き直す(電池が切れても読めるように)。1回のUpdate()で書くのは8192まで
    Step();
    PicoHostClock::now += MicFunctions::kHeaderUpdateMs;
    Step();
    const std::string& f2 = HostSd::files["/recordings/rec_0002.wav"];
    check(Le32(f2, 40) == MicFunctions::kRingSamples * 2 && f2.size() == 44 + MicFunctions::kRingSamples * 2,
        "録音: 録音中も見出しの大きさが追いつく");
    MicFunctions::StopRecording();
    Step(2);
    check(!MicFunctions::IsRecording(), "録音: 止まる");

    //I2Sを開始できない
    In()->fail_begin = true;
    check(MicFunctions::StartRecording() == MicFunctions::RecError::None, "失敗: 録音は始まる");
    MicFunctions::LoopCore1();
    check(MicFunctions::InputState() == MicFunctions::Input::Failed, "失敗: Failed");
    Step(3);
    check(!MicFunctions::IsRecording() && MicFunctions::LastStopReason() == MicFunctions::StopReason::InputFailed,
        "失敗: 録音はI2Sの失敗で止まる");
    In()->fail_begin = false;
    MicFunctions::SetMonitor(true);
    Step();
    check(In()->running, "失敗: 使い直すともう一度試す");
    MicFunctions::SetMonitor(false);
    Step();

    //SDが無い
    OSData::SD_usable = false;
    check(MicFunctions::StartRecording() == MicFunctions::RecError::NoSd, "SD無し: 断る");
    OSData::SD_usable = true;
}

static void TestConfig(){
    HostSd::files["/sys/mic.cfg"] = "rate = 48000\ngain = 6\nchannel = right\nrate = 11025\n";
    MicFunctions::Setup();
    check(MicFunctions::SampleRate() == 48000, "mic.cfg: rate(読めない値は無視)");
    check(MicFunctions::GainDb() == 6, "mic.cfg: gain");
    check(MicFunctions::RightChannel(), "mic.cfg: channel=right");
    MicFunctions::SetGainDb(30);
    check(HostSd::files["/sys/mic.cfg"].find("gain=30") != std::string::npos, "mic.cfg: 増幅を書く");
    check(MicFunctions::SetSampleRate(24000) && HostSd::files["/sys/mic.cfg"].find("rate=24000") != std::string::npos, "mic.cfg: 周波数を書く");
    check(!MicFunctions::SetSampleRate(44100), "mic.cfg: 使えない周波数は断る");

    //右チャンネル・24000Hz・+30dB で録る
    MicFunctions::SetGainDb(0);
    HostSd::files.erase("/recordings/rec_0003.wav");
    check(MicFunctions::StartRecording("/recordings/test.wav") == MicFunctions::RecError::None, "名前を指定して録る");
    Step();
    Feed(Tone(1000, 20000, 0, 4800, true));
    Step(30);
    MicFunctions::StopRecording();
    Step(2);
    const std::string& f = HostSd::files["/recordings/test.wav"];
    check(f.size() == 44 + 2400 * 2 && Le32(f, 24) == 24000, "24000Hz: 4800組 → 2400サンプル");
    std::vector<int16_t> pcm((f.size() - 44) / 2);
    memcpy(pcm.data(), f.data() + 44, pcm.size() * 2);
    check(Rms(pcm, 600) > 3000, "右チャンネルを録る");
}

int main(){
    TestDsp();
    TestRing();
    TestRecording();
    TestConfig();
    printf("\n%s (%d failures)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
