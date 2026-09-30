#include "ssh/Ssh_Util.hpp"
#include "ssh/Ssh_Sha256.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstdlib>

#if !defined(PICOOS_PC)
    #include <Arduino.h>
#endif

namespace {
    const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    int B64Value(char c){
        if(c >= 'A' && c <= 'Z') return c - 'A';
        if(c >= 'a' && c <= 'z') return c - 'a' + 26;
        if(c >= '0' && c <= '9') return c - '0' + 52;
        if(c == '+') return 62;
        if(c == '/') return 63;
        return -1;
    }

    uint32_t Be32(const uint8_t* p){
        return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
    }

    // SSHの string を1つ読む。足りなければfalse
    bool ReadString(const uint8_t*& p, const uint8_t* end, const uint8_t*& s, uint32_t& n){
        if(end - p < 4) return false;
        n = Be32(p);
        p += 4;
        if((uint32_t)(end - p) < n) return false;
        s = p;
        p += n;
        return true;
    }

    bool StrEq(const uint8_t* s, uint32_t n, const char* lit){
        return n == strlen(lit) && memcmp(s, lit, n) == 0;
    }

    // known_hostsの1つめの欄に書く名前
    void HostPattern(const char* host, uint16_t port, char* out, size_t cap){
        if(port == 22) snprintf(out, cap, "%s", host);
        else snprintf(out, cap, "[%s]:%u", host, (unsigned)port);
    }
}

void SshUtil::Random(uint8_t* out, size_t n){
#if defined(PICOOS_PC)
    FILE* f = fopen("/dev/urandom", "rb");
    size_t got = 0;
    if(f){
        got = fread(out, 1, n, f);
        fclose(f);
    }
    if(got != n){
        // /dev/urandomが無い環境は想定していない。鍵に使えない乱数で黙って進めないよう止める
        fprintf(stderr, "[SSH] 乱数を取得できません\n");
        abort();
    }
#else
    size_t i = 0;
    while(i < n){
        const uint32_t r = rp2040.hwrand32();
        for(int k = 0; k < 4 && i < n; k++) out[i++] = (uint8_t)(r >> (k * 8));
    }
#endif
}

size_t SshUtil::Base64Encode(const uint8_t* in, size_t n, char* out, size_t outCap){
    const size_t need = (n + 2) / 3 * 4 + 1;
    if(outCap < need) return 0;
    size_t o = 0;
    for(size_t i = 0; i < n; i += 3){
        const uint32_t v = ((uint32_t)in[i] << 16) |
                           ((i + 1 < n ? (uint32_t)in[i + 1] : 0) << 8) |
                           (i + 2 < n ? (uint32_t)in[i + 2] : 0);
        out[o++] = kB64[(v >> 18) & 63];
        out[o++] = kB64[(v >> 12) & 63];
        out[o++] = i + 1 < n ? kB64[(v >> 6) & 63] : '=';
        out[o++] = i + 2 < n ? kB64[v & 63] : '=';
    }
    out[o] = '\0';
    return o;
}

