#include "functions/Background_Functions.hpp"

#include "functions/Config_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "storage/SD_Path.hpp"
#include "OS_Data.hpp"
#include "Arduino.h"

#include <cstring>

namespace {
    BackgroundService* services[BackgroundFunctions::kMaxServices] = {};
    int service_count = 0;
    uint32_t revision = 0;
}

bool BackgroundFunctions::ConfigEnabled(const char* key, bool def){
    if(!key || !OSData::SD_usable) return def;
    if(!OSData::SD.exists(PICO_Path::FILE::CFG::SYS_BACKGROUND_CFG)) return def;
    bool result = def;
    PICO_Config::ParseFile(PICO_Path::FILE::CFG::SYS_BACKGROUND_CFG, [&](const char* k, const char* v){
        if(strcmp(k, key) != 0) return;
        //"on"/"off" と true/false のどちらでも(後勝ち)
        if(strcmp(v, "on") == 0) result = true;
        else if(strcmp(v, "off") == 0) result = false;
        else {
            bool b;
            if(PICO_Config::ConfigValue::AsBool(v, b)) result = b;
        }
    });
    return result;
}

bool BackgroundFunctions::Add(BackgroundService* s, bool default_enabled){
    if(!s) return false;
    if(Find(s->key())) return false;
    if(service_count >= kMaxServices){
        LOG_SYS_WARN("Background: 仕事が多すぎるため %s を登録しません(上限%d)", s->key(), kMaxServices);
        return false;
    }
    if(s->toggleable()) s->setEnabled(ConfigEnabled(s->key(), default_enabled));
    services[service_count++] = s;
    revision++;
    return true;
}

void BackgroundFunctions::Remove(BackgroundService* s){
    for(int i = 0; i < service_count; i++){
        if(services[i] != s) continue;
        for(int j = i; j + 1 < service_count; j++) services[j] = services[j + 1];
        services[--service_count] = nullptr;
        revision++;
        return;
    }
}

int BackgroundFunctions::Count(){ return service_count; }

BackgroundService* BackgroundFunctions::At(int i){
    return (i >= 0 && i < service_count) ? services[i] : nullptr;
}

BackgroundService* BackgroundFunctions::Find(const char* key){
    if(!key) return nullptr;
    for(int i = 0; i < service_count; i++){
        if(strcmp(services[i]->key(), key) == 0) return services[i];
    }
    return nullptr;
}

void BackgroundFunctions::SetEnabled(BackgroundService* s, bool enabled){
    if(!s || !s->toggleable()) return;
    if(s->enabled() != enabled){
        s->setEnabled(enabled);
        revision++;
    }
    if(OSData::SD_usable){
        PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_BACKGROUND_CFG, s->key(), enabled ? "on" : "off");
    }
}

void BackgroundFunctions::Update(){ UpdateAt(millis()); }

void BackgroundFunctions::UpdateAt(uint32_t now_ms){
    if(suspend_query && suspend_query()) return;
    //update() の中で自分を外す仕事があっても飛ばさないよう、後ろから
    for(int i = service_count - 1; i >= 0; i--){
        if(i < service_count) services[i]->update(now_ms);
    }
}

bool BackgroundFunctions::AnyActive(){
    for(int i = 0; i < service_count; i++){
        if(services[i]->active()) return true;
    }
    return false;
}

IconID BackgroundFunctions::IndicatorIcon(){
    //今だけ動いている仕事(ミュージックの再生)はその絵、定期の仕事だけなら一般の「動いている」印
    bool any = false;
    for(int i = 0; i < service_count; i++){
        if(!services[i]->active()) continue;
        if(!services[i]->toggleable()) return services[i]->icon();
        any = true;
    }
    return any ? IconID::Refresh : IconID::IconCount;
}

uint32_t BackgroundFunctions::PreferredSampleRate(){
    uint32_t rate = 0;
    for(int i = 0; i < service_count; i++){
        if(!services[i]->holdsSampleRate()) continue;
        const uint32_t r = services[i]->preferredSampleRate();
        if(r > rate) rate = r;
    }
    return rate;
}

uint32_t BackgroundFunctions::Revision(){ return revision; }

void BackgroundFunctions::Test::Reset(){
    for(int i = 0; i < kMaxServices; i++) services[i] = nullptr;
    service_count = 0;
    revision++;
}
