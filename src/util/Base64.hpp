#pragma once

#include <cstddef>
#include <cstdint>

// base64(RFC 4648)。確保をしない固定バッファ版。LuaのBase64とencrypt()の出力が使う。
// url_safe=trueなら '+' '/' の代わりに '-' '_' を使い、パディング('=')を付けない。
namespace Base64 {

    inline size_t EncodedSize(size_t n, bool url_safe = false) {
        return url_safe ? (n * 4 + 2) / 3 : ((n + 2) / 3) * 4;
    }

    // 書いたバイト数を返す(終端のNULは付けない)。outはEncodedSize(n)バイト以上であること
    inline size_t Encode(const uint8_t* in, size_t n, char* out, bool url_safe = false) {
        static const char* kStd = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        static const char* kUrl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
        const char* t = url_safe ? kUrl : kStd;
        size_t o = 0;
        size_t i = 0;
        while (i + 3 <= n) {
            const uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8) | in[i + 2];
            out[o++] = t[(v >> 18) & 63]; out[o++] = t[(v >> 12) & 63];
            out[o++] = t[(v >> 6) & 63];  out[o++] = t[v & 63];
            i += 3;
        }
        if (i < n) {
            uint32_t v = (uint32_t)in[i] << 16;
            if (i + 1 < n) v |= (uint32_t)in[i + 1] << 8;
            out[o++] = t[(v >> 18) & 63];
            out[o++] = t[(v >> 12) & 63];
            if (i + 1 < n) {
                out[o++] = t[(v >> 6) & 63];
                if (!url_safe) out[o++] = '=';
            } else if (!url_safe) {
                out[o++] = '=';
                out[o++] = '=';
            }
        }
        return o;
    }

    inline int Value(char c) {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+' || c == '-') return 62;
        if (c == '/' || c == '_') return 63;
        return -1;
    }

    // 標準とURL安全の両方を受け付ける。空白(改行を含む)は飛ばし、パディングは無くてもよい。
    // 不正な文字・不正な長さならfalse。outはn*3/4以上のバイト数を用意すること。out_lenへ書いた長さ
    inline bool Decode(const char* in, size_t n, uint8_t* out, size_t* out_len) {
        uint32_t acc = 0;
        int bits = 0;
        size_t o = 0;
        size_t count = 0;
        bool padding = false;
        for (size_t i = 0; i < n; i++) {
            const char c = in[i];
            if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
            if (c == '=') { padding = true; continue; }
            if (padding) return false; // パディングの後に文字は来ない
            const int v = Value(c);
            if (v < 0) return false;
            acc = (acc << 6) | (uint32_t)v;
            bits += 6;
            count++;
            if (bits >= 8) {
                bits -= 8;
                out[o++] = (uint8_t)((acc >> bits) & 0xFF);
            }
        }
        if (count % 4 == 1) return false; // 1文字余りは作れない
        *out_len = o;
        return true;
    }
}
