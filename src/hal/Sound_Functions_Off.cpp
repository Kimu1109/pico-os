#include "config/Hardware_Config.hpp"

#if !PICO_HW_SOUND
// 音声出力を載せない版(-DPICO_HW_SOUND=0。config/Hardware_Config.hpp)の SoundFunctions。
// 本物(Sound_Functions.cpp)と同じ口で、常に「鳴らす先がつながっていない」と答える。
// I2S・音源(ChipSynth/GbApu/MusicPlayer)・コマンドの列・WAVの読み取りを持たないので、RAMもフラッシュも使わない。
// 設定(sound.cfg)は読まない。鳴らす要求は受け取って捨て、曲/WAVは理由付きで断る(画面がそれを見せる)
#include "hal/Sound_Functions.hpp"
#include "sound/Mml_Compiler.hpp"
#include "system/Log_Functions.hpp"

#include <cstring>

using namespace SoundFunctions;

namespace {
    constexpr const char* kNoSound = "このファームは音声出力なしでビルドされています";

    bool Refuse(MmlResult* result){
        if(result){
            result->ok = false;
            result->message.assign(kNoSound);
        }
        return false;
    }
}

void SoundFunctions::Setup(){ LOG_SYS_MSG("Sound: %s", kNoSound); }
void SoundFunctions::Update(){}

State SoundFunctions::GetState(){ return State::Disconnected; }
bool SoundFunctions::IsConnected(){ return false; }
bool SoundFunctions::IsAvailable(){ return false; }

Output SoundFunctions::GetOutput(){ return Output::Off; }
void SoundFunctions::SetOutput(Output){}

Route SoundFunctions::GetRoute(){ return Route::Speaker; }
bool SoundFunctions::IsSpeakerConnected(){ return false; }
bool SoundFunctions::IsHeadphoneInserted(){ return false; }
RouteMode SoundFunctions::GetRouteMode(){ return RouteMode::Auto; }
void SoundFunctions::SetRouteMode(RouteMode){}
const char* SoundFunctions::RouteModeName(RouteMode m){
    switch(m){
        case RouteMode::Speaker:   return "speaker";
        case RouteMode::Headphone: return "headphone";
        default:                   return "auto";
    }
}
bool SoundFunctions::ParseRouteMode(const char* s, RouteMode& out){
    if(!s) return false;
    if(strcmp(s, "auto") == 0)              out = RouteMode::Auto;
    else if(strcmp(s, "speaker") == 0)      out = RouteMode::Speaker;
    else if(strcmp(s, "headphone") == 0)    out = RouteMode::Headphone;
    else return false;
    return true;
}

uint8_t SoundFunctions::GetVolume(){ return 0; }
void SoundFunctions::SetVolume(int){}
uint8_t SoundFunctions::GetVolumeFor(Route){ return 0; }
void SoundFunctions::SetVolumeFor(Route, int){}
const char* SoundFunctions::VolumeConfigKey(Route r){ return r == Route::Headphone ? "hp-volume" : "volume"; }

uint32_t SoundFunctions::SampleRate(){ return kDefaultSampleRate; }
uint32_t SoundFunctions::ConfiguredSampleRate(){ return kDefaultSampleRate; }
bool SoundFunctions::SetSampleRate(uint32_t rate){ return rate == kSampleRateLow || rate == kSampleRateHigh; }
bool SoundFunctions::RequestSampleRate(uint32_t rate){ return rate == 0 || rate == kSampleRateLow || rate == kSampleRateHigh; }
uint32_t SoundFunctions::RequestedSampleRate(){ return 0; }

bool SoundFunctions::GetBatteryCap(){ return false; }
void SoundFunctions::SetBatteryCap(bool){}
bool SoundFunctions::IsBatteryVolumeCapActive(){ return false; }

bool SoundFunctions::Play(uint8_t, const ChipSynth::Note&){ return false; }
void SoundFunctions::Stop(uint8_t){}
void SoundFunctions::StopAll(){}
void SoundFunctions::Beep(uint16_t, uint16_t){}

bool SoundFunctions::IsPlaying(){ return false; }
ChipSynth::ChannelMask SoundFunctions::ActiveChannels(){ return 0; }
uint32_t SoundFunctions::DroppedCommands(){ return 0; }

bool SoundFunctions::MusicPlayFile(const char*, MmlResult* result){ return Refuse(result); }
bool SoundFunctions::MusicPlayText(const char*, size_t, MmlResult* result){ return Refuse(result); }
void SoundFunctions::MusicStop(){}
bool SoundFunctions::MusicPlaying(){ return false; }
const char* SoundFunctions::MusicTitle(){ return ""; }
bool SoundFunctions::MusicPause(bool){ return false; }
bool SoundFunctions::MusicPaused(){ return false; }
uint32_t SoundFunctions::MusicElapsedMs(){ return 0; }
uint32_t SoundFunctions::MusicTotalMs(bool* loops){
    if(loops) *loops = false;
    return 0;
}
uint32_t SoundFunctions::PlaybackSerial(){ return 0; }

// GBエミュは音の窓口が無ければ(nullptr)鳴らさずに動く(GbEmu::setAudioSink())
GbAudioSink* SoundFunctions::GbAudio(){ return nullptr; }
uint32_t SoundFunctions::GbDroppedWrites(){ return 0; }

bool SoundFunctions::WavPlay(const char*, bool, uint8_t, const char** error, WavInfo*){
    if(error) *error = kNoSound;
    return false;
}
void SoundFunctions::WavStop(){}
bool SoundFunctions::WavPlaying(){ return false; }
const char* SoundFunctions::WavTitle(){ return ""; }
bool SoundFunctions::WavPause(bool){ return false; }
bool SoundFunctions::WavPaused(){ return false; }
uint32_t SoundFunctions::WavPositionMs(){ return 0; }
uint32_t SoundFunctions::WavDurationMs(){ return 0; }
bool SoundFunctions::WavSeekMs(uint32_t){ return false; }
uint32_t SoundFunctions::WavUnderruns(){ return 0; }

void SoundFunctions::SetPowerSave(bool){}
bool SoundFunctions::IsPowerSave(){ return false; }

void SoundFunctions::SetupCore1(){}
bool SoundFunctions::LoopCore1(){ return false; }
unsigned long SoundFunctions::IdleDelayMs(){ return kPowerSaveIdleDelayMs; }
bool SoundFunctions::Core1OutputRunning(){ return false; }

void SoundFunctions::SetupAt(unsigned long){ Setup(); }
void SoundFunctions::UpdateAt(unsigned long){}
bool SoundFunctions::Core1StepAt(unsigned long){ return false; }
#endif
