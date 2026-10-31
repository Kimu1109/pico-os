// 引き当てと言語コードだけ(SDもログも使わない)。TR() を使うどのファイルからもリンクされるので、
// 設定ファイルを読み書きする Setup()/SetLanguage() は I18n_Setup.cpp に分けてある
#include "system/I18n_Functions.hpp"

#include <cstring>

namespace {
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
    if(current_lang == Lang::Ja) return src;
    size_t count = 0;
    const Entry* table = tableOf(current_lang, count);
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
    if(!src || current_lang == Lang::Ja) return src;
    return Lookup(Hash(src), src);
}

I18nFunctions::Lang I18nFunctions::Current(){
    return current_lang;
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
