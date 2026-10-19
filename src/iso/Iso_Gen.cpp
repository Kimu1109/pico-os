// 2.5Dの箱庭: 地形の生成(自然・平ら・デモ・ARENA)。Lua 版(旧 world.lua)と同じ種なら同じ地形

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

namespace {

// 両端を含む直方体を、チャンク(原点 ox, oz)の中身へ塗る(チャンクの外は切り捨てる)
void Paint(uint8_t* b, int ox, int oz, int x0, int y0, int z0, int x1, int y1, int z1, uint8_t v) {
    if (x0 < ox) x0 = ox;
    if (z0 < oz) z0 = oz;
    if (x1 > ox + 7) x1 = ox + 7;
    if (z1 > oz + 7) z1 = oz + 7;
    if (y0 < 0) y0 = 0;
    if (y1 > H - 1) y1 = H - 1;
    if (x0 > x1 || z0 > z1) return;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            memset(b + y * kLayer + (x - ox) * 8 + (z0 - oz), v, (size_t)(z1 - z0 + 1));
        }
    }
}

// (x, y, z) を根元にした木(葉 → 幹の順。後から塗ったものが勝つ)
void Tree(uint8_t* b, int ox, int oz, int x, int y, int z) {
    Paint(b, ox, oz, x - 2, y + 3, z - 2, x + 2, y + 4, z + 2, LEAVES);
    Paint(b, ox, oz, x - 1, y + 5, z - 1, x + 1, y + 5, z + 1, LEAVES);
    Paint(b, ox, oz, x + 1, y + 6, z, x + 1, y + 6, z, LEAVES);
    Paint(b, ox, oz, x - 1, y + 6, z, x - 1, y + 6, z, LEAVES);
    Paint(b, ox, oz, x, y + 6, z + 1, x, y + 6, z + 1, LEAVES);
    Paint(b, ox, oz, x, y + 6, z - 1, x, y + 6, z - 1, LEAVES);
    Paint(b, ox, oz, x, y + 6, z, x, y + 6, z, LEAVES);
    Paint(b, ox, oz, x, y, z, x, y + 5, z, WOOD);
}

// デモのワールドの家・東屋・ピラミッド・池・木(元の gen_demo と同じ。左下の 48x48 の中)
void Demo(uint8_t* b, int ox, int oz) {
    constexpr int N = 48;
    struct Op { uint8_t x0, y0, z0, x1, y1, z1, v; };
    // 先頭の2つ → 木 → 残り、の順に塗る(Lua 版の並びと同じ)
    static const Op kHead[] = {
        {32, 1, 0, N - 1, 4, 16, WATER},
        {1, 1, N - 2, 1, H - 1, N - 2, BRICKS},
    };
    static const uint8_t kTrees[][2] = {{8, 7}, {23, 15}, {4, 26}, {16, 23}, {28, 28}};
    static const Op kRest[] = {
        // 家
        {18, 0, 3, 24, 0, 9, PLANKS},
        {18, 1, 3, 24, 1, 9, WATER},
        {25, 1, 3, 25, 3, 9, BRICKS},
        {18, 1, 10, 24, 3, 10, BRICKS},
        {17, 1, 3, 17, 1, 9, SAND},
        {17, 1, 10, 17, 3, 10, SLABS},
        {25, 1, 10, 25, 3, 10, SLABS},
        {25, 1, 2, 25, 3, 2, SLABS},
        {18, 2, 3, 18, 2, 3, TNT},
        {19, 1, 3, 19, 1, 3, BOOKS},
        {24, 3, 5, 24, 3, 5, COBBLE},
        // 東屋
        {N - 4, 1, 3, N - 4, 3, 3, SLABS},
        {N - 10, 1, 3, N - 10, 3, 3, SLABS},
        {N - 4, 1, 9, N - 4, 3, 9, SLABS},
        {N - 10, 1, 9, N - 10, 3, 9, SLABS},
        {N - 10, 4, 3, N - 4, 4, 9, SLABS},
        {N - 9, 4, 4, N - 8, 4, 5, WATER},
        {N - 6, 4, 4, N - 5, 4, 5, WATER},
        {N - 9, 4, 7, N - 8, 4, 8, WATER},
        {N - 6, 4, 7, N - 5, 4, 8, WATER},
    };
    auto apply = [&](const Op& o) {
        if (o.x1 >= ox && o.x0 <= ox + 7 && o.z1 >= oz && o.z0 <= oz + 7) {
            Paint(b, ox, oz, o.x0, o.y0, o.z0, o.x1, o.y1, o.z1, o.v);
        }
    };
    for (const Op& o : kHead) apply(o);
    for (const auto& t : kTrees) Tree(b, ox, oz, t[0], 1, t[1]);
    for (const Op& o : kRest) apply(o);
    // ピラミッド
    for (int i = 1; i <= 8; i++) {
        apply(Op{(uint8_t)(N - 1 - i), (uint8_t)(9 - i), (uint8_t)(N - 1 - i), N - 1, (uint8_t)(9 - i), N - 1, GOLD});
    }
}

}  // namespace

