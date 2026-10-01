#include "functions/Display_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Config_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/Pad_Functions.hpp"
#include "functions/KeyInput_Functions.hpp"
#include "storage/SD_Path.hpp"
#include "OS_Data.hpp"

#include <Arduino.h>
#include <cstring>

namespace {
    uint8_t       normal_brightness = DisplayFunctions::kDefaultBrightness;
    bool          auto_dim_enabled  = true;
    bool          is_dimmed         = false;
    bool          is_sleeping       = false;
    unsigned long last_activity_ms  = 0;

    void ApplyEffectiveBrightness(){
        //スリープ中はバックライトを消したまま(起きるときにSetSleeping(false)が今の値へ戻す)
        if(is_sleeping) return;
        if(is_dimmed){
            const uint8_t dim = (DisplayFunctions::kDimBrightness < normal_brightness)
                ? DisplayFunctions::kDimBrightness : normal_brightness;
            PICO_GFX::SetBrightness(dim);
        }else{
            PICO_GFX::SetBrightness(normal_brightness);
        }
    }

    void LoadConfig(){
        if(!OSData::SD_usable) return;
        //無いのが普通(既定値で動く)。ParseFile()は開けないとFAILを出すので先に確かめる
        if(!OSData::SD.exists(PICO_Path::FILE::CFG::SYS_DISPLAY_CFG)) return;

        PICO_Config::ParseFile(PICO_Path::FILE::CFG::SYS_DISPLAY_CFG,
            [](const char* key, const char* value){
                if(strcmp(key, "brightness") == 0){
                    int v = 0;
                    if(PICO_Config::ConfigValue::AsInt(value, v) && v >= 0 && v <= 100){
                        normal_brightness = (uint8_t)v;
                    }else{
                        LOG_SYS_WARN("display.cfg: brightness は0〜100の整数です: %s", value);
                    }
                }else if(strcmp(key, "auto-dim") == 0){
                    bool enabled = true;
                    if(PICO_Config::ConfigValue::AsBool(value, enabled)){
                        auto_dim_enabled = enabled;
                    }else{
                        LOG_SYS_WARN("display.cfg: auto-dim は true/false です: %s", value);
                    }
                }
            }
        );
    }
}

void DisplayFunctions::Setup(){
    normal_brightness = kDefaultBrightness;
    auto_dim_enabled  = true;
    is_dimmed         = false;
    is_sleeping       = false;

    LoadConfig();
    if(normal_brightness < kMinBrightness) normal_brightness = kMinBrightness;

    last_activity_ms = millis();
    ApplyEffectiveBrightness();

    LOG_SYS_OK("Display Setup has succeeded!");
}

void DisplayFunctions::Update(){
    const bool active = OSData::isTouched || PadFunctions::IsDown(PadFunctions::kAllButtons)
                       || KeyInputFunctions::Pending() > 0; //このフレームの打鍵(配るのはこの後)
    const unsigned long now = millis();

    if(active){
        last_activity_ms = now;
        if(is_dimmed){
            is_dimmed = false;
            ApplyEffectiveBrightness();
        }
        return;
    }

    if(!auto_dim_enabled || is_dimmed) return;

    if(now - last_activity_ms >= kIdleTimeoutMs){
        is_dimmed = true;
        ApplyEffectiveBrightness();
    }
}

uint8_t DisplayFunctions::GetBrightness(){
    return normal_brightness;
}

void DisplayFunctions::SetBrightness(int percent){
    if(percent < (int)kMinBrightness) percent = (int)kMinBrightness;
    if(percent > 100) percent = 100;
    normal_brightness = (uint8_t)percent;
    if(!is_dimmed && !is_sleeping) PICO_GFX::SetBrightness(normal_brightness);
}

bool DisplayFunctions::GetAutoDimEnabled(){
    return auto_dim_enabled;
}

void DisplayFunctions::SetAutoDimEnabled(bool enabled){
    auto_dim_enabled = enabled;
    //切ったときに暗いままにならないよう、その場で通常の明るさへ戻す
    if(!enabled && is_dimmed){
        is_dimmed = false;
        ApplyEffectiveBrightness();
    }
}

bool DisplayFunctions::IsDimmed(){
    return is_dimmed;
}

void DisplayFunctions::SetSleeping(bool sleeping){
    if(sleeping == is_sleeping) return;
    is_sleeping = sleeping;
    if(!OSData::lcd) return;

    if(sleeping){
        //先にバックライトを消してからパネルを休ませる(lcd->sleep()も明るさ0を設定する)
        OSData::lcd->sleep();
    }else{
        OSData::lcd->wakeup();
        //ILI9341/ST7789はSLPOUTから次のコマンドまで120ms空ける必要がある
        //(LovyanGFXのPanel_LCD::setSleep()は待たない)。起きるときの1回だけなので待つ
        delay(120);
        ApplyEffectiveBrightness();
    }
}

bool DisplayFunctions::IsSleeping(){
    return is_sleeping;
}
