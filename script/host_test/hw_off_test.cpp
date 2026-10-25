// ビルド時にハードウェア機能を外した版(config/Hardware_Config.hpp の PICO_HW_*=0)の検証。
//
// このテストだけ -DPICO_HW_SOUND=0 -DPICO_HW_MIC=0 -DPICO_HW_BATTERY=0 で作り(CMakeLists.txt)、
// 空の実装(hal/Sound_Functions_Off.cpp・hal/Mic_Functions_Off.cpp・hal/Battery_Functions.cpp の #else)を
// テスト自身に入れてリンクする。本物の実装は層のライブラリにあっても、名前が全部こちらで足りるので引っ張られない。
// 呼び出し側が分岐しなくてよいように「つながっていない」と答え、曲・WAV・録音は理由付きで断ることを見る。
#include "config/Hardware_Config.hpp"
#include "hal/Sound_Functions.hpp"
#include "hal/Mic_Functions.hpp"
#include "hal/Battery_Functions.hpp"
#include "sound/Mml_Compiler.hpp"
#include "system/Log_Functions.hpp"

#include <cstdio>
#include <cstring>

void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}

static_assert(!HardwareConfig::kSound && !HardwareConfig::kMic && !HardwareConfig::kBattery,
              "このテストは全部外した版で作る(CMakeLists.txt の target_compile_definitions)");
static_assert(!PICO_HW_CORE1, "音声もマイクも無ければ2コア目は要らない");

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

static void TestSound(){
    SoundFunctions::Setup();
    SoundFunctions::Update();
    check(SoundFunctions::GetState() == SoundFunctions::State::Disconnected, "音: 常に未接続");
    check(!SoundFunctions::IsConnected() && !SoundFunctions::IsAvailable(), "音: つながっていない・鳴らない");

    const ChipSynth::Note n;
    check(!SoundFunctions::Play(0, n), "音: 効果音は受け取って捨てる(積まない)");
    SoundFunctions::Beep(880, 100);
    check(!SoundFunctions::IsPlaying() && SoundFunctions::ActiveChannels() == 0, "音: 何も鳴っていない");

    MmlResult r;
    const char mml[] = "cde";
    check(!SoundFunctions::MusicPlayText(mml, sizeof(mml) - 1, &r), "曲: 断る");
    check(!r.ok && !r.message.empty(), "曲: 理由が入る");
    check(!SoundFunctions::MusicPlayFile("/music/a.mml", nullptr), "曲: 結果を受けなくても落ちない");
    check(!SoundFunctions::MusicPlaying() && SoundFunctions::MusicTitle()[0] == '\0', "曲: 鳴っていない");
    bool loops = true;
    check(SoundFunctions::MusicTotalMs(&loops) == 0 && !loops, "曲: 長さ0・ループ無し");

    const char* err = nullptr;
    check(!SoundFunctions::WavPlay("/a.wav", false, 100, &err) && err && err[0] != '\0', "WAV: 理由付きで断る");
    check(!SoundFunctions::WavPlaying() && !SoundFunctions::WavSeekMs(1000), "WAV: 鳴っていない・飛べない");

    check(SoundFunctions::GbAudio() == nullptr, "GB: 音の窓口は無し(エミュは音なしで動く)");

    check(SoundFunctions::SetSampleRate(SoundFunctions::kSampleRateHigh), "周波数: 正しい値は受け付ける");
    check(!SoundFunctions::SetSampleRate(12345), "周波数: おかしい値は断る");
    check(SoundFunctions::RequestSampleRate(0), "周波数: 要求の取り下げ");

    SoundFunctions::RouteMode m = SoundFunctions::RouteMode::Auto;
    check(SoundFunctions::ParseRouteMode("headphone", m) && m == SoundFunctions::RouteMode::Headphone, "出力先: 名前は読める");
    check(strcmp(SoundFunctions::RouteModeName(SoundFunctions::RouteMode::Speaker), "speaker") == 0, "出力先: 名前を書ける");

    SoundFunctions::SetupCore1();
    check(!SoundFunctions::LoopCore1(), "2コア目: 仕事は無い");
}

static void TestMic(){
    MicFunctions::Setup();
    MicFunctions::Update();
    check(MicFunctions::InputState() == MicFunctions::Input::Off, "マイク: 使っていなければOff");
    MicFunctions::SetMonitor(true);
    check(MicFunctions::InputState() == MicFunctions::Input::Failed, "マイク: メーターは開始できなかった扱い");
    check(MicFunctions::TakePeak() == 0 && !MicFunctions::HasSignal(), "マイク: 信号は無い");
    MicFunctions::SetMonitor(false);
    check(MicFunctions::InputState() == MicFunctions::Input::Off, "マイク: メーターを止めるとOff");

    check(MicFunctions::StartRecording() == MicFunctions::RecError::NoDevice, "録音: 載せていないと断る");
    check(MicFunctions::RecErrorToStr(MicFunctions::RecError::NoDevice)[0] != '\0', "録音: 理由の文字列");
    check(!MicFunctions::IsRecording() && !MicFunctions::IsFinishing(), "録音: していない");
    char path[64] = "x";
    check(!MicFunctions::NextRecordingPath(path, sizeof(path)) && path[0] == '\0', "録音: 名前は作らない");
    check(MicFunctions::SampleRate() == MicFunctions::kDefaultRate && MicFunctions::GainDb() == MicFunctions::kDefaultGainDb,
          "マイク: 設定は既定値");
    check(!MicFunctions::LoopCore1() && !MicFunctions::Core1Busy(), "2コア目: 仕事は無い");
}

static void TestBattery(){
    BatteryFunctions::Setup();
    BatteryFunctions::Update();
    check(!BatteryFunctions::HasSample(), "電池: 読めていない扱い(残量を出さない)");
    check(BatteryFunctions::IsExternallyPowered(), "電池: 常にUSB給電");
}

int main(){
    TestSound();
    TestMic();
    TestBattery();
    if(failures){
        printf("%d 件失敗\n", failures);
        return 1;
    }
    printf("全部通りました\n");
    return 0;
}
