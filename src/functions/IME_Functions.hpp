#pragma once

#include "ime/IME_Dict.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/UTF8_Functions.hpp"

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
    inline void okuri_attach(const char* okuri_kana){
        for(int i = 0; i < candidatesCount; i++){
            strcat(candidates[i], okuri_kana);
            candidates_width[i] = OSData::frame->textWidth(candidates[i], &lgfxJapanGothicP_16);
        }
    }

    // ---- buildOkuriKey() サンプル実装 ----
    // 前述の通り、これはサンプルです。Lua側の既存テーブルで置き換え推奨。

    struct KanaMarker {
        const char* kana; // UTF-8
        char marker;
    };

    static inline const KanaMarker kKanaMarkerTable[] = {
        {"か", 'k'}, {"き", 'k'}, {"く", 'k'}, {"け", 'k'}, {"こ", 'k'},
        {"が", 'g'}, {"ぎ", 'g'}, {"ぐ", 'g'}, {"げ", 'g'}, {"ご", 'g'},
        {"さ", 's'}, {"し", 's'}, {"す", 's'}, {"せ", 's'}, {"そ", 's'},
        {"ざ", 'z'}, {"じ", 'z'}, {"ず", 'z'}, {"ぜ", 'z'}, {"ぞ", 'z'},
        {"た", 't'}, {"ち", 't'}, {"つ", 't'}, {"て", 't'}, {"と", 't'},
        {"だ", 'd'}, {"ぢ", 'd'}, {"づ", 'd'}, {"で", 'd'}, {"ど", 'd'},
        {"な", 'n'}, {"に", 'n'}, {"ぬ", 'n'}, {"ね", 'n'}, {"の", 'n'},
        {"は", 'h'}, {"ひ", 'h'}, {"ふ", 'h'}, {"へ", 'h'}, {"ほ", 'h'},
        {"ば", 'b'}, {"び", 'b'}, {"ぶ", 'b'}, {"べ", 'b'}, {"ぼ", 'b'},
        {"ぱ", 'p'}, {"ぴ", 'p'}, {"ぷ", 'p'}, {"ぺ", 'p'}, {"ぽ", 'p'},
        {"ま", 'm'}, {"み", 'm'}, {"む", 'm'}, {"め", 'm'}, {"も", 'm'},
        {"や", 'y'}, {"ゆ", 'y'}, {"よ", 'y'},
        {"ら", 'r'}, {"り", 'r'}, {"る", 'r'}, {"れ", 'r'}, {"ろ", 'r'},
        // う音便(五段動詞の「う」語尾)系の活用かなは全てwマーカーに集約
        {"わ", 'w'}, {"い", 'w'}, {"う", 'w'}, {"え", 'w'}, {"お", 'w'},
    };
    static inline const int kKanaMarkerCount = sizeof(kKanaMarkerTable) / sizeof(kKanaMarkerTable[0]);

    inline String buildOkuriKey(const String input, const char* okuriKanaUtf8) {
        const String stem = UTF8_Functions::removeLastChar(input);
        
        char marker = 0;
        for (int i = 0; i < kKanaMarkerCount; i++) {
            if (strcmp(kKanaMarkerTable[i].kana, okuriKanaUtf8) == 0) {
                marker = kKanaMarkerTable[i].marker;
                break;
            }
        }
        if (marker == 0) {
            return input;
        }

        return stem + marker;
    }
}