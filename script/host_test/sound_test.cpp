// 音声出力(SoundFunctions)とチップチューン音源(ChipSynth::Engine)の検証。
//
// 前半は音源そのもの: 波形(矩形のデューティ/三角/のこぎり/ノイズの周期)、長さ、エンベロープ、
// 足し合わせ、全体の音量。
//
// 後半はSoundFunctions。実機では1コア目(UpdateAt)と2コア目(Core1StepAt)が別々に回るが、
// ここでは1本のスレッドで交互に呼ぶ(Step())。I2Sとアンプの代わりに stubs/I2S.h
// (書かれたワードを溜めるだけ)と stubs/Arduino.h の HostGpio::read_hook(検出ピン)を使い、
// 「アンプの抜き差しの見分け方」「刺さっている間だけI2Sを動かすこと」「刺さっていない間も
// 音が時間どおりに進むこと」「コマンドの列」「sound.cfg」を確かめる。
// SoundFunctionsの状態は1つしか無いので、1本の筋書きとして順に進める。
#include "functions/Sound_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/Battery_Functions.hpp"
#include "sound/Chip_Synth.hpp"
#include "sound/Note_Name.hpp"
#include "consts.hpp"
#include "OS_Data.hpp"

#include <Arduino.h>
#include <I2S.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}

using namespace SoundFunctions;
using ChipSynth::Note;
using ChipSynth::Wave;

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

// ================================================================
// 音源
// ================================================================

static const uint32_t kRate = 22050;

static std::vector<int16_t> Render(ChipSynth::Engine& e, size_t n){
    std::vector<int16_t> v(n);
    e.render(v.data(), n);
    return v;
}

static int SignFlips(const std::vector<int16_t>& v){
    int flips = 0;
    for(size_t i = 1; i < v.size(); i++) if((v[i] > 0) != (v[i - 1] > 0)) flips++;
    return flips;
}

static double PositiveRatio(const std::vector<int16_t>& v){
    size_t pos = 0;
    for(int16_t s : v) if(s > 0) pos++;
    return (double)pos / v.size();
}

static Note MakeNote(Wave w, double hz, uint32_t ms = 0, uint8_t vol = 15, int8_t env = 0){
    Note n;
    n.wave = w;
    n.freq_x16 = (uint32_t)(hz * 16 + 0.5);
    n.length_ms = ms;
    n.volume = vol;
    n.envelope = env;
    return n;
}

