// src/util/Secret_Cipher.hpp
//
// Wi-FiのSSID/パスワードをSDへ書く前に難読化するための、軽量な対称暗号。
//
// ============================================================================
// 前提となる脅威モデルと限界(相談の経緯。実装前にCLAUDE.mdの
// 「メモリ計測の結論」等と同じ位置づけで必ず読むこと)
// ============================================================================
// - 守れるのは「SDカードだけを紛失/盗難された場合に、中の network.cfg を
//   直接テキストエディタで開かれても平文のSSID/パスワードが読めない」という
//   ケースだけ。
// - 本体(基板)ごと持ち去られた場合は無力。鍵(kKey)はこのファイルにハード
//   コードされており、実機のフラッシュに焼かれるだけで「SDとは別の媒体に
//   置く」以上の意味は無い。SWD等でフラッシュを吸い出せば鍵も一緒に読める。
// - 鍵はこのリポジトリを見れば誰でも分かる既定値のままなので、「肩越しに
//   見られた・別人にSDだけ渡した」程度の偶発的な漏洩を防ぐのが目的であり、
//   本気の攻撃者(ファームウェアの吸い出しを厭わない相手)は防げない。
//   本格的に守るならRP2350のOTP/Secure Bootでデバイス固有鍵を焼く必要が
//   あるが、それは別の(実機検証が要る)話としてここでは扱わない。
// - **実運用でリポジトリをforkして使う場合は、下のkKeyを必ず自分だけの
//   値へ書き換えること。** 既定値のままでは「保護している」という誤った
//   安心感を生むだけになる。
//
// ============================================================================
// 実装方式
// ============================================================================
// XTEA(128bit固定鍵)をブロック暗号として使い、CTRモードの要領で
// 「用途文字列(purpose。例: "wifi-password")のFNV-1aハッシュ」を初期カウンタに
// した鍵ストリームを生成し、平文とXORするだけ。ブロック暗号なので暗号化と
// 復号は同じ操作(XOR)になる。
//
// - purposeを鍵ストリームの種として混ぜているのは、SSIDとパスワードのように
//   同じ鍵で複数の値を暗号化しても、それぞれ別々の鍵ストリームになるようにするため
//   (同じストリームを使い回すと、2つの暗号文をXORするだけで平文同士のXORが
//   漏れる、というストリーム暗号の典型的な弱点を避けられる)。
// - 動的メモリ確保は一切しない(固定長スタックバッファのみ)。RAM/Flash制約の
//   厳しい組み込み環境向けの他のモジュールと同じ方針。
// - 保存形式は "enc1:" 接頭辞 + 16進文字列。接頭辞が無い値は「まだ暗号化
//   されていない(旧データ、または移行前)平文」とみなしてそのまま返す
//   (後方互換。SetValue()で書き戻す際に暗号化形式へ移行する)。
// ============================================================================

#pragma once
#include <stdint.h>
#include <string.h>
#include <stddef.h>

namespace PICO_Secret
{
    // --------------------------------------------------------------------
    // 固定鍵(128bit)。**forkして実運用するときは必ずここを書き換えること。**
    // 値そのものに意味は無く、ランダムな32bit値4つであればよい。
    // --------------------------------------------------------------------
    constexpr uint32_t kKey[4] = { 0x9F3B1A7Cu, 0x452E8D01u, 0xC7A6F350u, 0x1B84DE29u };

    // Wi-Fiパスフレーズ/64桁PSK/SSIDなら64で足りるが、カレンダーの非公開URL(Googleは
    // 200文字を超えることがある。Url::pathをPICO_STR_LLへ広げた経緯と同じ理由)まで
    // 暗号化対象にしたため255に拡張した(2026-09-27)。既存の暗号文字列(Wi-Fi/チャット)の
    // 復号には影響しない(上限を緩めるだけの後方互換な変更)。
    constexpr size_t kMaxPlainBytes = 255;
    constexpr const char *kPrefix = "enc1:";
    constexpr size_t kPrefixLen = 5;

    // XTEAの1ブロック(64bit=32bit×2)を暗号化する。ストリーム生成専用の下請けで、
    // 単体でのブロック暗号として外から使うことは想定していない。
    inline void XteaEncryptBlock(uint32_t &v0, uint32_t &v1, const uint32_t key[4])
    {
        constexpr uint32_t kDelta = 0x9E3779B9u;
        uint32_t sum = 0;
        for (int i = 0; i < 32; ++i)
        {
            v0 += (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + key[sum & 3]);
            sum += kDelta;
            v1 += (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + key[(sum >> 11) & 3]);
        }
    }

    // 用途文字列から64bitの初期値を作るためだけのFNV-1a(暗号学的な強度は求めていない)
    inline uint64_t Fnv1aHash(const char *s)
    {
        uint64_t h = 1469598103934665603ull;
        for (; *s != '\0'; ++s)
        {
            h ^= (uint8_t)*s;
            h *= 1099511628211ull;
        }
        return h;
    }

