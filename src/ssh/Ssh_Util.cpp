#include "ssh/Ssh_Util.hpp"
#include "ssh/Ssh_Sha256.hpp"
#include "util/Secret_Cipher.hpp"
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

namespace {
    const char kKeyTag[] = "pico-ssh-ed25519:";

    const char* SkipSpace(const char* text, size_t& len){
        while(len > 0 && (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n')){
            text++;
            len--;
        }
        return text;
    }

    int HexVal(char c){
        if(c >= '0' && c <= '9') return c - '0';
        if(c >= 'a' && c <= 'f') return c - 'a' + 10;
        if(c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }
}

bool SshUtil::IsEncryptedKeyText(const char* text, size_t len){
    text = SkipSpace(text, len);
    return len >= 5 && memcmp(text, "enc1:", 5) == 0;
}

bool SshUtil::EncryptKey(const uint8_t secret[64], char* out, size_t outCap){
    static const char* d = "0123456789abcdef";
    char plain[sizeof(kKeyTag) + 128];
    memcpy(plain, kKeyTag, sizeof(kKeyTag) - 1);
    char* h = plain + sizeof(kKeyTag) - 1;
    for(int i = 0; i < 64; i++){
        *h++ = d[secret[i] >> 4];
        *h++ = d[secret[i] & 15];
    }
    *h = '\0';
    const bool ok = PICO_Secret::Encrypt(kKeyPurpose, plain, out, outCap);
    memset(plain, 0, sizeof(plain));
    return ok;
}

SshUtil::KeyResult SshUtil::DecryptKey(const char* text, size_t len, uint8_t secret[64], uint8_t pub[32]){
    text = SkipSpace(text, len);
    if(len < 5 || memcmp(text, "enc1:", 5) != 0) return KeyResult::NotFound;
    //1行目だけ(末尾の改行を落とす)
    size_t n = 0;
    while(n < len && text[n] != '\r' && text[n] != '\n') n++;
    char line[kEncryptedKeyCap];
    if(n >= sizeof(line)) return KeyResult::Broken;
    memcpy(line, text, n);
    line[n] = '\0';

    char plain[PICO_Secret::kMaxPlainBytes + 1];
    const size_t tag = sizeof(kKeyTag) - 1;
    KeyResult r = KeyResult::Broken;
    //鍵(kKey)が違うファームで作られたファイルは、復号しても目印が合わないので壊れた扱いになる
    if(PICO_Secret::Decrypt(kKeyPurpose, line, plain, sizeof(plain)) &&
       strlen(plain) == tag + 128 && memcmp(plain, kKeyTag, tag) == 0){
        r = KeyResult::Ok;
        for(int i = 0; i < 64; i++){
            const int hi = HexVal(plain[tag + i * 2]);
            const int lo = HexVal(plain[tag + i * 2 + 1]);
            if(hi < 0 || lo < 0){
                r = KeyResult::Broken;
                break;
            }
            secret[i] = (uint8_t)((hi << 4) | lo);
        }
        if(r == KeyResult::Ok) memcpy(pub, secret + 32, 32);
    }
    memset(plain, 0, sizeof(plain));
    memset(line, 0, sizeof(line));
    return r;
}

namespace {
    // 平文の鍵ファイルを暗号化した形で書き直す。一時ファイルへ書き、読み戻して同じ鍵に戻ることを
    // 確かめてから差し替える(途中で失敗しても元の鍵ファイルは壊さない)
    bool RewriteEncrypted(const char* path, const uint8_t secret[64]){
        char enc[SshUtil::kEncryptedKeyCap + 1];
        if(!SshUtil::EncryptKey(secret, enc, sizeof(enc) - 1)) return false;
        const size_t n = strlen(enc);
        enc[n] = '\n';

        char tmp[128];
        if(snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) return false;
        OSData::SD.remove(tmp);
        FsFile f = OSData::SD.open(tmp, O_WRONLY | O_CREAT | O_TRUNC);
        if(!f) return false;
        const bool wrote = f.write((const uint8_t*)enc, n + 1) == n + 1;
        f.close();

        bool ok = wrote;
        if(ok){
            //読み戻して確かめる
            char back[SshUtil::kEncryptedKeyCap + 8];
            FsFile r = OSData::SD.open(tmp, O_RDONLY);
            int got = r ? r.read((uint8_t*)back, sizeof(back)) : -1;
            if(r) r.close();
            uint8_t s2[64], p2[32];
            ok = got > 0 && SshUtil::DecryptKey(back, (size_t)got, s2, p2) == SshUtil::KeyResult::Ok &&
                 memcmp(s2, secret, 64) == 0;
            memset(s2, 0, sizeof(s2));
        }
        ok = ok && OSData::SD.remove(path) && OSData::SD.rename(tmp, path);
        if(!ok) OSData::SD.remove(tmp);
        memset(enc, 0, sizeof(enc));
        return ok;
    }
}

SshUtil::KeyResult SshUtil::LoadPrivateKey(const char* path, uint8_t secret[64], uint8_t pub[32], KeyRewrite* rewrite){
    if(rewrite) *rewrite = KeyRewrite::None;
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

    KeyResult r = KeyResult::Broken;
    if(n > 0 && IsEncryptedKeyText(text, (size_t)n)){
        r = DecryptKey(text, (size_t)n, secret, pub);
    }else if(n > 0){
        r = ParsePrivateKey(text, (size_t)n, secret, pub);
        //平文の鍵だった。次からはSDに平文で残らないよう、暗号化して書き直す
        if(r == KeyResult::Ok && rewrite){
            *rewrite = RewriteEncrypted(path, secret) ? KeyRewrite::Encrypted : KeyRewrite::Failed;
        }else if(r == KeyResult::Ok){
            RewriteEncrypted(path, secret);
        }
    }
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