static void TestSynth(){
    const int32_t A = ChipSynth::Engine::kChannelAmplitude;

    printf("--- 音源: 何も鳴っていないとき ---\n");
    {
        ChipSynth::Engine e(kRate);
        const auto v = Render(e, 100);
        bool silent = true;
        for(int16_t s : v) if(s != 0) silent = false;
        check(silent && e.activeMask() == 0, "無音でどのチャンネルも鳴っていない");
    }

    printf("--- 音源: 矩形波 ---\n");
    {
        ChipSynth::Engine e(kRate);
        e.play(0, MakeNote(Wave::Pulse50, 441));        //22050/441 = 50サンプルでちょうど1周期
        check(e.activeMask() == 0x1, "ch0が鳴っている");
        const auto v = Render(e, kRate);                //1秒
        int16_t mx = -32768, mn = 32767;
        for(int16_t s : v){ mx = std::max(mx, s); mn = std::min(mn, s); }
        check(mx == A - 1 || mx == A, "音量15・全体100の振幅はkChannelAmplitude");
        check(mn == -A, "負側も同じ振幅");
        const int flips = SignFlips(v);
        printf("       符号の切り替わり %d回(期待 882)\n", flips);
        check(abs(flips - 882) <= 2, "441Hz(1秒で約882回切り替わる)");
        check(fabs(PositiveRatio(v) - 0.5) < 0.01, "デューティ50%");
    }
    {
        const struct { Wave w; double duty; const char* label; } cases[] = {
            { Wave::Pulse12, 0.125, "デューティ12.5%" },
            { Wave::Pulse25, 0.25,  "デューティ25%" },
            { Wave::Pulse75, 0.75,  "デューティ75%" },
        };
        for(const auto& c : cases){
            ChipSynth::Engine e(kRate);
            e.play(1, MakeNote(c.w, 441));
            check(fabs(PositiveRatio(Render(e, kRate)) - c.duty) < 0.01, c.label);
        }
    }

    printf("--- 音源: 三角波 / のこぎり波 ---\n");
    {
        ChipSynth::Engine e(kRate);
        e.play(0, MakeNote(Wave::Triangle, 441));
        const auto v = Render(e, 50);   //ちょうど1周期
        //前半は上がり続け、後半は下がり続ける
        bool up = true, down = true;
        for(int i = 1; i < 25; i++) if(v[i] < v[i - 1]) up = false;
        for(int i = 26; i < 50; i++) if(v[i] > v[i - 1]) down = false;
        check(up && down, "三角波: 前半で上がり後半で下がる");
        check(v[0] <= -A + A / 20 && v[25] >= A - A / 10, "三角波: 下端から上端まで振れる");
    }
    {
        ChipSynth::Engine e(kRate);
        e.play(0, MakeNote(Wave::Saw, 441));
        const auto v = Render(e, 50);
        bool up = true;
        for(int i = 1; i < 50; i++) if(v[i] < v[i - 1]) up = false;
        check(up, "のこぎり波: 1周期のあいだ上がり続ける");
    }

    printf("--- 音源: ノイズ ---\n");
    {
        //1サンプルに1段ずつ進めて、周期を確かめる(短い方は127、長い方は32767)
        ChipSynth::Engine e(kRate);
        e.play(0, MakeNote(Wave::NoiseShort, kRate));
        const auto v = Render(e, 127 * 3);
        bool periodic = true;
        for(int i = 0; i < 127 * 2; i++) if(v[i] != v[i + 127]) periodic = false;
        check(periodic, "短いノイズは127段で一巡する");
        check(PositiveRatio(v) > 0.3 && PositiveRatio(v) < 0.7, "短いノイズは正負が混ざる");
    }
    {
        ChipSynth::Engine e(kRate);
        e.play(0, MakeNote(Wave::Noise, kRate));
        const auto v = Render(e, 32767 + 1000);
        bool same127 = true;
        for(int i = 0; i < 1000; i++) if(v[i] != v[i + 127]) same127 = false;
        bool same32767 = true;
        for(int i = 0; i < 1000; i++) if(v[i] != v[i + 32767]) same32767 = false;
        check(!same127, "長いノイズは127段では一巡しない");
        check(same32767, "長いノイズは32767段で一巡する");
    }

    printf("--- 音源: 長さ ---\n");
    {
        ChipSynth::Engine e(kRate);
        e.play(2, MakeNote(Wave::Pulse50, 441, 100));
        const auto v = Render(e, kRate / 10 + 100);
        size_t last = 0;
        for(size_t i = 0; i < v.size(); i++) if(v[i] != 0) last = i;
        check(last == kRate / 10 - 1, "100ms(2205サンプル)で止まる");
        check(e.activeMask() == 0, "止まったら鳴っていない扱い");
    }
    {
        ChipSynth::Engine e(kRate);
        e.play(2, MakeNote(Wave::Pulse50, 441, 100));
        e.render(nullptr, kRate / 10 - 1);
        check(e.activeMask() == 0x4, "作らずに進めても長さを数える(残り1サンプル)");
        e.render(nullptr, 1);
        check(e.activeMask() == 0, "ちょうど2205サンプルで終わる");
    }
    {
        ChipSynth::Engine e(kRate);
        e.play(0, MakeNote(Wave::Pulse50, 441, 0));
        e.render(nullptr, kRate * 5);
        check(e.activeMask() == 0x1, "長さ0は止めるまで鳴り続ける");
        e.stop(0);
        check(e.activeMask() == 0, "stop()で止まる");
    }

    printf("--- 音源: エンベロープ ---\n");
    {
        //-1 = 1/64秒(344サンプル)ごとに1段下げる。15段で消える
        ChipSynth::Engine e(kRate);
        e.play(0, MakeNote(Wave::Pulse50, 441, 0, 15, -1));
        const uint32_t step = kRate / 64;
        const auto v = Render(e, step * 15 + 10);
        const int32_t first = abs(v[0]);
        const int32_t later = abs(v[step * 7 + 1]);     //7段下がった後(音量8)
        check(first == A || first == A - 1, "鳴り始めは音量15");
        check(abs(later - A * 8 / 15) <= 1, "7段下がると音量8の振幅");
        check(e.activeMask() == 0, "15段下がり切ったら終わる");
        check(v[step * 15 + 5] == 0, "終わった後は無音");
    }
    {
        ChipSynth::Engine e(kRate);
        e.play(0, MakeNote(Wave::Pulse50, 441, 0, 0, 2));
        check(e.activeMask() == 0x1, "音量0から上げる音は鳴っている扱い");
        const auto v = Render(e, kRate / 32 * 16);
        check(v[0] == 0 && abs(v[v.size() - 1]) >= A - 1, "音量0から15まで上がる");
        check(e.activeMask() == 0x1, "上がり切ったら15のまま鳴り続ける");
    }
    {
        ChipSynth::Engine e(kRate);
        e.play(0, MakeNote(Wave::Pulse50, 441, 0, 0, 0));
        check(e.activeMask() == 0, "音量0で一定の音は最初から鳴っていない扱い");
    }

    printf("--- 音源: 足し合わせと全体の音量 ---\n");
    {
        //全チャンネルを同時に鳴らしたときの上限(kHeadroomChannelsを超えた分は比率で下げられる。
        //Chip_Synth.hpp「同時発音数」参照)。kChannels<=kHeadroomChannelsなら単純にA*kChannels
        const int nominal = ChipSynth::kHeadroomChannels;
        const int32_t fullMask = (int32_t)(((uint64_t)1 << ChipSynth::kChannels) - 1);
        const int32_t expectedFull = A * std::min<int>(ChipSynth::kChannels, nominal);

        ChipSynth::Engine e(kRate);
        for(uint8_t ch = 0; ch < ChipSynth::kChannels; ch++) e.play(ch, MakeNote(Wave::Pulse50, 441));
        check(e.activeMask() == (ChipSynth::ChannelMask)fullMask, "全チャンネル同時に鳴る");
        const auto v = Render(e, 50);
        //全チャンネルが同じ位相で重なっても、kHeadroomChannels分を超えて歪みはしない(溢れない)
        check(v[0] >= expectedFull - 4 && v[0] <= 32767, "全チャンネル分が足される(溢れない)");
        e.setMasterVolume(50);
        const auto h = Render(e, 50);
        check(abs(abs(h[0]) - expectedFull / 2) <= 8, "全体の音量50で半分");
        e.setMasterVolume(0);
        const auto z = Render(e, 50);
        bool silent = true;
        for(int16_t s : z) if(s != 0) silent = false;
        check(silent && e.activeMask() == (ChipSynth::ChannelMask)fullMask, "全体の音量0は無音だが鳴っている扱い");
        e.play((uint8_t)(ChipSynth::kChannels + 50), MakeNote(Wave::Pulse50, 441));
        check(e.activeMask() == (ChipSynth::ChannelMask)fullMask, "範囲外のチャンネルは無視");
        e.stopAll();
        check(e.activeMask() == 0, "stopAll()で全部止まる");
    }

    printf("--- 音名 ---\n");
    check(NoteName::Parse("A4") == 69 && NoteName::Parse("C4") == 60, "A4=69 / C4=60");
    check(NoteName::Parse("c#4") == 61 && NoteName::Parse("Db4") == 61, "C#4とDb4は同じ(大小を問わない)");
    check(NoteName::Parse("C-1") == 0 && NoteName::Parse("G9") == 127, "C-1〜G9");
    check(NoteName::Parse("H4") < 0 && NoteName::Parse("C") < 0 && NoteName::Parse("C44") < 0
          && NoteName::Parse("G#9") < 0, "読めない名前は-1");
    check(fabsf(NoteName::MidiToFreq(69) - 440.0f) < 0.01f, "69は440Hz");
    check(fabsf(NoteName::MidiToFreq(60) - 261.63f) < 0.01f, "60は261.63Hz");
    check(NoteName::MidiToFreq(128) == 0.0f, "範囲外は0");
}