    // data[0..len) を鍵ストリームとXORする(in-place)。対称操作なので暗号化・復号どちらもこれ1つ。
    inline void XorStream(const char *purpose, uint8_t *data, size_t len)
    {
        const uint64_t base = Fnv1aHash(purpose);
        const uint32_t base0 = (uint32_t)(base & 0xFFFFFFFFu);
        const uint32_t base1 = (uint32_t)(base >> 32);

        size_t pos = 0;
        uint32_t counter = 0;
        while (pos < len)
        {
            uint32_t v0 = base0;
            uint32_t v1 = base1 ^ counter;
            XteaEncryptBlock(v0, v1, kKey);

            uint8_t block[8];
            memcpy(block, &v0, 4);
            memcpy(block + 4, &v1, 4);

            const size_t n = (len - pos < 8) ? (len - pos) : 8;
            for (size_t i = 0; i < n; ++i)
            {
                data[pos + i] ^= block[i];
            }
            pos += n;
            ++counter;
        }
    }

    inline char HexDigit(uint8_t v)
    {
        return (v < 10) ? (char)('0' + v) : (char)('a' + (v - 10));
    }

    inline bool HexNibble(char c, uint8_t &out)
    {
        if (c >= '0' && c <= '9') { out = (uint8_t)(c - '0'); return true; }
        if (c >= 'a' && c <= 'f') { out = (uint8_t)(c - 'a' + 10); return true; }
        if (c >= 'A' && c <= 'F') { out = (uint8_t)(c - 'A' + 10); return true; }
        return false;
    }

    // --------------------------------------------------------------------
    // 平文(NUL終端)を暗号化し、"enc1:"+16進文字列(NUL終端)をoutへ書く。
    // outCapが足りない/平文がkMaxPlainBytesを超える場合はfalseで失敗する
    // (out自体は書き換えない)。
    // --------------------------------------------------------------------
    inline bool Encrypt(const char *purpose, const char *plaintext, char *out, size_t outCap)
    {
        const size_t len = strlen(plaintext);
        if (len == 0)
        {
            // 空文字列は「未設定」の意味を保つため、暗号化せずそのまま返す
            if (outCap < 1) return false;
            out[0] = '\0';
            return true;
        }
        if (len > kMaxPlainBytes) return false;

        const size_t needed = kPrefixLen + len * 2 + 1;
        if (outCap < needed) return false;

        uint8_t buf[kMaxPlainBytes];
        memcpy(buf, plaintext, len);
        XorStream(purpose, buf, len);

        memcpy(out, kPrefix, kPrefixLen);
        for (size_t i = 0; i < len; ++i)
        {
            out[kPrefixLen + i * 2]     = HexDigit(buf[i] >> 4);
            out[kPrefixLen + i * 2 + 1] = HexDigit(buf[i] & 0x0F);
        }
        out[kPrefixLen + len * 2] = '\0';
        return true;
    }

    // --------------------------------------------------------------------
    // Config上のvalueを復号してoutへ書く。
    // - "enc1:"接頭辞が無ければ「まだ暗号化されていない平文」とみなし、
    //   そのままコピーして返す(後方互換。既存のnetwork.cfgを壊さないため)
    // - 接頭辞はあるが16進として不正/長さが奇数/outCap不足の場合は、
    //   壊れたデータとして空文字列を書きfalseを返す
    // --------------------------------------------------------------------
    inline bool Decrypt(const char *purpose, const char *value, char *out, size_t outCap)
    {
        if (strncmp(value, kPrefix, kPrefixLen) != 0)
        {
            // 後方互換: まだ暗号化されていない平文をそのまま扱う
            const size_t len = strlen(value);
            if (outCap < len + 1)
            {
                if (outCap > 0) out[0] = '\0';
                return false;
            }
            memcpy(out, value, len + 1);
            return true;
        }

        const char *hex = value + kPrefixLen;
        const size_t hexLen = strlen(hex);
        if (hexLen % 2 != 0 || hexLen / 2 > kMaxPlainBytes)
        {
            if (outCap > 0) out[0] = '\0';
            return false;
        }

        const size_t plainLen = hexLen / 2;
        if (outCap < plainLen + 1)
        {
            if (outCap > 0) out[0] = '\0';
            return false;
        }

        uint8_t buf[kMaxPlainBytes];
        for (size_t i = 0; i < plainLen; ++i)
        {
            uint8_t hi, lo;
            if (!HexNibble(hex[i * 2], hi) || !HexNibble(hex[i * 2 + 1], lo))
            {
                out[0] = '\0';
                return false;
            }
            buf[i] = (uint8_t)((hi << 4) | lo);
        }

        XorStream(purpose, buf, plainLen);
        memcpy(out, buf, plainLen);
        out[plainLen] = '\0';
        return true;
    }
}