// ===================================================================
// 地形の生成
// ===================================================================

uint32_t World::hash(int32_t a, int32_t b, int32_t c) const {
    // Lua 版と同じ(64bitの整数で計算して下位32bitを取るのと、32bitで溢れさせるのは同じ値)
    uint32_t h = ((uint32_t)a * 73856093u) ^ ((uint32_t)b * 19349663u) ^ ((uint32_t)c * 83492791u) ^ seed_;
    h = (h ^ (h >> 15)) * 0x2c1b3c6du;
    h = (h ^ (h >> 12)) * 0x297a2d39u;
    return h ^ (h >> 15);
}

// 元と同じ: 8マスごとの格子に高さ(3〜12)を決め、間は双一次補間
int World::height(int x, int z) const {
    const int gx = x >> 3, gz = z >> 3, lx = x & 7, lz = z & 7;
    auto grid = [this](int a, int b) { return 3 + (int)(hash(a, b, 0) % 10); };
    return (grid(gx, gz) * (8 - lx) * (8 - lz) + grid(gx + 1, gz) * lx * (8 - lz)
          + grid(gx, gz + 1) * (8 - lx) * lz + grid(gx + 1, gz + 1) * lx * lz) / 64;
}

// ---- ARENA(タワーディフェンス用) ----

void World::arenaLayout(int& bx, int& bz, int* sx, int* sz) const {
    bx = W_ / 2;
    bz = 4;
    for (int i = 0; i < kArenaSpawns; i++) {
        if (sx) sx[i] = W_ * (i + 1) / (kArenaSpawns + 1);
        if (sz) sz[i] = W_ - 3;
    }
}

namespace {
// 平らにした所(中心の高さ hf、半径 r0)から、半径 r1 までで元の高さ h へつなぐ
int Blend(int h, int hf, int d, int r0, int r1) {
    if (d <= r0) return hf;
    if (d >= r1) return h;
    return hf + ((h - hf) * (d - r0) * 2 + (r1 - r0)) / (2 * (r1 - r0));   // 四捨五入
}
int Cheb(int ax, int az, int bx, int bz) {
    const int dx = abs(ax - bx), dz = abs(az - bz);
    return dx > dz ? dx : dz;
}
}  // namespace

int World::arenaHeight(int x, int z) const {
    // 8マスごとの格子に高さ(4〜9)を決め、間は双一次補間(自然より起伏が小さい。1歩の段差は1段まで)
    auto raw = [this](int x, int z) {
        const int gx = x >> 3, gz = z >> 3, lx = x & 7, lz = z & 7;
        auto grid = [this](int a, int b) { return 4 + (int)(hash(a, b, 11) % 6); };
        return (grid(gx, gz) * (8 - lx) * (8 - lz) + grid(gx + 1, gz) * lx * (8 - lz)
              + grid(gx, gz + 1) * (8 - lx) * lz + grid(gx + 1, gz + 1) * lx * lz) / 64;
    };
    int h = raw(x, z);
    int bx, bz, sx[kArenaSpawns], sz[kArenaSpawns];
    arenaLayout(bx, bz, sx, sz);
    for (int i = 0; i < kArenaSpawns; i++) {
        const int hf = raw(sx[i], sz[i]) > kWaterLevel + 1 ? raw(sx[i], sz[i]) : kWaterLevel + 1;
        h = Blend(h, hf, Cheb(x, z, sx[i], sz[i]), 1, 4);
    }
    const int hb = raw(bx, bz) > kWaterLevel + 1 ? raw(bx, bz) : kWaterLevel + 1;
    return Blend(h, hb, Cheb(x, z, bx, bz), 3, 8);
}

