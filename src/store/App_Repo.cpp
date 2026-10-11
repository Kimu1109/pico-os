#include "store/App_Repo.hpp"
#include "store/App_Repo_Keys.hpp"

#include <cstring>
#include <cstdlib>

#include "monocypher-ed25519.h"

namespace {
    // line をタブで最大 max 個に分ける(その場で書き換える。末尾の\r\nは除く)。最後の列は残り全部。
    // 分けた数を返す(作業用の写しを取らないのは、1行512バイトをスタックに重ねないため)
    int SplitTabs(char* line, char** cols, int max){
        size_t n = strlen(line);
        while(n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
        int count = 0;
        char* p = line;
        cols[count++] = p;
        while(count < max){
            char* t = strchr(p, '\t');
            if(!t) break;
            *t = '\0';
            p = t + 1;
            cols[count++] = p;
        }
        return count;
    }

    bool ParseU32(const char* s, uint32_t& out){
        if(!s || !*s) return false;
        uint64_t v = 0;
        for(const char* p = s; *p; p++){
            if(*p < '0' || *p > '9') return false;
            v = v * 10 + (uint64_t)(*p - '0');
            if(v > 0xFFFFFFFFULL) return false;
        }
        out = (uint32_t)v;
        return true;
    }

    int HexVal(char c){
        if(c >= '0' && c <= '9') return c - '0';
        if(c >= 'a' && c <= 'f') return c - 'a' + 10;
        if(c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    bool AllZero(const uint8_t* p, size_t n){
        uint8_t acc = 0;
        for(size_t i = 0; i < n; i++) acc |= p[i];
        return acc == 0;
    }
}

bool AppRepo::IsSkipLine(const char* line){
    const char c = line[0];
    return c == '\0' || c == '\n' || c == '\r' || c == '#';
}

bool AppRepo::ValidId(const char* s){
    if(!s || !*s) return false;
    size_t n = 0;
    for(const char* p = s; *p; p++, n++){
        const char c = *p;
        if(!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    }
    return n < PICO_STR_S;
}

bool AppRepo::ValidVersion(const char* s){
    if(!s || !*s) return false;
    size_t n = 0;
    char prev = '.';
    for(const char* p = s; *p; p++, n++){
        const char c = *p;
        if(c == '.'){
            if(prev == '.') return false; //空の区切り
        }else if(c < '0' || c > '9'){
            return false;
        }
        prev = c;
    }
    return prev != '.' && n < PICO_STR_S;
}

bool AppRepo::ValidDirName(const char* s){
    if(!s || !*s || s[0] == '.') return false;
    size_t n = 0;
    for(const char* p = s; *p; p++, n++){
        const unsigned char c = (unsigned char)*p;
        if(c < 0x20 || c == 0x7F) return false;
        if(strchr("/\\:*?\"<>|", c)) return false;
    }
    const char last = s[n - 1];
    return last != ' ' && last != '.' && n < PICO_STR_M;
}

bool AppRepo::ValidRelPath(const char* s){
    if(!s || !*s) return false;
    const size_t n = strlen(s);
    if(n > kMaxPathLen) return false;
    int depth = 1;
    bool seg_start = true;
    for(const char* p = s; *p; p++){
        const char c = *p;
        if(c == '/'){
            if(seg_start) return false; //空の区切り(先頭の"/"・"//")
            seg_start = true;
            if(++depth > kMaxPathDepth) return false;
            continue;
        }
        if(seg_start && c == '.') return false; //"."・".."・隠しファイル
        seg_start = false;
        if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
             || c == '.' || c == '-' || c == '_')) return false;
    }
    return !seg_start; //末尾の"/"は不可
}

int AppRepo::CompareVersion(const char* a, const char* b){
    while(*a || *b){
        uint32_t va = 0, vb = 0;
        while(*a && *a != '.'){ va = va * 10 + (uint32_t)(*a - '0'); a++; }
        while(*b && *b != '.'){ vb = vb * 10 + (uint32_t)(*b - '0'); b++; }
        if(va != vb) return va < vb ? -1 : 1;
        if(*a == '.') a++;
        if(*b == '.') b++;
    }
    return 0;
}

bool AppRepo::HexDecode(const char* hex, size_t hex_len, uint8_t* out, size_t out_len){
    if(!hex || hex_len != out_len * 2) return false;
    for(size_t i = 0; i < out_len; i++){
        const int hi = HexVal(hex[i * 2]);
        const int lo = HexVal(hex[i * 2 + 1]);
        if(hi < 0 || lo < 0) return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

void AppRepo::HexEncode(const uint8_t* in, size_t len, char* out){
    static const char d[] = "0123456789abcdef";
    for(size_t i = 0; i < len; i++){
        out[i * 2] = d[in[i] >> 4];
        out[i * 2 + 1] = d[in[i] & 15];
    }
    out[len * 2] = '\0';
}

bool AppRepo::ParsePerms(const char* s, uint8_t& out){
    out = 0;
    if(!s) return false;
    if(strcmp(s, "-") == 0 || *s == '\0') return true;
    const char* p = s;
    while(*p){
        const char* e = strchr(p, ',');
        const size_t n = e ? (size_t)(e - p) : strlen(p);
        if(n == 7 && strncmp(p, "network", 7) == 0) out |= PermNetwork;
        else if(n == 6 && strncmp(p, "notify", 6) == 0) out |= PermNotify;
        else if(n == 2 && strncmp(p, "sd", 2) == 0) out |= PermSd;
        else return false;
        p += n;
        if(*p == ',') p++;
    }
    return true;
}

void AppRepo::PermsText(uint8_t perms, FixedString<PICO_STR_M>& out){
    out.clear();
    if(perms & PermNetwork){ out.append("ネットワーク"); }
    if(perms & PermNotify){ if(!out.empty()) out.append("・"); out.append("通知"); }
    if(perms & PermSd){ if(!out.empty()) out.append("・"); out.append("SD全体"); }
    if(out.empty()) out.assign("なし");
}

bool AppRepo::ParseCatalogHeader(char* line, uint32_t& serial){
    char* cols[4];
    const int n = SplitTabs(line, cols, 4);
    if(n < 3) return false;
    if(strcmp(cols[0], kCatalogMagic) != 0) return false;
    uint32_t fmt = 0;
    if(!ParseU32(cols[1], fmt) || fmt != (uint32_t)kCatalogFormat) return false;
    //4列目以降は将来の拡張(無視する)
    return ParseU32(cols[2], serial);
}

bool AppRepo::ParseCatalogLine(char* line, Entry& out, bool& skip){
    skip = IsSkipLine(line);
    if(skip) return false;
    char* cols[9];
    const int n = SplitTabs(line, cols, 9);
    if(n < 8) return false;
    //9列目以降は将来の拡張(無視する。description にタブは入らない取り決め)
    out = Entry{};
    if(!ValidId(cols[0]) || !out.id.assign(cols[0])) return false;
    if(!ValidVersion(cols[1]) || !out.version.assign(cols[1])) return false;
    if(!ValidDirName(cols[2]) || !out.dir.assign(cols[2])) return false;
    if(!*cols[3]) return false;
    out.name.assign(cols[3]); //表示だけなので切り詰めてよい
    if(!ParseU32(cols[4], out.bytes) || out.bytes > kMaxAppBytes) return false;
    if(!HexDecode(cols[5], strlen(cols[5]), out.files_sha256, 32)) return false;
    if(!ParsePerms(cols[6], out.perms)) return false;
    out.description.assign(cols[7]);
    return true;
}

bool AppRepo::ParseFilesLine(char* line, FileEntry& out, bool& skip){
    skip = IsSkipLine(line);
    if(skip) return false;
    char* cols[4];
    const int n = SplitTabs(line, cols, 4);
    if(n < 3) return false;
    out = FileEntry{};
    if(!ValidRelPath(cols[0]) || !out.path.assign(cols[0])) return false;
    if(!ParseU32(cols[1], out.bytes) || out.bytes > kMaxFileBytes) return false;
    return HexDecode(cols[2], strlen(cols[2]), out.sha256, 32);
}

bool AppRepo::ParseSignature(const char* text, uint8_t pubkey[32], uint8_t sig[64]){
    if(!text) return false;
    const char* p = text;
    while(*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if(strncmp(p, "ed25519", 7) != 0) return false;
    p += 7;
    auto field = [&](uint8_t* out, size_t out_len) -> bool {
        if(*p != ' ' && *p != '\t') return false;
        while(*p == ' ' || *p == '\t') p++;
        const char* s = p;
        while(HexVal(*p) >= 0) p++;
        return HexDecode(s, (size_t)(p - s), out, out_len);
    };
    if(!field(pubkey, 32) || !field(sig, 64)) return false;
    while(*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return *p == '\0';
}

bool AppRepo::VerifyCatalog(const uint8_t catalog_sha256[32], const uint8_t pubkey[32], const uint8_t sig[64]){
    if(AllZero(pubkey, 32)) return false;
    uint8_t msg[kSignMessageBytes];
    memcpy(msg, kSignContext, 16);
    memcpy(msg + 16, catalog_sha256, 32);
    return crypto_ed25519_check(sig, pubkey, msg, sizeof(msg)) == 0;
}

bool AppRepo::IsBuiltinKey(const uint8_t pubkey[32]){
    if(AllZero(pubkey, 32)) return false;
    for(int i = 0; i < AppRepoKeys::kBuiltinCount; i++){
        if(memcmp(AppRepoKeys::kBuiltin[i].pub, pubkey, 32) == 0) return true;
    }
    return false;
}
