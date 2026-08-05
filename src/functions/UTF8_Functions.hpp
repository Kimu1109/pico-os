#pragma once

#include <Arduino.h>

namespace UTF8_Functions {
    // UTF-8文字列から最後の1文字を安全に切り出す関数
    String getLastChar(String str) {
        int len = str.length();
        if (len == 0) return "";

        int lastCharBytes = 1;
        
        // 末尾から手前に向かって、UTF-8のマルチバイト文字の開始バイトを探す
        // UTF-8の続行バイトは「10xxxxxx」の形（0x80〜0xBF）になります
        for (int i = len - 1; i >= 0; i--) {
            uint8_t b = str.charAt(i);
            if ((b & 0xC0) != 0x80) { // 続行バイトではない＝文字の開始位置
                lastCharBytes = len - i;
                break;
            }
        }
        
        // 最後の1文字分を切り出す
        return str.substring(len - lastCharBytes);
    }

    // UTF-8文字列から最後の1文字を削除する関数
    String removeLastChar(String str) {
        int len = str.length();
        if (len == 0) return "";

        int lastCharBytes = 1;
        
        // 末尾から手前に向かって、UTF-8文字の開始バイト（10xxxxxx 以外）を探す
        for (int i = len - 1; i >= 0; i--) {
            uint8_t b = str.charAt(i);
            // 0xC0（11000000）でAND演算し、結果が0x80（10000000）でなければ文字の先頭
            if ((b & 0xC0) != 0x80) { 
                lastCharBytes = len - i; // 最後の文字が何バイトだったかを計算
                break;
            }
        }
        
        // 全体の長さから、最後の1文字分のバイト数を引いて切り出す
        return str.substring(0, len - lastCharBytes);
    }

    // 文字列の先頭1文字（UTF-8考慮）を取得する
    String getFirstChar(const String& str) {
        if (str.length() == 0) return "";

        uint8_t firstByte = (uint8_t)str.charAt(0);
        int charLen;

        if ((firstByte & 0x80) == 0x00) {
            charLen = 1;       // 0xxxxxxx → ASCII(アルファベット等)
        } else if ((firstByte & 0xE0) == 0xC0) {
            charLen = 2;       // 110xxxxx → 2バイト文字
        } else if ((firstByte & 0xF0) == 0xE0) {
            charLen = 3;       // 1110xxxx → 3バイト文字(日本語の大半)
        } else if ((firstByte & 0xF8) == 0xF0) {
            charLen = 4;       // 11110xxx → 4バイト文字(絵文字など)
        } else {
            charLen = 1;       // 不正なバイト列への保険
        }

        // 文字列長を超えないようにクリップ
        charLen = min(charLen, (int)str.length());

        return str.substring(0, charLen);
    }

    // アルファベット1文字かどうかの判定
    bool isAsciiAlpha(const String& firstChar) {
        return firstChar.length() == 1 && isAlpha(firstChar.charAt(0));
    }

    String replaceLastChar(const String& str, const String& newText) {
        if (str.length() == 0) return str;

        int lastCharStart = str.length() - 1;
        
        while (lastCharStart > 0) {
            uint8_t byte = (uint8_t)str.charAt(lastCharStart);
            if ((byte & 0xC0) != 0x80) {
                break;
            }
            lastCharStart--;
        }

        return str.substring(0, lastCharStart) + newText;
    }

    static uint32_t utf8Decode(const uint8_t* s, int& len) {
        uint8_t c = s[0];
        if (c < 0x80) { len = 1; return c; }
        else if ((c & 0xE0) == 0xC0) { len = 2; return ((c & 0x1F) << 6) | (s[1] & 0x3F); }
        else if ((c & 0xF0) == 0xE0) { len = 3; return ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F); }
        else if ((c & 0xF8) == 0xF0) { len = 4; return ((c & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F); }
        len = 1; return c; // 不正なバイト列のフォールバック
    }

    // codepoint を UTF-8 の3バイト（日本語の範囲は基本ここ）としてバッファに書き込む
    static int utf8Encode3(uint32_t cp, uint8_t* out) {
        out[0] = 0xE0 | ((cp >> 12) & 0x0F);
        out[1] = 0x80 | ((cp >> 6) & 0x3F);
        out[2] = 0x80 | (cp & 0x3F);
        return 3;
    }

    String hiraganaToKatakana(const String& input) {
        String result;
        result.reserve(input.length());

        const uint8_t* p = (const uint8_t*)input.c_str();
        int total = input.length();
        int i = 0;

        while (i < total) {
            int len;
            uint32_t cp = utf8Decode(p + i, len);

            if (cp >= 0x3041 && cp <= 0x3096) {
                // ひらがな範囲 → カタカナへ
                uint8_t buf[3];
                int n = utf8Encode3(cp + 0x60, buf);
                result.concat((const char*)buf, n);
            } else {
                // それ以外はそのままコピー
                result.concat((const char*)(p + i), len);
            }

            i += len;
        }

        return result;
    }
}