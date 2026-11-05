// 引き当てと言語コードだけ(SDもログも使わない)。TR() を使うどのファイル・ホストテストからもリンクされる
#include "system/I18n_Functions.hpp"
#include "config/Lang_Config.hpp"

#if PICO_LANG_EN
namespace {
    // 表から hash の訳を探す。無ければ nullptr
    const char* find(uint32_t hash){
        const I18nFunctions::Entry* table = I18nFunctions::kTableEn;
        //hash の昇順(生成時に重複が無いことを確かめてある)
        size_t lo = 0, hi = I18nFunctions::kTableEnCount;
        while(lo < hi){
            const size_t mid = lo + (hi - lo) / 2;
            const uint32_t h = table[mid].hash;
            if(h == hash) return table[mid].text;
            if(h < hash) lo = mid + 1;
            else         hi = mid;
        }
        return nullptr;
    }
}

const char* I18nFunctions::LookupHash(uint32_t hash){
    const char* t = find(hash);
    return t ? t : "";
}

const char* I18nFunctions::Tr(const char* src){
    if(!src) return src;
    //Luaアプリの名前等、表に無いものは渡したまま
    const char* t = find(Hash(src));
    return t ? t : src;
}
#endif

const char* I18nFunctions::Code(Lang lang){
    switch(lang){
        case Lang::En: return "en";
        default:       return "ja";
    }
}
