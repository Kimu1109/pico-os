// 2.5Dの箱庭: ワールドを開く・作る・保存する・前の版から移す

#include "iso/Iso_World.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <new>

#include <Arduino.h>
#include <SdFat.h>

#include "OS_Data.hpp"
#include "system/Log_Functions.hpp"
#include "iso/Iso_Internal.hpp"

namespace Iso {

using namespace Detail;

// ===================================================================
// 開く・作る・保存する
// ===================================================================

bool World::begin(const char* dir, uint8_t kind, int k, uint32_t seed) {
    close();
    if (k < 1 || k > kMaxK) return false;
    // チャンクの置き場は読み込むときに1個ずつ確保する(allocSlot)。以前は kMaxChunks 個(約75KB)を1回で取っていて、
    // 実機でゾンビTDの画像を読んだ後に連続した空きが無く「メモリが足りません」になった
    kind_ = kind;
    if (lighting()) {
        scratch_ = static_cast<uint8_t*>(malloc(kScratchBytes));
        if (!scratch_) {
            LOG_APP_WARN("ブロック: 明るさの作業場所(%u バイト)を確保できません", (unsigned)kScratchBytes);
            kind_ = EMPTY;
            return false;
        }
    }
    open_ = true;
    ld_valid_ = false;
    memset(map_, 0, sizeof(map_));
    memset(saved_, 0, sizeof(saved_));
    dir_.assign(dir ? dir : "");
    K_ = k;
    W_ = k * 8;
    seed_ = seed;
    qlen_ = qpos_ = 0;
    wa0_ = wb0_ = 1; wa1_ = wb1_ = 0;
    window_stale_ = true;
    warned_full_ = false;
    warned_compact_ = false;
    return true;
}

void World::close() {
    for (int i = 0; i < kMaxChunks; i++) {
        free(slots_[i]);
        slots_[i] = nullptr;
    }
    allocated_ = 0;
    open_ = false;
    free(scratch_);
    scratch_ = nullptr;
    ld_valid_ = false;
    memset(map_, 0, sizeof(map_));
    // 人や物はそのワールドの中の位置なので、閉じたら片付ける(描き直しは呼び出し側。ハンドルは使い回さない)
    freeEntities();
    delete[] shots_;
    shots_ = nullptr;
    keep_all_ = false;
    dir_.assign("");
    kind_ = EMPTY;
    K_ = 6;
    W_ = 48;
    qlen_ = qpos_ = 0;
}

bool World::create(const char* dir, uint8_t kind, uint32_t seed, int k, int& px, int& py, int& pz) {
    if (kind > ARENA || !begin(dir, kind, k, seed)) return false;
    if (kind == DEMO) { px = 0; py = 1; pz = 0; return true; }
    if (kind == ARENA) {
        arenaLayout(px, pz, nullptr, nullptr);
        py = arenaHeight(px, pz) + 1;
        return true;
    }
    px = pz = W_ / 2;
    py = firstAir(px, pz);
    return true;
}

bool World::Info(const char* path, int& kind, int& width) {
    uint8_t h[kHead];
    if (ReadFile(path, 0, h, kHead) != kHead || memcmp(h, kMagic, 4) != 0) return false;
    kind = h[4];
    width = R16(h + 5) * 8;
    return true;
}

bool World::open(const char* dir, int& px, int& py, int& pz, int& cur, const char*& err) {
    FixedString<PICO_PATH_LEN> path(dir);
    path.append("/world.dat");
    uint8_t h[kHead];
    if (ReadFile(path.c_str(), 0, h, kHead) != kHead || memcmp(h, kMagic, 4) != 0) {
        err = "形式が違います";
        return false;
    }
    const int kind = h[4], kk = R16(h + 5);
    if (kind > ARENA || kk < 1) { err = "壊れています"; return false; }
    if (kk > kMaxK) { err = "大きすぎます"; return false; }
    const uint32_t seed = ((uint32_t)h[7] << 24) | ((uint32_t)h[8] << 16) | ((uint32_t)h[9] << 8) | h[10];
    const int x = R16(h + 11), y = h[13], z = R16(h + 14);
    int c = h[16];
    const int bits = (kk * kk + 7) / 8;
    // 印はスタックに置かず(実機の1コア目のスタックは小さい)、開いてから直接読む。読めなければ閉じる
    if (!begin(dir, (uint8_t)kind, kk, seed)) { err = "メモリが足りません"; return false; }
    if (ReadFile(path.c_str(), kHead, saved_, (size_t)bits) != bits) {
        close();
        err = "途中で切れています";
        return false;
    }
    if (c < 1 || c > kBlockCount) c = STONE;
    px = imin(x, W_ - 1);
    py = imin(y, H - 1);
    pz = imin(z, W_ - 1);
    cur = c;
    return true;
}

bool World::save(int px, int py, int pz, int cur) {
    if (!open_ || dir_.length() == 0) return false;
    bool ok = true;
    for (int i = 0; i < kMaxChunks; i++) {
        Chunk* c = slots_[i];
        if (c && c->cx >= 0 && c->dirty && !writeChunk(c)) ok = false;
    }
    const int bits = (K_ * K_ + 7) / 8;
    uint8_t h[kHead];
    memcpy(h, kMagic, 4);
    h[4] = kind_;
    h[5] = (uint8_t)(K_ >> 8); h[6] = (uint8_t)K_;
    h[7] = (uint8_t)(seed_ >> 24); h[8] = (uint8_t)(seed_ >> 16); h[9] = (uint8_t)(seed_ >> 8); h[10] = (uint8_t)seed_;
    h[11] = (uint8_t)(px >> 8); h[12] = (uint8_t)px;
    h[13] = (uint8_t)py;
    h[14] = (uint8_t)(pz >> 8); h[15] = (uint8_t)pz;
    h[16] = (uint8_t)cur;
    FixedString<PICO_PATH_LEN> path(dir_.c_str());
    path.append("/world.dat");
    FsFile f = OSData::SD.open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
    if (!f) return false;
    if (f.write(h, kHead) != (size_t)kHead || f.write(saved_, (size_t)bits) != (size_t)bits) ok = false;
    f.close();
    return ok;
}

bool World::migrate(const char* old_path, const char* dir, const char*& err) {
    uint8_t head[10];
    if (ReadFile(old_path, 0, head, 10) != 10 || memcmp(head, "BLK1", 4) != 0) { err = "形式が違います"; return false; }
    if (head[4] != 48 || head[5] != H) { err = "大きさが違います"; return false; }
    const int px = head[6], py = head[7], pz = head[8], cur = head[9];
    if (!begin(dir, EMPTY, 6, 0)) { err = "メモリが足りません"; return false; }
    // 前の版は高さごとに (x, z) が x*48+z バイト目。1段ずつ読んで 6x6 のチャンクへ配る
    constexpr int kOldLayer = 48 * 48;
    uint8_t* layer = static_cast<uint8_t*>(malloc(kOldLayer));
    if (!layer) { close(); err = "メモリが足りません"; return false; }
    Chunk* cs[36];
    for (int i = 0; i < 36; i++) {
        cs[i] = allocSlot(i / 6, i % 6);
        memset(cs[i]->b, 0, kChunkBytes);
    }
    bool ok = true;
    for (int y = 0; y < H && ok; y++) {
        if (ReadFile(old_path, 10 + (uint32_t)y * kOldLayer, layer, kOldLayer) != kOldLayer) { ok = false; break; }
        for (int i = 0; i < kOldLayer; i++) if (layer[i] > kBlockCount) { ok = false; break; }
        for (int x = 0; x < 48 && ok; x++) {
            for (int cz = 0; cz < 6; cz++) {
                memcpy(cs[(x >> 3) * 6 + cz]->b + y * kLayer + (x & 7) * 8, layer + x * 48 + cz * 8, 8);
            }
        }
    }
    free(layer);
    if (!ok) { close(); err = "壊れています"; return false; }
    for (int i = 0; i < 36; i++) { Finish(cs[i]); cs[i]->dirty = true; }
    ok = save(px, py, pz, cur);
    close();
    if (!ok) { err = "書き出せません"; return false; }
    OSData::SD.remove(old_path);
    return true;
}

}  // namespace Iso
