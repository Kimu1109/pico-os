#include "config/Hardware_Config.hpp"

#if !PICO_HW_MIC
// マイクを載せない版(-DPICO_HW_MIC=0。config/Hardware_Config.hpp)の MicFunctions。
// 本物(Mic_Functions.cpp)と同じ口で、I2S・DMAのバッファ・録音の列・WAVの書き込みを持たない。
// メーターを頼まれると「開始できなかった」(Input::Failed)と答え、録音は RecError::NoDevice で断る。
// 設定(mic.cfg)は読まず、既定値を返すだけ
#include "hal/Mic_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "sound/Mic_Dsp.hpp"

#include <cstdio>

using namespace MicFunctions;

namespace {
    bool monitor = false;
}

void MicFunctions::Setup(){ LOG_SYS_MSG("Mic: このファームはマイクなしでビルドされています"); }
void MicFunctions::Update(){}

void MicFunctions::SetMonitor(bool on){ monitor = on; }
Input MicFunctions::InputState(){ return monitor ? Input::Failed : Input::Off; }
int32_t MicFunctions::TakePeak(){ return 0; }
bool MicFunctions::HasSignal(){ return false; }

RecError MicFunctions::StartRecording(const char*){ return RecError::NoDevice; }
void MicFunctions::StopRecording(){}
bool MicFunctions::IsRecording(){ return false; }
bool MicFunctions::IsFinishing(){ return false; }
const char* MicFunctions::RecordingPath(){ return ""; }
uint32_t MicFunctions::RecordedMs(){ return 0; }
uint32_t MicFunctions::DroppedSamples(){ return 0; }
uint32_t MicFunctions::Overruns(){ return 0; }
StopReason MicFunctions::LastStopReason(){ return StopReason::None; }

bool MicFunctions::NextRecordingPath(char* out, size_t cap){
    if(cap) out[0] = '\0';
    return false;
}

uint32_t MicFunctions::SampleRate(){ return kDefaultRate; }
bool MicFunctions::SetSampleRate(uint32_t rate, bool){ return MicDsp::ValidRate(rate); }
void MicFunctions::RestoreSampleRate(uint32_t){}
int MicFunctions::GainDb(){ return kDefaultGainDb; }
void MicFunctions::SetGainDb(int, bool){}
bool MicFunctions::RightChannel(){ return false; }

const char* MicFunctions::RecErrorToStr(RecError e){
    return e == RecError::None ? "" : "このファームはマイクなしでビルドされています";
}
const char* MicFunctions::StopReasonToStr(StopReason){ return ""; }

bool MicFunctions::LoopCore1(){ return false; }
bool MicFunctions::Core1Busy(){ return false; }
#endif
