#pragma once

#include "ime/IME_Dict.hpp"
#include "functions/Log_Functions.hpp"

namespace IME_Functions {
    ImeDictionary ime;
    char candidates[IME_MAX_CANDIDATES][IME_MAX_CAND_BYTES];
    int candidates_width[IME_MAX_CANDIDATES];
    int candidatesCount = 0;

    inline void setup(){
        ime.begin(OSData::SD, "dict/skk_body.tsv", "dict/skk_index.tsv");
    }
    inline int ime_lookup(const char* key) {
        int n = ime.lookup(key, candidates, IME_MAX_CANDIDATES);
        candidatesCount = n;

        for(int i = 0; i < n; i++){
            candidates_width[i] = OSData::frame->textWidth(candidates[i], &lgfxJapanGothicP_16);
        }

        return n;
    }
}