void World::generate(int cx, int cz, uint8_t* b) const {
    memset(b, 0, kChunkBytes);
    const int ox = cx * 8, oz = cz * 8;
    if (kind_ == EMPTY) return;
    if (kind_ == ARENA) {
        // 水面(5)より低い所は水(底は砂)。ベースの平らな所は丸石、出現位置は砂利
        int bx, bz, sx[kArenaSpawns], sz[kArenaSpawns];
        arenaLayout(bx, bz, sx, sz);
        for (int lx = 0; lx < 8; lx++) {
            for (int lz = 0; lz < 8; lz++) {
                const int x = ox + lx, z = oz + lz, i = lx * 8 + lz;
                const int h = arenaHeight(x, z);
                uint8_t top = h < kWaterLevel ? SAND : GRASS;
                if (Cheb(x, z, bx, bz) <= 3) top = COBBLE;
                for (int k = 0; k < kArenaSpawns; k++) if (Cheb(x, z, sx[k], sz[k]) <= 1) top = GRAVEL;
                for (int y = 0; y < H; y++) {
                    uint8_t v = AIR;
                    if (y == 0) v = BEDROCK;
                    else if (y < h - 2) v = STONE;
                    else if (y < h) v = DIRT;
                    else if (y == h) v = top;
                    else if (y <= kWaterLevel) v = WATER;
                    b[y * kLayer + i] = v;
                }
            }
        }
        return;
    }
    if (kind_ == FLAT || (kind_ == DEMO && (cx >= 6 || cz >= 6))) {
        memset(b, GRASS, kLayer);
        return;
    }
    if (kind_ == DEMO) {
        memset(b, GRASS, kLayer);
        Demo(b, ox, oz);
        return;
    }

    // 自然: 水面(5)より低い所は水、水辺は砂、石の1割は石炭/鉄鉱石、チャンク3つに1本くらい木
    // 格子の高さはチャンクのまわりの 4x4 個(cx-1〜cx+2)だけなので先に引いておき、補間は Lua 版と同じ式で
    int grid[4][4];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) grid[i][j] = 3 + (int)(hash(cx - 1 + i, cz - 1 + j, 0) % 10);
    }
    // まわり2マスまでの高さ(砂の判定に使う)
    uint8_t hh[12][12];
    for (int x = -2; x <= 9; x++) {
        for (int z = -2; z <= 9; z++) {
            const int wx = ox + x, wz = oz + z;
            const int gi = (wx >> 3) - cx + 1, gj = (wz >> 3) - cz + 1;  // 0〜2
            const int lx = wx & 7, lz = wz & 7;
            hh[x + 2][z + 2] = (uint8_t)((grid[gi][gj] * (8 - lx) * (8 - lz) + grid[gi + 1][gj] * lx * (8 - lz)
                                + grid[gi][gj + 1] * (8 - lx) * lz + grid[gi + 1][gj + 1] * lx * lz) / 64);
        }
    }
    uint8_t hs[kLayer], sd[kLayer];
    int top = kWaterLevel;
    for (int lx = 0; lx < 8; lx++) {
        for (int lz = 0; lz < 8; lz++) {
            const int h = hh[lx + 2][lz + 2];
            uint8_t s = 0;
            if (h == kWaterLevel - 1) {
                s = 1;
            } else if (h == kWaterLevel) {
                for (int bx = lx; bx <= lx + 4 && !s; bx++) {
                    for (int bz = lz; bz <= lz + 4; bz++) {
                        if (hh[bx][bz] < kWaterLevel) { s = 1; break; }
                    }
                }
            }
            const int i = lx * 8 + lz;
            hs[i] = (uint8_t)h;
            sd[i] = s;
            if (h > top) top = h;
        }
    }
    for (int y = 0; y <= top && y < H; y++) {
        uint8_t* row = b + y * kLayer;
        for (int lx = 0; lx < 8; lx++) {
            for (int lz = 0; lz < 8; lz++) {
                const int i = lx * 8 + lz;
                const int h = hs[i];
                uint8_t v;
                if (y == 0) v = BEDROCK;
                else if (y > h) v = (y <= kWaterLevel) ? WATER : AIR;
                else if (y == h && sd[i]) v = SAND;
                else if (y == h && h >= kWaterLevel) v = GRASS;
                else if (h > 3 && y <= h - 3) {
                    const uint32_t r = hash(ox + lx, y, oz + lz) % 20;
                    v = (r == 0) ? COAL_ORE : (r == 1) ? IRON_ORE : STONE;
                } else v = DIRT;
                row[i] = v;
            }
        }
    }
    // 木: 葉が隣のチャンクへはみ出さないよう、根元はチャンクの中の 2〜5
    const uint32_t r = hash(cx, cz, 7);
    if (r % 3 == 0) {
        const int lx = 2 + (int)((r >> 4) % 4), lz = 2 + (int)((r >> 8) % 4);
        const int i = lx * 8 + lz;
        const int h = hs[i];
        if (sd[i] == 0 && h >= kWaterLevel && h <= H - 8) {
            Tree(b, ox, oz, ox + lx, h + 1, oz + lz);
            Paint(b, ox, oz, ox + lx, h, oz + lz, ox + lx, h, oz + lz, DIRT);
        }
    }
}

}  // namespace Iso
