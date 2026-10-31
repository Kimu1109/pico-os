#include "system/I18n_Functions.hpp"
#include "system/Config_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "storage/SD_Path.hpp"

#include <cstring>

namespace {
    I18nFunctions::Lang current = I18nFunctions::Lang::Ja;

    constexpr const char* kUserCfgKey = "language";

    const I18nFunctions::Entry* tableOf(I18nFunctions::Lang lang, size_t& count){
        switch(lang){
            case I18nFunctions::Lang::En:
                count = I18nFunctions::kTableEnCount;
                return I18nFunctions::kTableEn;
            default:
                count = 0;
                return nullptr;
        }
    }
}

const char* I18nFunctions::Lookup(uint32_t hash, const char* src){
    if(current == Lang::Ja) return src;
    size_t count = 0;
    const Entry* table = tableOf(current, count);
    //hash の昇順(生成時に重複が無いことを確かめてある)
    size_t lo = 0, hi = count;
    while(lo < hi){
        const size_t mid = lo + (hi - lo) / 2;
        const uint32_t h = table[mid].hash;
        if(h == hash) return table[mid].text;
        if(h < hash) lo = mid + 1;
        else         hi = mid;
    }
    return src;
}

const char* I18nFunctions::Tr(const char* src){
    if(!src || current == Lang::Ja) return src;
    return Lookup(Hash(src), src);
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
    current = lang;
    LOG_SYS_OK("I18n Setup has succeeded! (%s)", Code(current));
}

I18nFunctions::Lang I18nFunctions::Current(){
    return current;
}

void I18nFunctions::SetLanguage(Lang lang, bool save){
    if(lang >= Lang::Count) return;
    current = lang;
    if(save) PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_USER_CFG, kUserCfgKey, Code(lang));
}

const char* I18nFunctions::Code(Lang lang){
    switch(lang){
        case Lang::En: return "en";
        default:       return "ja";
    }
}

bool I18nFunctions::FromCode(const char* code, Lang& out){
    if(!code) return false;
    //"en-US" "ja_JP" のような地域つきの値は先頭の2文字で見る
    const size_t n = strlen(code);
    if(n < 2 || (n > 2 && code[2] != '-' && code[2] != '_')) return false;
    char c0 = code[0], c1 = code[1];
    if(c0 >= 'A' && c0 <= 'Z') c0 = (char)(c0 - 'A' + 'a');
    if(c1 >= 'A' && c1 <= 'Z') c1 = (char)(c1 - 'A' + 'a');
    if(c0 == 'j' && c1 == 'a'){ out = Lang::Ja; return true; }
    if(c0 == 'e' && c1 == 'n'){ out = Lang::En; return true; }
    return false;
}

const char* I18nFunctions::NativeName(Lang lang){
    switch(lang){
        case Lang::En: return "English";
        default:       return "日本語";
    }
}
