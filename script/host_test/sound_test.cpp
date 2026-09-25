// 音声出力(SoundFunctions)の検証。
//
// 実機のI2Sとアンプの代わりに stubs/I2S.h(書かれたワードを溜めるだけ)と
// stubs/Arduino.h の HostGpio::read_hook(検出ピンの読み取り)を使い、
// 「アンプの抜き差しの見分け方」「刺さっている間だけI2Sを動かすこと」
// 「刺さっていない間も音が時間どおりに進むこと」「sound.cfg」を確かめる。
//
// SoundFunctionsの状態は名前空間の中に1つしか無いので、1本の筋書きとして順に進める。
#include "functions/Sound_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "consts.hpp"
#include "OS_Data.hpp"

#include <Arduino.h>
#include <I2S.h>
#include <cstdio>
#include <cstdlib>

void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}

using namespace SoundFunctions;

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

// 検出ピンの状態(true=アンプが刺さっている=LOW)
static bool plugged = false;
static int ReadHook(int pin){
    if(pin != AUDIO_DETECT) return -1;
    return plugged ? LOW : HIGH;
}

static I2S& Out(){ return *I2S::last; }

static int16_t Left(uint32_t w){ return (int16_t)(w >> 16); }
static int16_t Right(uint32_t w){ return (int16_t)(w & 0xFFFF); }

// 刺さっている間: DMAが全部送ったことにして次を書かせる、を繰り返し、
// 鳴り終わるまでに書かれた音のあるサンプル数を数える
static size_t DrainTone(unsigned long& now){
    size_t nonzero = 0;
    for(int guard = 0; guard < 1000 && IsPlaying(); guard++){
        Out().consume(Out().queued.size());
        const size_t before = Out().written.size();
        now += 10;
        UpdateAt(now);
        for(size_t i = before; i < Out().written.size(); i++){
            if(Left(Out().written[i]) != 0) nonzero++;
        }
    }
    return nonzero;
}