// ================================================================
// SoundFunctions(1コア目と2コア目)
// ================================================================

// 検出ピンの状態(true=アンプが刺さっている=LOW)
static bool plugged = false;
static int ReadHook(int pin){
    if(pin != AUDIO_DETECT) return -1;
    return plugged ? LOW : HIGH;
}

static I2S& Out(){ return *I2S::last; }
static int16_t Left(uint32_t w){ return (int16_t)(w >> 16); }
static int16_t Right(uint32_t w){ return (int16_t)(w & 0xFFFF); }

// 1コア目と2コア目を1回ずつ進める
static void Step(unsigned long now){
    UpdateAt(now);
    Core1StepAt(now);
}

// 刺さっている間: DMAが全部送ったことにして次を書かせる、を鳴り終わるまで繰り返し、
// 書かれた音のあるサンプル数を数える
static size_t DrainTone(unsigned long& now){
    size_t nonzero = 0;
    for(int guard = 0; guard < 5000 && IsPlaying(); guard++){
        Out().consume(Out().queued.size());
        const size_t before = Out().written.size();
        now += 1;
        Step(now);
        for(size_t i = before; i < Out().written.size(); i++){
            if(Left(Out().written[i]) != 0) nonzero++;
        }
    }
    return nonzero;
}

static void TestSoundFunctions(){
    HostGpio::read_hook = &ReadHook;
    unsigned long now = 1000;

    printf("--- 1コア目のSetup()より前は2コア目は何もしない ---\n");
    plugged = true;
    check(!Core1StepAt(now), "何もしない");
    check(Out().begin_count == 0, "I2Sも開始しない");
    plugged = false;

    printf("--- 起動時: アンプ無し ---\n");
    SetupAt(now);
    Core1StepAt(now);
    check(GetState() == State::Disconnected, "未接続");
    check(!IsConnected() && !IsAvailable(), "IsConnected/IsAvailableともfalse");
    check(Out().begin_count == 0, "I2Sは開始しない");
    check(HostGpio::last_written[AUDIO_SHUTDOWN] == LOW, "休止端子はLOW");
    check(GetVolume() == kDefaultVolume && GetOutput() == Output::Auto, "sound.cfgが無ければ既定値");

    printf("--- 未接続でも音は時間どおりに進む ---\n");
    Beep(1000, 100);
    check(IsPlaying(), "Beep直後(2コア目がまだ受け取っていない)でも鳴っていることになっている");
    Step(now);
    check(IsPlaying() && ActiveChannels() == 0x1, "2コア目が受け取るとch0が鳴っている");
    now += 50; Step(now);
    check(IsPlaying(), "50ms後もまだ鳴っている");
    now += 51; Step(now);
    check(!IsPlaying(), "100msを過ぎたら終わる");
    check(Out().begin_count == 0 && Out().written.empty(), "その間I2Sには何も書かない");

    printf("--- 抜き差しのばたつきは採用しない ---\n");
    plugged = true;
    now += kDetectIntervalMs; Step(now);
    plugged = false;
    now += kDetectIntervalMs; Step(now);
    now += kDetectIntervalMs; Step(now);
    check(!IsConnected(), "1回だけLOWになっても接続にしない");

    printf("--- 刺すと続けて同じ値になった時点で採用 ---\n");
    plugged = true;
    for(int i = 0; i < kDetectStableCount - 1; i++){
        now += kDetectIntervalMs; Step(now);
    }
    check(!IsConnected(), "規定回数に届くまでは未接続のまま");
    now += kDetectIntervalMs;
    UpdateAt(now);
    check(IsConnected() && GetState() == State::Disconnected, "1コア目が採用しても、2コア目が開始するまではActiveにしない");
    Core1StepAt(now);
    check(GetState() == State::Active && IsAvailable(), "2コア目がI2Sを開始したらActive");
    check(Out().begin_count == 1 && Out().running, "I2Sを1回だけ開始");
    check(Out().bclk == AUDIO_I2S_BCLK && Out().data == AUDIO_I2S_DATA, "ピンはconsts.hppのとおり");
    check(Out().bps == 16 && Out().sample_rate == (long)kSampleRate, "16bit / kSampleRate");
    check(Out().capacity == (size_t)kBufferWords * kBufferCount, "バッファの大きさ");
    check(HostGpio::last_written[AUDIO_SHUTDOWN] == HIGH, "休止端子はHIGH");
    check(Out().queued.size() == Out().capacity, "開始したその回でバッファを埋める");

    printf("--- 空いた分だけ書き足す ---\n");
    Out().consume(100);
    const size_t before = Out().written.size();
    check(Core1StepAt(now), "書いた回はtrue");
    check(Out().written.size() - before == 100, "送られた100サンプル分だけ書く");
    check(!Core1StepAt(now), "満杯で何もすることが無ければfalse(呼び出し側が休む)");
    check(Out().written.size() - before == 100, "満杯なら何も書かない");

    printf("--- 音の中身と音量 ---\n");
    SetVolume(100);
    Out().consume(Out().queued.size());
    Step(now);      //無音でバッファを埋め直す(ここまでに作った分を流し切る)
    Out().consume(Out().queued.size());
    const size_t tone_start = Out().written.size();
    Beep(1000, 100);
    DrainTone(now);
    {
        //Beep()より前に作ってあった分(一時置き場の残り、最大64サンプル)の無音は読み飛ばす
        size_t i = tone_start;
        while(i < Out().written.size() && Left(Out().written[i]) == 0) i++;
        check(i - tone_start <= 64, "Beep前の無音は一時置き場の分(64サンプル)まで");
        bool lr_same = true, full_amp = true;
        size_t n = 0;
        for(; i < Out().written.size(); i++){
            const uint32_t w = Out().written[i];
            if(Left(w) != Right(w)) lr_same = false;
            const int16_t s = Left(w);
            if(s == 0) break;
            const int32_t a = abs(s);
            if(a != ChipSynth::Engine::kChannelAmplitude && a != ChipSynth::Engine::kChannelAmplitude - 1) full_amp = false;
            n++;
        }
        printf("       サンプル数 %zu\n", n);
        check(lr_same, "左右に同じ値");
        check(full_amp, "音量100で振幅はkChannelAmplitude");
        check(n == kSampleRate / 10, "100ms分のサンプル数で止まる");
    }
    SetVolume(150);
    check(GetVolume() == 100, "100を超えたら100へ丸める");
    SetVolume(-5);
    check(GetVolume() == 0, "負なら0へ丸める");
    SetVolume(50);

    printf("--- コマンドの列 ---\n");
    {
        //2コア目を回さずに積み続けると、列の大きさを超えた分は捨てる
        Note n = MakeNote(Wave::Triangle, 220, 0);
        int ok = 0;
        for(int i = 0; i < kCommandQueueSize + 8; i++) if(Play(1, n)) ok++;
        check(ok == kCommandQueueSize, "列の大きさまでは積める");
        check(DroppedCommands() == 8, "溢れた分は数えて捨てる");
        check(!Play(kChannels, n), "範囲外のチャンネルは積まない");
        Core1StepAt(now);
        check(ActiveChannels() == 0x2, "2コア目が取り出して鳴らす");
        check(Play(1, n), "取り出した後はまた積める");
        Stop(1);
        Core1StepAt(now);
        check(ActiveChannels() == 0, "Stop()で止まる");
        Play(0, n); Play(2, n); Play(3, n);
        StopAll();
        Core1StepAt(now);
        check(ActiveChannels() == 0 && !IsPlaying(), "StopAll()で全部止まる");
    }

    printf("--- 抜くとI2Sを止める ---\n");
    Out().consume(Out().queued.size());
    Beep(1000, 1000);
    Core1StepAt(now);   //2コア目が受け取り、バッファ1本分(512サンプル)を書く
    plugged = false;
    for(int i = 0; i < kDetectStableCount; i++){
        now += kDetectIntervalMs; Step(now);
    }
    check(!IsConnected() && GetState() == State::Disconnected, "未接続へ戻る");
    check(!Out().running && Out().end_count == 1, "I2Sを止めた(バッファを返した)");
    check(HostGpio::last_written[AUDIO_SHUTDOWN] == LOW, "休止端子はLOW");

    printf("--- 途中で刺し直すと続きから鳴る ---\n");
    //鳴らせる間はI2Sが引き取った量だけ進む。このスタブは consume() しない限り引き取らないので、
    //抜けるまでに進んだのは受け取った時に書いたバッファ1本分と一時置き場の残りだけ。
    //止めた後は時間で進む(抜いたと判定されてから、刺したと判定されるまで = 検出待ち kDetectStableCount 回分)
    plugged = true;
    for(int i = 0; i < kDetectStableCount; i++){
        now += kDetectIntervalMs; Step(now);
    }
    check(IsConnected() && Out().running && Out().begin_count == 2, "刺し直すとI2Sを開始し直す");
    check(IsPlaying(), "まだ鳴っている");
    {
        size_t queued_nonzero = 0;
        for(uint32_t w : Out().queued) if(Left(w) != 0) queued_nonzero++;
        const size_t rest = queued_nonzero + DrainTone(now);
        //鳴らした長さ(1秒) - 抜く前に作った分(バッファ+一時置き場の64) - 抜けていた間
        const long expect = (long)kSampleRate
                          - (long)Out().capacity - 64
                          - (long)kSampleRate * kDetectIntervalMs * kDetectStableCount / 1000;
        printf("       刺し直した後に鳴ったサンプル数: %zu (期待 約%ld)\n", rest, expect);
        check(labs((long)rest - expect) <= 64, "抜けていた間の分を飛ばして、残りだけが鳴る");
    }

    printf("--- sound.cfg: output=off / volume ---\n");
    OSData::SD_usable = true;
    HostSd::files["/sys/sound.cfg"] = "# テスト\noutput = off\nvolume = 30\n";
    const int end_before = Out().end_count;
    SetupAt(now);
    Core1StepAt(now);
    check(GetVolume() == 30, "volumeを読む");
    check(GetOutput() == Output::Off, "outputを読む");
    check(IsConnected() && GetState() == State::Muted, "刺さっていても消音");
    check(!IsAvailable(), "IsAvailableはfalse");
    check(!Out().running && Out().end_count == end_before + 1, "I2Sは止める");
    SetOutput(Output::Auto);
    Core1StepAt(now);
    check(GetState() == State::Active && Out().running, "autoへ戻すと鳴らせる");

    printf("--- sound.cfg: battery-cap ---\n");
    {
        //電池駆動(PCの疑似値: 80%・USB給電なし)なら既定で頭打ちを掛ける
        setenv("PICOOS_BATTERY_PERCENT", "80", 1);
        BatteryFunctions::Setup();
        HostSd::files["/sys/sound.cfg"] = "volume = 50\n";
        SetupAt(now);
        UpdateAt(now);
        check(IsBatteryVolumeCapActive(), "既定では電池駆動中に頭打ちする");
        HostSd::files["/sys/sound.cfg"] = "volume = 50\nbattery-cap = false\n";
        SetupAt(now);
        UpdateAt(now);
        check(!IsBatteryVolumeCapActive(), "battery-cap = false なら頭打ちしない(ライン出力のDAC向け)");
        HostSd::files["/sys/sound.cfg"] = "volume = 50\n";
        SetupAt(now);
        UpdateAt(now);
        check(IsBatteryVolumeCapActive(), "キーを消すと既定へ戻る");
        //後のテストへ影響しないようUSB給電に戻す
        setenv("PICOOS_BATTERY_PERCENT", "100", 1);
        BatteryFunctions::Setup();
        UpdateAt(now);
        check(!IsBatteryVolumeCapActive(), "USB給電なら頭打ちしない");
        HostSd::files["/sys/sound.cfg"] = "# テスト\noutput = off\nvolume = 30\n";
        SetupAt(now);
        SetOutput(Output::Auto);
        Core1StepAt(now);
    }

    printf("--- I2Sを開始できなかったとき ---\n");
    SetOutput(Output::Off);
    Core1StepAt(now);
    Out().fail_begin = true;
    const int begins = Out().begin_count;
    SetOutput(Output::Auto);
    for(int i = 0; i < 5; i++){ now += kDetectIntervalMs; Step(now); }
    check(GetState() == State::Disconnected && !IsAvailable(), "鳴らせない扱い");
    check(Out().begin_count == begins + 1, "失敗したら試し直さない(1回だけ)");
    Out().fail_begin = false;
    plugged = false;
    for(int i = 0; i < kDetectStableCount; i++){ now += kDetectIntervalMs; Step(now); }
    plugged = true;
    for(int i = 0; i < kDetectStableCount; i++){ now += kDetectIntervalMs; Step(now); }
    check(GetState() == State::Active, "刺し直せば試し直す");

    printf("--- 省電力(スリープ中): 何も鳴っていなければI2Sとアンプを止める ---\n");
    StopAll();
    Step(now);
    check(GetState() == State::Active && Out().running, "省電力の前は動いている");
    check(IdleDelayMs() == 1, "省電力でなければ2コア目は1msずつ休む");
    SetPowerSave(true);
    check(IsPowerSave(), "省電力を掛けた");
    Step(now);
    check(!Out().running, "何も鳴っていなければI2Sを止める");
    check(HostGpio::last_written[AUDIO_SHUTDOWN] == LOW, "アンプも休止端子で止める");
    check(IdleDelayMs() == kPowerSaveIdleDelayMs, "止まっている間は2コア目を長く休ませる");
    check(IsConnected(), "アンプの検出はそのまま(刺さっている扱い)");

    Beep(880, 100);
    Step(now);
    check(Out().running && HostGpio::last_written[AUDIO_SHUTDOWN] == HIGH, "鳴らす要求が来れば動き直す");
    check(IdleDelayMs() == 1, "動いている間は1msに戻る");
    now += 200;
    Step(now);
    Out().consume(Out().queued.size());
    for(int i = 0; i < 20 && Out().running; i++){ now += 20; Out().consume(Out().queued.size()); Step(now); }
    check(!Out().running, "鳴り終わったらまた止める");

    SetPowerSave(false);
    Step(now);
    check(Out().running && !IsPowerSave(), "省電力を解けば動き直す");
}

int main(){
    TestSynth();
    TestSoundFunctions();

    printf("\n%s (%d件の失敗)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
