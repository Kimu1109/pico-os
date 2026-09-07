// src/util/Utf8Byte.hpp
//
// UTF-8の「リードバイトから文字のバイト数を判定する」という
// 最下層のロジックだけを切り出した、依存関係を持たないヘッダー。
//
// FixedString.hpp と UTF8_Functions.hpp の両方がこれを使う。
// (FixedStringがUTF8_Functionsに依存し、UTF8_FunctionsがFixedStringに
//  依存する、という循環を避けるための下敷き)

#pragma once
#include <stdint.h>

// UTF-8のリードバイトから、その文字が何バイトで構成されるかを返す
inline int Utf8CharBytesFromLeadByte(uint8_t b) {
    if ((b & 0x80) == 0x00) return 1;       // 0xxxxxxx: ASCII
    else if ((b & 0xE0) == 0xC0) return 2;  // 110xxxxx
    else if ((b & 0xF0) == 0xE0) return 3;  // 1110xxxx (日本語の大半)
    else if ((b & 0xF8) == 0xF0) return 4;  // 11110xxx (絵文字など)
    return 1; // 不正なバイト列への保険
}
