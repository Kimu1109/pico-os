#include "crypto/Secret_File.hpp"

#include "crypto/Secret_Cipher.hpp"
#include "OS_Data.hpp"
#include "consts.hpp"

#include <cstdio>
#include <cstring>

namespace {
    // 1行に何バイトずつ置くか(PICO_Secret の平文の上限)
    constexpr size_t kChunkBytes = PICO_Secret::kMaxPlainBytes;
    // 暗号化した1行("enc3:" + 16進 + 改行)。平文で置かれた行もこの長さまで読める
    constexpr size_t kLineCap = PICO_Secret::kPrefixLen + kChunkBytes * 2 + 4;

    void ChunkPurpose(const char* purpose, int i, char* out, size_t cap){
        snprintf(out, cap, "%s-%d", purpose, i);
    }
}

bool PICO_SecretFile::Load(const char* path, const char* purpose, char* out, size_t cap, bool* needs_rewrite){
    if(needs_rewrite) *needs_rewrite = false;
    if(cap == 0) return false;
    out[0] = '\0';
    if(!OSData::SD_usable) return false;
    FsFile f = OSData::SD.open(path, O_RDONLY);
    if(!f) return false;
    size_t len = 0;
    bool ok = true;
    char line[kLineCap];
    int i = 0;
    while(ok){
        const int n = f.fgets(line, sizeof(line));
        if(n <= 0) break;
        size_t ll = strlen(line);
        while(ll > 0 && (line[ll - 1] == '\n' || line[ll - 1] == '\r' || line[ll - 1] == ' ')) line[--ll] = '\0';
        if(ll == 0 || line[0] == '#') continue;
        if(needs_rewrite && PICO_Secret::NeedsUpgrade(line)) *needs_rewrite = true;
        char chunk_purpose[48];
        ChunkPurpose(purpose, i, chunk_purpose, sizeof(chunk_purpose));
        char plain[kLineCap];
        if(!PICO_Secret::Decrypt(chunk_purpose, line, plain, sizeof(plain))){ ok = false; break; }
        const size_t pl = strlen(plain);
        if(len + pl >= cap){ ok = false; break; }
        memcpy(out + len, plain, pl);
        len += pl;
        i++;
    }
    f.close();
    if(!ok){
        out[0] = '\0';
        return false;
    }
    out[len] = '\0';
    return len > 0;
}

bool PICO_SecretFile::Save(const char* path, const char* purpose, const char* header, const char* plain){
    if(!OSData::SD_usable || !plain) return false;
    const size_t n = strlen(plain);
    if(n == 0) return false;
    char tmp[PICO_PATH_LEN];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FsFile f = OSData::SD.open(tmp, O_WRONLY | O_CREAT | O_TRUNC);
    if(!f) return false;
    bool ok = true;
    if(header){
        const size_t hl = strlen(header);
        ok = f.write(header, hl) == hl && f.write("\n", 1) == 1;
    }
    for(size_t off = 0, i = 0; ok && off < n; off += kChunkBytes, i++){
        char chunk[kChunkBytes + 1];
        const size_t m = (n - off) < kChunkBytes ? (n - off) : kChunkBytes;
        memcpy(chunk, plain + off, m);
        chunk[m] = '\0';
        char chunk_purpose[48];
        ChunkPurpose(purpose, (int)i, chunk_purpose, sizeof(chunk_purpose));
        char line[kLineCap];
        if(!PICO_Secret::Encrypt(chunk_purpose, chunk, line, sizeof(line) - 1)){ ok = false; break; }
        const size_t ll = strlen(line);
        line[ll] = '\n';
        ok = f.write(line, ll + 1) == ll + 1;
    }
    f.close();
    if(!ok){
        OSData::SD.remove(tmp);
        return false;
    }
    if(OSData::SD.exists(path)) OSData::SD.remove(path);
    return OSData::SD.rename(tmp, path);
}

void PICO_SecretFile::Remove(const char* path){
    if(!OSData::SD_usable) return;
    if(OSData::SD.exists(path)) OSData::SD.remove(path);
}
