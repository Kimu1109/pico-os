#pragma once

#include "hal/TouchCalib.hpp"
#include "consts.hpp"
#include "OS_Data.hpp"
#include "storage/SD_Path.hpp"
#include "system/Config_Functions.hpp"
#include "system/Log_Functions.hpp"

#include <cstring>

// タッチ位置の補正の窓口。補正値は /sys/touch.cfg に a〜f の6つの小数で持つ
// (無ければ補正なし=旧来の式)。求めかたは hal/TouchCalib.hpp、操作の画面は TouchCalibScene。
namespace TouchCalibFunctions {
    inline void Reset(){
        TouchCalib::current = TouchCalib::Default(SCREEN_WIDTH, SCREEN_HEIGHT);
        TouchCalib::has_current = true;
    }

    // 変換が使える範囲か(壊れたファイルで画面の外ばかり指さないように)。
    // 既定の式の向きと大きさが2倍以上違うものは断る
    inline bool Sane(const TouchCalib::Affine& t){
        const TouchCalib::Affine d = TouchCalib::Default(SCREEN_WIDTH, SCREEN_HEIGHT);
        auto ok = [](float v, float ref, float tol){ return std::fabs(v - ref) <= tol; };
        // 2×2部分の各成分が既定の「主成分」の半分を超えてずれていない
        const float ta = std::fabs(d.a), te = std::fabs(d.e);
        if(!ok(t.a, d.a, ta * 0.5f)) return false;
        if(!ok(t.e, d.e, te * 0.5f)) return false;
        if(std::fabs(t.b) > ta) return false;
        if(std::fabs(t.d) > te) return false;
        return std::isfinite(t.c) && std::isfinite(t.f);
    }

    inline void Setup(){
        Reset();
        if(!OSData::SD_usable) return;
        if(!OSData::SD.exists(PICO_Path::FILE::CFG::SYS_TOUCH_CFG)) return;

        TouchCalib::Affine t = TouchCalib::current;
        int got = 0;
        PICO_Config::ParseFile(PICO_Path::FILE::CFG::SYS_TOUCH_CFG,
            [&](const char* key, const char* value){
                float v;
                if(!PICO_Config::ConfigValue::AsFloat(value, v)) return;
                float* dst = nullptr;
                if(strcmp(key, "a") == 0) dst = &t.a;
                else if(strcmp(key, "b") == 0) dst = &t.b;
                else if(strcmp(key, "c") == 0) dst = &t.c;
                else if(strcmp(key, "d") == 0) dst = &t.d;
                else if(strcmp(key, "e") == 0) dst = &t.e;
                else if(strcmp(key, "f") == 0) dst = &t.f;
                if(dst){ *dst = v; got++; }
            });
        if(got == 6 && Sane(t)){
            TouchCalib::current = t;
            LOG_SYS_OK("タッチ補正を読み込みました");
        }else{
            LOG_SYS_WARN("touch.cfg が不完全か範囲外です。補正なしで動きます");
        }
    }

    // /sys/touch.cfg へ6つの値を書く(1キーずつ差し替え)
    inline bool Save(const TouchCalib::Affine& t){
        if(!OSData::SD_usable) return false;
        const float v[6] = { t.a, t.b, t.c, t.d, t.e, t.f };
        const char* keys[6] = { "a", "b", "c", "d", "e", "f" };
        for(int i = 0; i < 6; i++){
            char buf[32];
            if(!PICO_Config::ConfigValue::FromFloat(v[i], buf, sizeof(buf), 6)) return false;
            if(!PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_TOUCH_CFG, keys[i], buf)) return false;
        }
        return true;
    }

    // 補正を外して旧来の式へ戻し、ファイルも消す
    inline void Clear(){
        Reset();
        if(OSData::SD_usable && OSData::SD.exists(PICO_Path::FILE::CFG::SYS_TOUCH_CFG))
            OSData::SD.remove(PICO_Path::FILE::CFG::SYS_TOUCH_CFG);
    }
}
