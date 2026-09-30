#include "functions/Power_Functions.hpp"
#include "functions/Display_Functions.hpp"
#include "functions/Network_Functions.hpp"
#include "functions/Sound_Functions.hpp"
#include "functions/Pad_Functions.hpp"
#include "functions/Config_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "storage/SD_Path.hpp"
#include "OS_Data.hpp"

#include <Arduino.h>
#include <cstring>

namespace {
    unsigned long sleep_timeout_ms = PowerFunctions::kDefaultSleepTimeoutMs;
    bool          sleeping         = false;
    unsigned long last_activity_ms = 0;
    // スリープから起こしたタッチを、指を離すまで画面へ渡さない
    bool          swallow_touch    = false;

    void LoadConfig(){
        if(!OSData::SD_usable) return;
        //無いのが普通(既定値で動く)。ParseFile()は開けないとFAILを出すので先に確かめる
        if(!OSData::SD.exists(PICO_Path::FILE::CFG::SYS_DISPLAY_CFG)) return;

        PICO_Config::ParseFile(PICO_Path::FILE::CFG::SYS_DISPLAY_CFG,
            [](const char* key, const char* value){
                if(strcmp(key, "sleep-timeout") == 0){
                    int v = 0;
                    if(PICO_Config::ConfigValue::AsInt(value, v) && v >= 0 && v <= 86400){
                        sleep_timeout_ms = (unsigned long)v * 1000UL;
                    }else{
                        LOG_SYS_WARN("display.cfg: sleep-timeout は0〜86400(秒)の整数です: %s", value);
                    }
                }
            }
        );
    }

    // 操作が無くても止めてはいけない仕事があるか
    bool Busy(){
        return SoundFunctions::IsPlaying()
            || SoundFunctions::MusicPlaying()
            || NetworkFunctions::currentStatus == NetworkFunctions::NetStatus::TRYING_CONNECT;
    }

    void Enter(){
        sleeping = true;
        DisplayFunctions::SetSleeping(true);
        NetworkFunctions::SetLowPower(true);
        SoundFunctions::SetPowerSave(true);
        LOG_SYS_MSG("Power: スリープに入りました");
    }

    void Leave(){
        sleeping = false;
        //パネルを起こす(120ms待つ)前に、他を戻しておく
        SoundFunctions::SetPowerSave(false);
        NetworkFunctions::SetLowPower(false);
        DisplayFunctions::SetSleeping(false);
        LOG_SYS_MSG("Power: スリープから復帰しました");
    }
}

void PowerFunctions::Setup(){ SetupAt(millis()); }
void PowerFunctions::Update(){ UpdateAt(millis()); }

void PowerFunctions::SetupAt(unsigned long now_ms){
    sleep_timeout_ms = kDefaultSleepTimeoutMs;
    sleeping         = false;
    swallow_touch    = false;
    detail::keep_awake = false;
    LoadConfig();
    last_activity_ms = now_ms;
    LOG_SYS_OK("Power Setup has succeeded!");
}

void PowerFunctions::UpdateAt(unsigned long now_ms){
    const bool touched = OSData::isTouched;
    const bool active  = touched || PadFunctions::IsDown(PadFunctions::kAllButtons);
    //直前のフレームで各画面が呼んだ印(この後の画面の更新で、次のフレーム分が立つ)
    const bool kept = detail::keep_awake;
    detail::keep_awake = false;

    if(active){
        last_activity_ms = now_ms;
        if(sleeping){
            Leave();
            if(touched) swallow_touch = true;
        }
    }else if(sleeping && kept){
        //操作が無くても動く画面へ切り替わった/その画面が動き出した。画面を戻す
        Leave();
    }

    //起こしたタッチは指を離すまで画面へ渡さない。Touch_Functionsは毎フレーム
    //「isTouchedが偽なら新しいタッチ」と見なすので、ここで下ろし続けても離した瞬間に自然に終わる
    if(swallow_touch){
        if(OSData::isTouched){
            OSData::isTouched    = false;
            OSData::isTouchStart = false;
            OSData::isTouchMove  = false;
            OSData::isTouchEnd   = false;
        }else{
            swallow_touch = false;
        }
    }

    if(!sleeping && sleep_timeout_ms > 0 && !active && !kept
        && now_ms - last_activity_ms >= sleep_timeout_ms && !Busy()){
        Enter();
    }
}

void PowerFunctions::IdleWait(){
    if(sleeping) delay(kSleepLoopDelayMs);
}

bool PowerFunctions::IsSleeping(){ return sleeping; }

unsigned long PowerFunctions::GetSleepTimeoutMs(){ return sleep_timeout_ms; }
void PowerFunctions::SetSleepTimeoutMs(unsigned long ms){ sleep_timeout_ms = ms; }