int SshUtil::Base64Decode(const char* in, size_t n, uint8_t* out, size_t outCap){
    uint32_t acc = 0;
    int bits = 0;
    size_t o = 0;
    for(size_t i = 0; i < n; i++){
        const char c = in[i];
        if(c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
        if(c == '=') break;
        const int v = B64Value(c);
        if(v < 0) return -1;
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if(bits >= 8){
            bits -= 8;
            if(o >= outCap) return -1;
            out[o++] = (uint8_t)(acc >> bits);
        }
    }
    return (int)o;
}

void SshUtil::Ed25519Blob(const uint8_t pub[32], uint8_t out[kEd25519BlobBytes]){
    const uint8_t head[] = { 0, 0, 0, 11, 's', 's', 'h', '-', 'e', 'd', '2', '5', '5', '1', '9', 0, 0, 0, 32 };
    memcpy(out, head, sizeof(head));
    memcpy(out + sizeof(head), pub, 32);
}

void SshUtil::Fingerprint(const uint8_t pub[32], char* out, size_t outCap){
    uint8_t blob[kEd25519BlobBytes];
    Ed25519Blob(pub, blob);
    uint8_t digest[32];
    Sha256::Hash(blob, sizeof(blob), digest);
    char b64[48];
    Base64Encode(digest, sizeof(digest), b64, sizeof(b64));
    //OpenSSHと同じく末尾の'='は付けない
    char* eq = strchr(b64, '=');
    if(eq) *eq = '\0';
    snprintf(out, outCap, "SHA256:%s", b64);
}

SshUtil::KeyResult SshUtil::ParsePrivateKey(const char* text, size_t len, uint8_t secret[64], uint8_t pub[32]){
    static const char kBegin[] = "-----BEGIN OPENSSH PRIVATE KEY-----";
    static const char kEnd[] = "-----END OPENSSH PRIVATE KEY-----";

    const char* b = nullptr;
    for(size_t i = 0; i + sizeof(kBegin) - 1 <= len; i++){
        if(memcmp(text + i, kBegin, sizeof(kBegin) - 1) == 0){
            b = text + i + sizeof(kBegin) - 1;
            break;
        }
    }
    if(!b){
        //PEM(RSA等)の鍵は扱わない
        for(size_t i = 0; i + 10 <= len; i++){
            if(memcmp(text + i, "-----BEGIN", 10) == 0) return KeyResult::Unsupported;
        }
        return KeyResult::Broken;
    }
    const char* e = nullptr;
    for(const char* p = b; p + sizeof(kEnd) - 1 <= text + len; p++){
        if(memcmp(p, kEnd, sizeof(kEnd) - 1) == 0){
            e = p;
            break;
        }
    }
    if(!e) return KeyResult::Broken;

    // 鍵を読む間だけ確保する(常駐のRAMにしない)
    constexpr size_t kRawCap = 1024;
    uint8_t* raw = (uint8_t*)malloc(kRawCap);
    if(!raw) return KeyResult::Broken;
    const KeyResult r = ParseDecoded(raw, Base64Decode(b, (size_t)(e - b), raw, kRawCap), secret, pub);
    memset(raw, 0, kRawCap);
    free(raw);
    return r;
}

SshUtil::KeyResult SshUtil::ParseDecoded(const uint8_t* raw, int n, uint8_t secret[64], uint8_t pub[32]){
    if(n < 15 || memcmp(raw, "openssh-key-v1\0", 15) != 0) return KeyResult::Broken;

    const uint8_t* p = raw + 15;
    const uint8_t* end = raw + n;
    const uint8_t* s;
    uint32_t sn;
    if(!ReadString(p, end, s, sn)) return KeyResult::Broken; // ciphername
    if(!StrEq(s, sn, "none")) return KeyResult::Encrypted;
    if(!ReadString(p, end, s, sn)) return KeyResult::Broken; // kdfname
    if(!ReadString(p, end, s, sn)) return KeyResult::Broken; // kdfoptions
    if(end - p < 4) return KeyResult::Broken;
    if(Be32(p) != 1) return KeyResult::Unsupported;
    p += 4;
    if(!ReadString(p, end, s, sn)) return KeyResult::Broken; // 公開鍵
    const uint8_t* priv;
    uint32_t priv_n;
    if(!ReadString(p, end, priv, priv_n)) return KeyResult::Broken;

    p = priv;
    end = priv + priv_n;
    if(end - p < 8 || Be32(p) != Be32(p + 4)) return KeyResult::Broken;
    p += 8;
    if(!ReadString(p, end, s, sn)) return KeyResult::Broken;
    if(!StrEq(s, sn, "ssh-ed25519")) return KeyResult::Unsupported;
    const uint8_t* pk;
    uint32_t pk_n;
    if(!ReadString(p, end, pk, pk_n) || pk_n != 32) return KeyResult::Broken;
    const uint8_t* sk;
    uint32_t sk_n;
    if(!ReadString(p, end, sk, sk_n) || sk_n != 64) return KeyResult::Broken;
    if(memcmp(sk + 32, pk, 32) != 0) return KeyResult::Broken;

    memcpy(secret, sk, 64);
    memcpy(pub, pk, 32);
    return KeyResult::Ok;
}

SshUtil::KeyResult SshUtil::LoadPrivateKey(const char* path, uint8_t secret[64], uint8_t pub[32]){
    if(!OSData::SD_usable || !OSData::SD.exists(path)) return KeyResult::NotFound;
    FsFile f = OSData::SD.open(path, O_RDONLY);
    if(!f) return KeyResult::NotFound;
    constexpr size_t kTextCap = 2048; // ssh-ed25519の鍵ファイルは400バイト程度
    char* text = (char*)malloc(kTextCap);
    if(!text){
        f.close();
        return KeyResult::Broken;
    }
    const int n = f.read((uint8_t*)text, kTextCap);
    f.close();
    const KeyResult r = n > 0 ? ParsePrivateKey(text, (size_t)n, secret, pub) : KeyResult::Broken;
    memset(text, 0, kTextCap);
    free(text);
    return r;
}

SshUtil::HostStatus SshUtil::CheckKnownHost(const char* path, const char* host, uint16_t port, const uint8_t pub[32]){
    if(!OSData::SD_usable) return HostStatus::Unknown;
    FsFile f = OSData::SD.open(path, O_RDONLY);
    if(!f) return HostStatus::Unknown;

    char pattern[128];
    HostPattern(host, port, pattern, sizeof(pattern));
    const size_t plen = strlen(pattern);

    HostStatus result = HostStatus::Unknown;
    char line[512];
    while(true){
        const int n = f.fgets(line, sizeof(line));
        if(n <= 0) break;
        if(line[0] == '#' || line[0] == '|') continue; // コメント・ハッシュ化された名前

        //1つめの欄(カンマ区切りの名前の並び)に一致するか
        char* sp = strchr(line, ' ');
        if(!sp) continue;
        bool hit = false;
        const char* q = line;
        while(q < sp){
            const char* c = q;
            while(c < sp && *c != ',') c++;
            if((size_t)(c - q) == plen && memcmp(q, pattern, plen) == 0) hit = true;
            q = c + 1;
        }
        if(!hit) continue;

        char* type = sp + 1;
        char* sp2 = strchr(type, ' ');
        if(!sp2) continue;
        *sp2 = '\0';
        if(strcmp(type, "ssh-ed25519") != 0) continue; // 他の種類の鍵は比べない
        char* key = sp2 + 1;
        char* kend = key;
        while(*kend && *kend != ' ' && *kend != '\r' && *kend != '\n') kend++;

        uint8_t blob[64];
        const int bn = Base64Decode(key, (size_t)(kend - key), blob, sizeof(blob));
        uint8_t want[kEd25519BlobBytes];
        Ed25519Blob(pub, want);
        if(bn == (int)kEd25519BlobBytes && memcmp(blob, want, kEd25519BlobBytes) == 0){
            result = HostStatus::Match;
            break;
        }
        result = HostStatus::Mismatch;
    }
    f.close();
    return result;
}

bool SshUtil::AddKnownHost(const char* path, const char* host, uint16_t port, const uint8_t pub[32]){
    if(!OSData::SD_usable) return false;
    //親ディレクトリを作る
    char dir[128];
    snprintf(dir, sizeof(dir), "%s", path);
    char* slash = strrchr(dir, '/');
    if(slash && slash != dir){
        *slash = '\0';
        if(!OSData::SD.exists(dir)) OSData::SD.mkdir(dir);
    }

    FsFile f = OSData::SD.open(path, O_WRONLY | O_CREAT | O_APPEND);
    if(!f) return false;
    char pattern[128];
    HostPattern(host, port, pattern, sizeof(pattern));
    uint8_t blob[kEd25519BlobBytes];
    Ed25519Blob(pub, blob);
    char b64[80];
    Base64Encode(blob, sizeof(blob), b64, sizeof(b64));
    char line[256];
    const int n = snprintf(line, sizeof(line), "%s ssh-ed25519 %s\n", pattern, b64);
    const bool ok = n > 0 && f.write((const uint8_t*)line, (size_t)n) == (size_t)n;
    f.close();
    return ok;
}
