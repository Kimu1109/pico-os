// src/functions/UTF8_Functions.hpp
//
// UTF-8エンコード/デコード関連のユーティリティ関数群。
// 文字数カウント、オフセット計算、部分切り出し、末尾削除・置換等の文字列操作機能は
// util/FixedString.hpp に実装されています。

#pragma once
#include <Arduino.h>
#include "util/FixedString.hpp"

namespace UTF8_Functions {

    // ============ UTF-8 エンコード / デコード ============

    inline uint32_t Utf8Decode(const uint8_t* s, int& len) {
        uint8_t c = s[0];
        if (c < 0x80) { len = 1; return c; }
        else if ((c & 0xE0) == 0xC0) { len = 2; return ((c & 0x1F) << 6) | (s[1] & 0x3F); }
        else if ((c & 0xF0) == 0xE0) { len = 3; return ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F); }
        else if ((c & 0xF8) == 0xF0) { len = 4; return ((c & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F); }
        len = 1; return c; // 不正なバイト列のフォールバック
    }

    // codepoint を UTF-8 の3バイト(日本語の範囲は基本ここ)としてバッファに書き込む
    // out には最低4バイト分の領域(3バイト+終端\0)を用意すること
    inline int Utf8Encode3(uint32_t cp, uint8_t* out) {
        out[0] = 0xE0 | ((cp >> 12) & 0x0F);
        out[1] = 0x80 | ((cp >> 6) & 0x3F);
        out[2] = 0x80 | (cp & 0x3F);
        return 3;
    }

    // ひらがな→カタカナ変換。result へ書き込む(破壊的、resultは事前にclearされる)。
    // 戻り値は「容量内に収まりきったか」(falseなら途中で切り詰められている)。
    template<size_t N, size_t M>
    inline bool HiraganaToKatakana(const FixedString<N>& input, FixedString<M>& result) {
        result.clear();

        const uint8_t* p = (const uint8_t*)input.c_str();
        int total = (int)input.length();
        int i = 0;
        bool ok = true;

        while (i < total) {
            int len;
            uint32_t cp = Utf8Decode(p + i, len);

            if (cp >= 0x3041 && cp <= 0x3096) {
                // ひらがな範囲 → カタカナへ(コードポイントを+0x60するとカタカナになる)
                uint8_t buf[3];
                int n = Utf8Encode3(cp + 0x60, buf);
                if (!result.append(reinterpret_cast<const char*>(buf), (size_t)n)) ok = false;
            } else {
                // それ以外はそのままコピー
                if (!result.append(reinterpret_cast<const char*>(p) + i, (size_t)len)) ok = false;
            }
            i += len;
        }
        return ok;
    }
}
