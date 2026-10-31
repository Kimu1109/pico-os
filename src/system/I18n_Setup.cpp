// 表示言語の読み込みと保存(/sys/user.cfg の language)。引き当ては I18n_Functions.cpp
#include "system/I18n_Functions.hpp"
#include "system/Config_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "storage/SD_Path.hpp"

#include <cstring>

namespace {
    constexpr const char* kUserCfgKey = "language";
}

void I18nFunctions::Setup(){
    Lang lang = Lang::Ja;
    if(override_code){
        if(!FromCode(override_code, lang)){
            LOG_SYS_WARN("I18n: 知らない言語です (%s)。日本語にします", override_code);
        }
    }else{
        bool found = false;
        PICO_Config::ParseFile(PICO_Path::FILE::CFG::SYS_USER_CFG,
            [&](const char* key, const char* value){
                if(strcmp(key, kUserCfgKey) != 0) return;
                found = FromCode(value, lang);
                if(!found){
                    LOG_SYS_WARN("I18n: user.cfg の language が読めません (%s)", value);
                }
            }
        );
        //知らない言語(ブラウザが fr 等)は日本語ではなく英語で見せる
        if(!found && default_code && !FromCode(default_code, lang)) lang = Lang::En;
    }
    current_lang = lang;
    LOG_SYS_OK("I18n Setup has succeeded! (%s)", Code(current_lang));
}

void I18nFunctions::SetLanguage(Lang lang, bool save){
    if(lang >= Lang::Count) return;
    current_lang = lang;
    if(save) PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_USER_CFG, kUserCfgKey, Code(lang));
}
