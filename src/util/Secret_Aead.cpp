#include "util/Secret_Aead.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "monocypher.h"
#include "util/Base64.hpp"
#include "util/Secret_Cipher.hpp"

#if !defined(PICOOS_PC)
#include <Arduino.h>
#endif

const char* SecretAead::ResultToStr(Result r) {
    switch (r) {
        case Result::Ok: return "ok";
        case Result::BadFormat: return "暗号文の形式が正しくありません";
        case Result::AuthFailed: return "復号できません(パスワード/アプリが違うか、改ざんされています)";
        case Result::NeedPassword: return "パスワードが必要です";
        case Result::TooBig: return "大きすぎます";
        case Result::NoMemory: return "メモリが足りません";
    }
    return "?";
}

bool SecretAead::IsEncrypted(const char* text, size_t len) {
    const size_t pl = strlen(kPrefix);
    return text && len >= pl && memcmp(text, kPrefix, pl) == 0;
}

void SecretAead::Random(uint8_t* out, size_t n) {
#if defined(PICOOS_PC)
    // Ssh_Util.cppのSshUtil::Random()と同じ。鍵に使えない乱数で黙って進まないよう、取れなければ止める
    FILE* f = fopen("/dev/urandom", "rb");
    size_t got = 0;
    if (f) {
        got = fread(out, 1, n, f);
        fclose(f);
    }
    if (got != n) {
        fprintf(stderr, "[AEAD] 乱数を取得できません\n");
        abort();
    }
#else
    size_t i = 0;
    while (i < n) {
        const uint32_t r = rp2040.hwrand32();
        for (int k = 0; k < 4 && i < n; k++) out[i++] = (uint8_t)(r >> (k * 8));
    }
#endif
}

void SecretAead::Blake2b(uint8_t* hash, size_t hash_size, const uint8_t* data, size_t n,
                         const uint8_t* key, size_t key_size) {
    crypto_blake2b_keyed(hash, hash_size, key, key_size, data, n);
}

namespace {
    constexpr size_t kNonce = 24;
    constexpr size_t kMac = 16;
    constexpr size_t kSalt = 16;

    // パスワード無し: 固定鍵とアプリ名から作る
    void DeviceKey(uint8_t key[32], const char* context) {
        std::string msg = "pico-lua-aead-v1";
        msg.push_back('\0');
        for (uint32_t w : PICO_Secret::kKey) {
            for (int k = 0; k < 4; k++) msg.push_back((char)(w >> (k * 8)));
        }
        msg += context ? context : "";
        crypto_blake2b(key, 32, (const uint8_t*)msg.data(), msg.size());
        crypto_wipe(&msg[0], msg.size());
    }

    bool PasswordKey(uint8_t key[32], const char* password, const uint8_t salt[kSalt],
                     uint32_t blocks, uint32_t passes) {
        void* work = malloc((size_t)blocks * 1024);
        if (!work) return false;
        crypto_argon2_config cfg;
        cfg.algorithm = CRYPTO_ARGON2_ID;
        cfg.nb_blocks = blocks;
        cfg.nb_passes = passes;
        cfg.nb_lanes = 1;
        crypto_argon2_inputs in;
        in.pass = (const uint8_t*)password;
        in.pass_size = (uint32_t)strlen(password);
        in.salt = salt;
        in.salt_size = kSalt;
        crypto_argon2(key, 32, work, cfg, in, crypto_argon2_no_extras);
        crypto_wipe(work, (size_t)blocks * 1024);
        free(work);
        return true;
    }
}

SecretAead::Result SecretAead::Encrypt(const uint8_t* plain, size_t n, const char* context,
                                       const char* password, std::string& out) {
    if (n > kMaxPlainBytes) return Result::TooBig;
    const bool use_pw = password && password[0];

    std::vector<uint8_t> buf;
    buf.reserve(1 + 3 + kSalt + kNonce + n + kMac);
    uint8_t key[32];

    buf.push_back(use_pw ? kVersionPassword : kVersionDevice);
    if (use_pw) {
        buf.push_back((uint8_t)kArgonPasses);
        buf.push_back((uint8_t)(kArgonBlocks & 0xFF));
        buf.push_back((uint8_t)(kArgonBlocks >> 8));
        uint8_t salt[kSalt];
        Random(salt, kSalt);
        buf.insert(buf.end(), salt, salt + kSalt);
        if (!PasswordKey(key, password, salt, kArgonBlocks, kArgonPasses)) return Result::NoMemory;
    } else {
        DeviceKey(key, context);
    }
    const size_t header = buf.size();

    uint8_t nonce[kNonce];
    Random(nonce, kNonce);
    buf.insert(buf.end(), nonce, nonce + kNonce);

    buf.resize(buf.size() + n + kMac);
    uint8_t* ct = buf.data() + header + kNonce;
    uint8_t* mac = ct + n;
    crypto_aead_lock(ct, mac, key, nonce, buf.data(), header, plain, n);
    crypto_wipe(key, sizeof(key));

    out.assign(kPrefix);
    const size_t enc = Base64::EncodedSize(buf.size(), true);
    const size_t base = out.size();
    out.resize(base + enc);
    const size_t written = Base64::Encode(buf.data(), buf.size(), &out[base], true);
    out.resize(base + written);
    return Result::Ok;
}

SecretAead::Result SecretAead::Decrypt(const char* text, size_t len, const char* context,
                                       const char* password, std::string& out) {
    if (!IsEncrypted(text, len)) return Result::BadFormat;
    if (len > kMaxEncodedBytes) return Result::TooBig;
    const size_t pl = strlen(kPrefix);

    std::vector<uint8_t> raw(len);   // base64より必ず短い
    size_t raw_len = 0;
    if (!Base64::Decode(text + pl, len - pl, raw.data(), &raw_len)) return Result::BadFormat;
    if (raw_len < 1) return Result::BadFormat;

    const uint8_t version = raw[0];
    size_t header = 1;
    uint8_t key[32];
    const bool use_pw = (version == kVersionPassword);
    uint32_t blocks = 0, passes = 0;
    const uint8_t* salt = nullptr;
    if (version == kVersionPassword) {
        header = 1 + 3 + kSalt;
        if (raw_len < header) return Result::BadFormat;
        passes = raw[1];
        blocks = (uint32_t)raw[2] | ((uint32_t)raw[3] << 8);
        salt = raw.data() + 4;
        if (passes < 1 || passes > kMaxArgonPasses || blocks < 8 || blocks > kMaxArgonBlocks) return Result::BadFormat;
    } else if (version != kVersionDevice) {
        return Result::BadFormat;
    }
    if (raw_len < header + kNonce + kMac) return Result::BadFormat;
    const size_t n = raw_len - header - kNonce - kMac;
    if (n > kMaxPlainBytes) return Result::TooBig;

    if (use_pw) {
        if (!password || !password[0]) return Result::NeedPassword;
        if (!PasswordKey(key, password, salt, blocks, passes)) return Result::NoMemory;
    } else {
        DeviceKey(key, context);
    }

    const uint8_t* nonce = raw.data() + header;
    const uint8_t* ct = nonce + kNonce;
    const uint8_t* mac = ct + n;
    out.resize(n);
    const int bad = crypto_aead_unlock(n ? (uint8_t*)&out[0] : nullptr, mac, key, nonce, raw.data(), header, ct, n);
    crypto_wipe(key, sizeof(key));
    if (bad) {
        out.clear();
        return Result::AuthFailed;
    }
    return Result::Ok;
}