int main(){
    HostGpio::read_hook = &ReadHook;
    unsigned long now = 1000;

    printf("--- 起動時: アンプ無し ---\n");
    plugged = false;
    SetupAt(now);
    check(I2S::last != nullptr, "I2Sの実体がある");
    check(GetState() == State::Disconnected, "未接続");
    check(!IsConnected() && !IsAvailable(), "IsConnected/IsAvailableともfalse");
    check(Out().begin_count == 0, "I2Sは開始しない");
    check(HostGpio::last_written[AUDIO_SHUTDOWN] == LOW, "休止端子はLOW");
    check(GetVolume() == kDefaultVolume && GetOutput() == Output::Auto, "sound.cfgが無ければ既定値");

    printf("--- 未接続でも音は時間どおりに進む ---\n");
    Beep(1000, 100);
    check(IsPlaying(), "Beep直後は鳴っていることになっている");
    now += 50; UpdateAt(now);
    check(IsPlaying(), "50ms後もまだ鳴っている");
    now += 51; UpdateAt(now);
    check(!IsPlaying(), "100msを過ぎたら終わる");
    check(Out().begin_count == 0 && Out().written.empty(), "その間I2Sには何も書かない");

    printf("--- 抜き差しのばたつきは採用しない ---\n");
    plugged = true;
    now += kDetectIntervalMs; UpdateAt(now);
    plugged = false;
    now += kDetectIntervalMs; UpdateAt(now);
    now += kDetectIntervalMs; UpdateAt(now);
    check(!IsConnected(), "1回だけLOWになっても接続にしない");

    printf("--- 刺すと続けて同じ値になった時点で採用 ---\n");
    plugged = true;
    for(int i = 0; i < kDetectStableCount - 1; i++){
        now += kDetectIntervalMs; UpdateAt(now);
    }
    check(!IsConnected(), "規定回数に届くまでは未接続のまま");
    now += kDetectIntervalMs; UpdateAt(now);
    check(IsConnected() && GetState() == State::Active && IsAvailable(), "規定回数で接続・鳴らせる");
    check(Out().begin_count == 1 && Out().running, "I2Sを1回だけ開始");
    check(Out().bclk == AUDIO_I2S_BCLK && Out().data == AUDIO_I2S_DATA, "ピンはconsts.hppのとおり");
    check(Out().bps == 16 && Out().sample_rate == (long)kSampleRate, "16bit / kSampleRate");
    check(Out().capacity == (size_t)kBufferWords * kBufferCount, "バッファの大きさ");
    check(HostGpio::last_written[AUDIO_SHUTDOWN] == HIGH, "休止端子はHIGH");
    check(Out().queued.size() == Out().capacity, "開始したフレームでバッファを埋める");

    printf("--- 空いた分だけ書き足す ---\n");
    Out().consume(100);
    const size_t before = Out().written.size();
    now += 5; UpdateAt(now);
    check(Out().written.size() - before == 100, "送られた100サンプル分だけ書く");
    now += 5; UpdateAt(now);
    check(Out().written.size() - before == 100, "満杯なら何も書かない");

    printf("--- 矩形波の中身 ---\n");
    Out().consume(Out().queued.size());
    const size_t tone_start = Out().written.size();
    SetVolume(100);
    Beep(1000, 100);
    now += 5; UpdateAt(now);
    DrainTone(now);     //バッファ1本(約93ms)に収まらないので、送らせながら最後まで書かせる
    {
        bool lr_same = true;
        bool full_amp = true;
        int flips = 0;
        int16_t prev = 0;
        size_t n = 0;
        //Beep()より前に作って書き込めずにいた1サンプル(無音)が先頭に来るので読み飛ばす
        size_t i = tone_start;
        while(i < Out().written.size() && Left(Out().written[i]) == 0) i++;
        check(i - tone_start <= 1, "Beep前の無音は高々1サンプル");
        for(; i < Out().written.size(); i++){
            const uint32_t w = Out().written[i];
            if(Left(w) != Right(w)) lr_same = false;
            const int16_t s = Left(w);
            if(s == 0) break;
            if(abs(s) != kMaxAmplitude) full_amp = false;
            if(prev != 0 && (s > 0) != (prev > 0)) flips++;
            prev = s;
            n++;
        }
        const size_t expect = (size_t)kSampleRate / 10;  //100ms
        check(lr_same, "左右に同じ値");
        check(full_amp, "音量100で振幅はkMaxAmplitude");
        printf("       サンプル数 %zu / 符号の切り替わり %d回\n", n, flips);
        check(n == expect, "100ms分のサンプル数で止まる");
        //1000Hz×0.1秒=100周期=符号の切り替わりは約200回
        check(flips >= 198 && flips <= 200, "1000Hzの矩形波(符号の切り替わりが約200回)");
    }

    printf("--- 音量 ---\n");
    SetVolume(150);
    check(GetVolume() == 100, "100を超えたら100へ丸める");
    SetVolume(-5);
    check(GetVolume() == 0, "負なら0へ丸める");
    Beep(440, 10);
    check(NextSample() == 0, "音量0なら無音");
    StopAll();
    check(!IsPlaying(), "StopAllで止まる");
    SetVolume(50);
    Beep(440, 10);
    check(abs(NextSample()) == kMaxAmplitude / 2, "音量50で振幅は半分");
    Beep(0, 100);
    check(!IsPlaying(), "周波数0なら止めるだけ");

    printf("--- 抜くとI2Sを止める ---\n");
    Out().consume(Out().queued.size());
    Beep(1000, 1000);
    plugged = false;
    for(int i = 0; i < kDetectStableCount; i++){
        now += kDetectIntervalMs; UpdateAt(now);
    }
    check(!IsConnected() && GetState() == State::Disconnected, "未接続へ戻る");
    check(!Out().running && Out().end_count == 1, "I2Sを止めた(バッファを返した)");
    check(HostGpio::last_written[AUDIO_SHUTDOWN] == LOW, "休止端子はLOW");

    printf("--- 途中で刺し直すと続きから鳴る ---\n");
    //鳴らせる間はI2Sが引き取った量だけ進む。このスタブは consume() しない限り引き取らないので、
    //抜けるまでに進んだのは最初に埋めたバッファ1本分だけ。止めた後は時間で進む
    //(抜いたと判定されてから、刺したと判定されるまで = 検出待ち kDetectStableCount 回分)
    plugged = true;
    for(int i = 0; i < kDetectStableCount; i++){
        now += kDetectIntervalMs; UpdateAt(now);
    }
    check(IsConnected() && Out().running && Out().begin_count == 2, "刺し直すとI2Sを開始し直す");
    check(IsPlaying(), "まだ鳴っている");
    {
        size_t queued_nonzero = 0;
        for(uint32_t w : Out().queued) if(Left(w) != 0) queued_nonzero++;
        const size_t rest = queued_nonzero + DrainTone(now);
        const size_t expect = (size_t)kSampleRate                               //鳴らした長さ(1秒)
                            - Out().capacity                                    //抜く前に送った分
                            - (size_t)kSampleRate * kDetectIntervalMs * kDetectStableCount / 1000; //抜けていた間
        printf("       刺し直した後に鳴ったサンプル数: %zu (期待 %zu)\n", rest, expect);
        check(rest + 2 >= expect && rest <= expect + 2, "抜けていた間の分を飛ばして、残りだけが鳴る");
    }

    printf("--- sound.cfg: output=off / volume ---\n");
    OSData::SD_usable = true;
    HostSd::files["/sys/sound.cfg"] = "# テスト\noutput = off\nvolume = 30\n";
    const int end_before = Out().end_count;
    SetupAt(now);
    check(GetVolume() == 30, "volumeを読む");
    check(GetOutput() == Output::Off, "outputを読む");
    check(IsConnected() && GetState() == State::Muted, "刺さっていても消音");
    check(!IsAvailable(), "IsAvailableはfalse");
    check(!Out().running && Out().end_count == end_before + 1, "I2Sは止める");
    SetOutput(Output::Auto);
    check(GetState() == State::Active && Out().running, "autoへ戻すと鳴らせる");

    printf("--- I2Sを開始できなかったとき ---\n");
    SetOutput(Output::Off);
    Out().fail_begin = true;
    const int begins = Out().begin_count;
    SetOutput(Output::Auto);
    for(int i = 0; i < 5; i++){ now += kDetectIntervalMs; UpdateAt(now); }
    check(GetState() == State::Disconnected && !IsAvailable(), "鳴らせない扱い");
    check(Out().begin_count == begins + 1, "失敗したら毎フレーム試し直さない(1回だけ)");
    Out().fail_begin = false;
    plugged = false;
    for(int i = 0; i < kDetectStableCount; i++){ now += kDetectIntervalMs; UpdateAt(now); }
    plugged = true;
    for(int i = 0; i < kDetectStableCount; i++){ now += kDetectIntervalMs; UpdateAt(now); }
    check(GetState() == State::Active, "刺し直せば試し直す");

    printf("\n%s (%d件の失敗)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
