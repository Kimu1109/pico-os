// 2.5Dボクセルの箱庭のエンジン。説明は Iso_World.hpp。
// 地形の生成・描く順・影・引き当ては Lua 版(pc/sdcard/lua/apps/ブロック/ の旧 view.lua / world.lua)と
// 同じ答えになるように移してある(同じ種なら同じ地形。既存のセーブの「書き換えていないチャンク」を作り直すため)。

#include "iso/Iso_World.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <new>

#include <Arduino.h>
#include <SdFat.h>

#include "OS_Data.hpp"
#include "functions/Log_Functions.hpp"

namespace Iso {

namespace {

constexpr char kMagic[4] = {'B', 'L', 'K', '2'};
constexpr int kHead = 17;

// 床のある割り算(Lua の //)。b > 0
inline int fdiv(int a, int b) {
    const int q = a / b;
    return (a % b != 0 && a < 0) ? q - 1 : q;
}

inline int imax(int a, int b) { return a > b ? a : b; }
inline int imin(int a, int b) { return a < b ? a : b; }
inline int iround(float v) { return (int)floorf(v + 0.5f); }
constexpr float kEps = 0.01f;   // 人や物の箱が面で接しているのを「離れている」とみなす余裕(小数の誤差)

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

// 絵(32x31)の中の点 (lx, ly) がどの面か
Face FaceAt(int lx, int ly) {
    if (lx < 0 || lx > 31 || ly < 0 || ly > 30) return Face::None;
    if (ly <= 14) {
        const int half = (ly <= 7) ? 2 * (ly + 1) : 2 * (15 - ly);
        if (lx >= 16 - half && lx < 16 + half) return Face::Top;
    }
    if (lx < 16) {
        const int i = lx / 2;
        if (ly >= 8 + i && ly <= 23 + i) return Face::Left;
    } else {
        const int i = (lx - 16) / 2;
        if (ly >= 15 - i && ly <= 30 - i) return Face::Right;
    }
    return Face::None;
}

bool WriteFile(const char* path, const void* data, size_t len) {
    FsFile f = OSData::SD.open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (!f) return false;
    const bool ok = (len == 0) || (f.write(data, len) == len);
    f.close();
    return ok;
}

// ファイルの offset バイト目から最大 len バイト。読めたバイト数(開けなければ -1)
int ReadFile(const char* path, uint32_t offset, void* out, size_t len) {
    FsFile f = OSData::SD.open(path, O_RDONLY);
    if (!f) return -1;
    if (offset > 0 && !f.seek(offset)) { f.close(); return 0; }
    size_t got = 0;
    uint8_t* p = static_cast<uint8_t*>(out);
    while (got < len) {
        const int n = f.read(p + got, len - got);
        if (n <= 0) break;
        got += (size_t)n;
    }
    f.close();
    return (int)got;
}

inline uint16_t R16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }

}  // namespace

World::~World() { close(); }

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

// ===================================================================
// チャンクの置き場
// ===================================================================

void World::CountTorches(Chunk* c) {
    int n = 0;
    for (int i = 0; i < c->top * kLayer; i++) n += BlockOf(c, i) == TORCH;
    c->torches = (uint8_t)(n > 255 ? 255 : n);
}

// 1チャンクぶんのブロック(kChunkBytes)を compact の形にする。表せないマスがあれば false(表せる所までは入る)
bool World::Compress(Chunk* c, const uint8_t* full) {
    bool ok = true;
    for (int i = 0; i < kLayer; i++) {
        // 地面 = 空気と水でない一番上(その上の1個は、すぐ上が空気・水でなければ「その上」として持つ)
        int h = 0;
        for (int y = H - 1; y >= 0; y--) {
            const uint8_t v = full[y * kLayer + i];
            if (v != AIR && v != WATER) { h = y; break; }
        }
        uint8_t extra = 0;
        if (h >= 1 && h + 1 < H) {
            // 一番上が地面の上に置いたもの(下が地面の色)なら、1段下を地面とする
            const uint8_t below = full[(h - 1) * kLayer + i];
            if (below != DIRT && below != STONE && below != BEDROCK && below != AIR && below != WATER) {
                extra = full[h * kLayer + i];
                h--;
            }
        }
        c->b[i] = (uint8_t)h;
        c->b[kLayer + i] = full[h * kLayer + i];
        c->b[2 * kLayer + i] = extra;
        for (int y = 0; y < H && ok; y++) {
            if (CompactGet(c, y, i) != full[y * kLayer + i]) ok = false;
        }
    }
    return ok;
}

void World::Finish(Chunk* c) {
    int top = 0;
    for (int i = 0; i < kLayer; i++) {
        int t = 0;
        for (int y = H - 1; y >= 0; y--) {
            if (BlockOf(c, y * kLayer + i)) { t = y + 1; break; }
        }
        c->col[i] = (uint8_t)t;
        if (t > top) top = t;
    }
    c->top = (uint8_t)top;
    CountTorches(c);
}

void World::chunkPath(int cx, int cz, FixedString<PICO_PATH_LEN>& out) const {
    char name[32];
    snprintf(name, sizeof(name), "/c_%d_%d.dat", cx, cz);
    out.assign(dir_.c_str());
    out.append(name);
}

bool World::readChunkFile(int cx, int cz, uint8_t* out) {
    FixedString<PICO_PATH_LEN> path;
    chunkPath(cx, cz, path);
    FsFile f = OSData::SD.open(path.c_str(), O_RDONLY);
    if (!f) return false;
    const size_t size = f.size();
    bool ok = (size % kLayer == 0) && size <= (size_t)kChunkBytes;
    if (ok) {
        memset(out, 0, kChunkBytes);
        size_t got = 0;
        while (got < size) {
            const int n = f.read(out + got, size - got);
            if (n <= 0) break;
            got += (size_t)n;
        }
        ok = got == size;
        for (size_t i = 0; ok && i < size; i++) {
            if (out[i] > kBlockCount) ok = false;
        }
    }
    f.close();
    return ok;
}

bool World::writeChunk(Chunk* c) {
    if (dir_.length() == 0) return false;
    FixedString<PICO_PATH_LEN> path;
    chunkPath(c->cx, c->cz, path);
    const uint8_t* src = c->b;
    if (c->compact) {
        for (int i = 0; i < c->top * kLayer; i++) gen_buf_[i] = BlockOf(c, i);
        src = gen_buf_;
    }
    if (!WriteFile(path.c_str(), src, (size_t)c->top * kLayer)) return false;
    c->dirty = false;
    markSaved(c->cx * K_ + c->cz);
    return true;
}

void World::unload(Chunk* c) {
    if (c->dirty) {
        if (!writeChunk(c)) LOG_APP_WARN("ブロック: チャンク %d,%d を書き出せません", c->cx, c->cz);
    }
    uint8_t& m = map_[((c->cx & 31) << 5) | (c->cz & 31)];
    if (m && slots_[m - 1] == c) m = 0;
    c->cx = c->cz = -1;
    c->dirty = false;
}

bool World::inWindow(int cx, int cz, int margin) const {
    const int a = cx - cz, b = cx + cz;
    return a >= wa0_ - margin && a <= wa1_ + margin && b >= wb0_ - margin && b <= wb1_ + margin;
}

// (cx, cz) のための置き場を空ける。空きが無ければ、読み込む範囲から一番遠いチャンクを手放す
Chunk* World::allocSlot(int cx, int cz) {
    // 同じ map の升を使っている別のチャンク(32チャンク以上離れている)は先に手放す
    const int mi = ((cx & 31) << 5) | (cz & 31);
    if (map_[mi]) unload(slots_[map_[mi] - 1]);
    Chunk* best = nullptr;
    int best_i = -1, best_d = -1;
    const int ac2 = wa0_ + wa1_, bc2 = wb0_ + wb1_;
    for (int i = 0; i < kMaxChunks; i++) {
        Chunk* c = slots_[i];
        if (!c) {
            // まだ確保していない置き場: 1個ずつ確保する(大きな連続した空きが無くても入るように)
            c = static_cast<Chunk*>(malloc(slotBytes()));
            if (!c) break;   // 確保できなければ、今ある中から手放せるものを使う
            c->cx = c->cz = -1;
            c->dirty = false;
            c->no_light = !lighting();
            c->compact = compactChunks();
            slots_[i] = c;
            allocated_++;
        }
        if (c->cx < 0) { best = c; best_i = i; best_d = -2; break; }
        if (inWindow(c->cx, c->cz, 0)) continue;
        const int a = c->cx - c->cz, b = c->cx + c->cz;
        const int d = abs(2 * a - ac2) + abs(2 * b - bc2);
        if (d > best_d) { best = c; best_i = i; best_d = d; }
    }
    if (!best) {
        if (!warned_full_) {
            LOG_APP_WARN("ブロック: チャンクの置き場(%d個、確保できたのは%d個)が足りません", kMaxChunks, allocated_);
            warned_full_ = true;
        }
        return nullptr;
    }
    if (best->cx >= 0) unload(best);
    best->cx = (int16_t)cx;
    best->cz = (int16_t)cz;
    best->dirty = false;
    best->torches = 0;
    if (!best->no_light) memset(best->light, 0, sizeof(best->light));
    map_[mi] = (uint8_t)(best_i + 1);
    return best;
}

Chunk* World::loadChunk(int cx, int cz) {
    if (!open_ || cx < 0 || cz < 0 || cx >= K_ || cz >= K_) return nullptr;
    Chunk* c = findMut(cx, cz);
    if (c) return c;
    c = allocSlot(cx, cz);
    if (!c) return nullptr;
    bool from_file = false;
    uint8_t* out = c->compact ? gen_buf_ : c->b;
    if (dir_.length() && isSaved(cx * K_ + cz)) {
        from_file = readChunkFile(cx, cz, out);
        if (!from_file) LOG_APP_WARN("ブロック: チャンク %d,%d を読めません。作り直します", cx, cz);
    }
    if (!from_file) generate(cx, cz, out);
    if (c->compact && !Compress(c, out)) {
        if (!warned_compact_) {
            LOG_APP_WARN("ブロック: チャンク %d,%d は柱ごとの形で持てません(違うブロックは消えます)", cx, cz);
            warned_compact_ = true;
        }
    }
    Finish(c);
    // 自分の松明はまわりを、まわりの松明は自分を照らす
    relightAround(cx, cz, false);
    return c;
}

// ===================================================================
// 松明の光
// ===================================================================

void World::setOccluders(uint32_t mask) {
    if (mask == occluders_) return;
    occluders_ = mask;
    relightAll();   // 光を通すブロックが変わる
}

void World::relightAll() {
    if (!open_) return;
    for (int i = 0; i < kMaxChunks; i++) {
        if (slots_[i] && slots_[i]->cx >= 0) relight(slots_[i]->cx, slots_[i]->cz, false);
    }
}

bool World::torchNear(int x, int y, int z) const {
    constexpr int R = kLightReach;
    for (int ccx = (x - R) >> 3; ccx <= (x + R) >> 3; ccx++) {
        for (int ccz = (z - R) >> 3; ccz <= (z + R) >> 3; ccz++) {
            const Chunk* n = find(ccx, ccz);
            if (!n || !n->torches) continue;
            for (int i = 0; i < n->top * kLayer; i++) {
                if (n->b[i] != TORCH) continue;
                const int d = abs(n->cx * 8 + ((i & 63) >> 3) - x) + abs(n->cz * 8 + (i & 7) - z) + abs((i >> 6) - y);
                if (d <= R) return true;
            }
        }
    }
    return false;
}

void World::relightAround(int cx, int cz, bool track) {
    for (int dx = -1; dx <= 1; dx++) {
        for (int dz = -1; dz <= 1; dz++) {
            if (find(cx + dx, cz + dz)) relight(cx + dx, cz + dz, track);
        }
    }
}

// チャンクのまわり kLightReach マス(作業場所 kLightSpan 四方)の中で光を広げる。松明から kLightReach 歩以内の道は
// 全部この中に収まるので、チャンクの中のマスの明るさはこれで正しく決まる。読み込んでいないチャンクと世界の外は
// 光を通さないものとして扱う(読み込んだときに、まわりと一緒に計算し直す)
void World::relight(int cx, int cz, bool track) {
    Chunk* c = findMut(cx, cz);
    if (!c || c->no_light || !scratch_) return;
    constexpr int S = kLightSpan, R = kLightReach, kWall = 0xFF;
    const int ex0 = cx * 8 - R, ez0 = cz * 8 - R;
    // 作業場所の中にある松明の範囲(作業場所の座標)。まわり3x3の松明のあるチャンクだけを見る
    int tx0 = S, tx1 = -1, ty0 = H, ty1 = -1, tz0 = S, tz1 = -1;
    for (int dx = -1; dx <= 1; dx++) {
        for (int dz = -1; dz <= 1; dz++) {
            const Chunk* n = find(cx + dx, cz + dz);
            if (!n || !n->torches) continue;
            for (int i = 0; i < n->top * kLayer; i++) {
                if (n->b[i] != TORCH) continue;
                const int ex = n->cx * 8 + ((i & 63) >> 3) - ex0, ez = n->cz * 8 + (i & 7) - ez0, y = i >> 6;
                if (ex < 0 || ex >= S || ez < 0 || ez >= S) continue;
                tx0 = imin(tx0, ex); tx1 = imax(tx1, ex);
                tz0 = imin(tz0, ez); tz1 = imax(tz1, ez);
                ty0 = imin(ty0, y); ty1 = imax(ty1, y);
            }
        }
    }
    uint8_t* sc = scratch_;
    const bool any = tx1 >= 0 && sc;
    // 光が届きうる範囲 = 松明の範囲 + kLightReach(作業場所の中だけ)
    int bx0 = 0, bx1 = -1, by0 = 0, by1 = -1, bz0 = 0, bz1 = -1;
    if (any) {
        bx0 = imax(tx0 - R, 0); bx1 = imin(tx1 + R, S - 1);
        bz0 = imax(tz0 - R, 0); bz1 = imin(tz1 + R, S - 1);
        by0 = imax(ty0 - R, 0); by1 = imin(ty1 + R, H - 1);
        // チャンクにかからなければ、このチャンクは暗い
        if (bx1 < R || bx0 > R + 7 || bz1 < R || bz0 > R + 7) bx1 = -1;
    }
    if (bx1 < 0) {
        // 松明の光が届かない: 暗くするだけ(もともと真っ暗なら何もしない)
        bool lit = false;
        for (size_t i = 0; i < sizeof(c->light); i++) if (c->light[i]) { lit = true; break; }
        if (!lit) return;
    } else {
        for (int ex = bx0; ex <= bx1; ex++) {
            for (int ez = bz0; ez <= bz1; ez++) {
                const int wx = ex0 + ex, wz = ez0 + ez;
                const Chunk* n = (wx < 0 || wz < 0 || wx >= W_ || wz >= W_) ? nullptr : find(wx >> 3, wz >> 3);
                const int ci = (wx & 7) * 8 + (wz & 7);
                for (int y = by0; y <= by1; y++) {
                    uint8_t v = kWall;
                    if (n) {
                        const uint8_t b = n->b[y * kLayer + ci];
                        v = blocksLight(b) ? kWall : (b == TORCH ? kTorchLight : 0);
                    }
                    sc[(y * S + ex) * S + ez] = v;
                }
            }
        }
        // 明るさ l のマスから、まだ l-1 より暗い隣へ l-1 を広げる(l の大きい順に1周ずつ。範囲の外へは出ない)
        for (int l = kTorchLight; l >= 2; l--) {
            const uint8_t nl = (uint8_t)(l - 1);
            for (int y = by0; y <= by1; y++) {
                for (int ex = bx0; ex <= bx1; ex++) {
                    uint8_t* row = sc + (y * S + ex) * S;
                    for (int ez = bz0; ez <= bz1; ez++) {
                        if (row[ez] != l) continue;
                        auto spread = [&](uint8_t& t) { if (t != kWall && t < nl) t = nl; };
                        if (ez > bz0) spread(row[ez - 1]);
                        if (ez < bz1) spread(row[ez + 1]);
                        if (ex > bx0) spread(row[ez - S]);
                        if (ex < bx1) spread(row[ez + S]);
                        if (y > by0) spread(row[ez - S * S]);
                        if (y < by1) spread(row[ez + S * S]);
                    }
                }
            }
        }
    }
    for (int y = 0; y < H; y++) {
        for (int lx = 0; lx < 8; lx++) {
            for (int lz = 0; lz < 8; lz++) {
                int q = 0;
                const int ex = lx + R, ez = lz + R;
                if (ex >= bx0 && ex <= bx1 && ez >= bz0 && ez <= bz1 && y >= by0 && y <= by1) {
                    const int l = sc[(y * S + ex) * S + ez];
                    q = (l == kWall) ? 0 : (l >= 5 ? 3 : (l >= 3 ? 2 : (l >= 1 ? 1 : 0)));
                }
                const int i = y * kLayer + lx * 8 + lz;
                if (LightOf(c, i) == q) continue;
                uint8_t& d = c->light[i >> 2];
                const int sh = (i & 3) * 2;
                d = (uint8_t)((d & ~(3 << sh)) | (q << sh));
                if (!track) continue;
                const int wx = cx * 8 + lx, wz = cz * 8 + lz;
                if (!ld_valid_) {
                    ld_[0] = ld_[3] = wx; ld_[1] = ld_[4] = y; ld_[2] = ld_[5] = wz;
                    ld_valid_ = true;
                } else {
                    ld_[0] = imin(ld_[0], wx); ld_[1] = imin(ld_[1], y); ld_[2] = imin(ld_[2], wz);
                    ld_[3] = imax(ld_[3], wx); ld_[4] = imax(ld_[4], y); ld_[5] = imax(ld_[5], wz);
                }
            }
        }
    }
}

int World::loadedCount() const {
    if (!open_) return 0;
    int n = 0;
    for (int i = 0; i < kMaxChunks; i++) n += slots_[i] && slots_[i]->cx >= 0;
    return n;
}

int World::topAll() const {
    if (!open_) return -1;
    int t = 0;
    for (int i = 0; i < kMaxChunks; i++) {
        if (slots_[i] && slots_[i]->cx >= 0 && slots_[i]->top > t) t = slots_[i]->top;
    }
    return t - 1;
}

void World::set(int x, int y, int z, uint8_t v) {
    if (x < 0 || z < 0 || y < 0 || x >= W_ || z >= W_ || y >= H || v > kBlockCount) return;
    Chunk* c = loadChunk(x >> 3, z >> 3);
    if (!c) return;
    const int i = (x & 7) * 8 + (z & 7);
    const uint8_t old = BlockOf(c, y * kLayer + i);
    if (old == v) return;
    if (c->compact) {
        // 持てるのは地面の上の1段と、地面の一番上の種類だけ
        const int h = c->b[i];
        if (y == h + 1) {
            c->b[2 * kLayer + i] = 0;
            if (CompactGet(c, y, i) != v) c->b[2 * kLayer + i] = v;
        } else if (y == h && v != AIR && v != WATER) {
            c->b[kLayer + i] = v;
        } else {
            if (!warned_compact_) {
                LOG_APP_WARN("ブロック: この地形では地面の上の1段しか書き換えられません(%d,%d,%d)", x, y, z);
                warned_compact_ = true;
            }
            return;
        }
    } else {
        c->b[y * kLayer + i] = v;
    }
    c->dirty = true;
    // その柱の高さと、チャンクの段の数だけ数え直す
    int t = 0;
    for (int yy = H - 1; yy >= 0; yy--) {
        if (BlockOf(c, yy * kLayer + i)) { t = yy + 1; break; }
    }
    c->col[i] = (uint8_t)t;
    if (t >= c->top) c->top = (uint8_t)t;
    else {
        int top = 0;
        for (int k = 0; k < kLayer; k++) if (c->col[k] > top) top = c->col[k];
        c->top = (uint8_t)top;
    }
    // 松明を置いた/取った、光を通すかが変わった: まわり kLightReach マスにかかるチャンクの明るさを計算し直す
    if (old == TORCH || v == TORCH) CountTorches(c);
    // (光を通すかだけが変わったときは、松明から kLightReach 歩以内のマスでなければ、どの道も変わらない)
    if (old == TORCH || v == TORCH || (blocksLight(old) != blocksLight(v) && torchNear(x, y, z))) {
        for (int ccx = (x - kLightReach) >> 3; ccx <= (x + kLightReach) >> 3; ccx++) {
            for (int ccz = (z - kLightReach) >> 3; ccz <= (z + kLightReach) >> 3; ccz++) relight(ccx, ccz, true);
        }
    }
}

int World::firstAir(int x, int z) {
    loadChunk(x >> 3, z >> 3);
    int y = 0;
    while (y < H - 1 && get(x, y, z) != AIR) y++;
    return y;
}

void World::window(int umin, int umax, int smin, int smax) {
    if (!open_) return;
    const int amin = -fdiv(7 - umin, 8), amax = fdiv(umax + 7, 8);
    const int bmin = -fdiv(14 - smin, 8), bmax = fdiv(smax, 8);
    wa0_ = amin; wa1_ = amax; wb0_ = bmin; wb1_ = bmax;
    // 範囲より1チャンクより外のチャンクは手放す(1チャンクぶんは残して、行き来で読み直さない)
    for (int i = 0; i < kMaxChunks; i++) {
        Chunk* c = slots_[i];
        if (c && c->cx >= 0 && !inWindow(c->cx, c->cz, 1)) unload(c);
    }
    // 読み込む予定: まだ読み込んでいないものを、範囲の真ん中から近い順に
    int32_t* dist = qdist_;
    qlen_ = qpos_ = 0;
    const int ac2 = amin + amax, bc2 = bmin + bmax;
    for (int b = bmin; b <= bmax; b++) {
        for (int a = amin; a <= amax; a++) {
            if (((a + b) & 1) != 0) continue;
            const int cx = (a + b) / 2, cz = (b - a) / 2;
            if (cx < 0 || cz < 0 || cx >= K_ || cz >= K_ || find(cx, cz)) continue;
            const int d = abs(2 * a - ac2) + abs(2 * b - bc2);
            if (qlen_ < kMaxQueue) {
                queue_[qlen_] = cx * K_ + cz;
                dist[qlen_] = d;
                qlen_++;
            } else {
                // 溢れたら一番遠いものと入れ替える(範囲がおかしく大きいときだけ)
                int worst = 0;
                for (int i = 1; i < qlen_; i++) if (dist[i] > dist[worst]) worst = i;
                if (d < dist[worst]) { queue_[worst] = cx * K_ + cz; dist[worst] = d; }
            }
        }
    }
    // 挿入ソート(高々数十個)
    for (int i = 1; i < qlen_; i++) {
        const int32_t q = queue_[i], d = dist[i];
        int j = i - 1;
        while (j >= 0 && dist[j] > d) { queue_[j + 1] = queue_[j]; dist[j + 1] = dist[j]; j--; }
        queue_[j + 1] = q;
        dist[j + 1] = d;
    }
}

int World::pump(int max, uint32_t ms) {
    if (!open_) return 0;
    if (window_stale_) {
        if (keep_all_) {
            // 世界全体(u = x - z は -(W-1)〜W-1、s = x + z は 0〜2W-2)
            window(-(W_ - 1), W_ - 1, 0, 2 * W_ - 2);
        } else {
            int umin, umax, smin, smax;
            loadRange(umin, umax, smin, smax);
            window(umin, umax, smin, smax);
        }
        window_stale_ = false;
    }
    const uint32_t t0 = millis();
    int done = 0;
    while (qpos_ < qlen_ && done < max) {
        const int n = queue_[qpos_++];
        const int cx = n / K_, cz = n % K_;
        if (find(cx, cz)) continue;
        loadChunk(cx, cz);
        done++;
        if (ms && millis() - t0 >= ms) break;
    }
    return done;
}

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

// ===================================================================
// 表示
// ===================================================================

void World::setView(int x, int y, int w, int h) {
    vx_ = x; vy_ = y; vw_ = w; vh_ = h;
    window_stale_ = true;
    refreshEntityRects();
}

void World::setOrigin(int ox, int oy) {
    if (ox == OX_ && oy == OY_) return;
    OX_ = ox; OY_ = oy;
    window_stale_ = true;
    refreshEntityRects();
}

void World::loadRange(int& umin, int& umax, int& smin, int& smax) const {
    // 表示範囲にかかる柱の範囲に、影をたどる分を足す(光は1歩ごとに u が2減る。高さ H-1 までなので左へ 2*(H-1))
    const int x0 = vx_, y0 = vy_, x1 = vx_ + vw_, y1 = vy_ + vh_;
    umin = fdiv(x0 - OX_ - 32, 16) + 1 - 2 * (H - 1);
    umax = -fdiv(OX_ - x1, 16) - 1;
    smin = fdiv(OY_ - 16 * (H - 1) - y1, 8) + 1;
    smax = -fdiv(y0 - OY_ - 31, 8) - 1;
}

// 影: 太陽は (-1, +1, +1) の向き。日の当たりうる上面と左面を、光から見た三角形2つに分け
// (上面は x+z が一定の線で奥/手前、左面は y=z の線で上/下)、三角形ごとに影を決める。
// 三角形の中の1点から太陽へ向かう直線は1歩(-1,+1,+1)ごとに3マスを通り、2つの三角形で2マスを共有するので、
// 1歩あたり4マスを見る。水と松明は影を作らない(Casts)。
//
// 上面: k歩目、高さ y+1+k で A=(x-k, z+k) 両方 / B=(x-k, z+1+k) 奥 / C=(x-1-k, z+k) 手前 / D=(x-1-k, z+1+k) 両方
namespace {
inline bool Casts(uint8_t b) { return b >= 2 && b != TORCH; }
}

void World::topShadow(int x, int y, int z, int top, bool& far, bool& near) const {
    far = near = false;
    for (int k = 0; y + 1 + k <= top; k++) {
        const int yy = y + 1 + k, x0 = x - k, x1 = x - 1 - k, z0 = z + k, z1 = z + 1 + k;
        if (x0 < 0 || z0 >= W_) break;
        if (Casts(get(x0, yy, z0)) || Casts(get(x1, yy, z1))) { far = near = true; return; }
        if (!far && Casts(get(x0, yy, z1))) far = true;
        if (!near && Casts(get(x1, yy, z0))) near = true;
        if (far && near) return;
    }
}

// 左面: k歩目、x-1-k で E=(y+k, z+k) 両方 / F=(y+1+k, z+k) 上 / G=(y+k, z+1+k) 下 / J=(y+1+k, z+1+k) 両方
void World::leftShadow(int x, int y, int z, int top, bool& up, bool& low) const {
    up = low = false;
    for (int k = 0;; k++) {
        const int xx = x - 1 - k, y0 = y + k, z0 = z + k;
        if (xx < 0 || y0 > top || z0 >= W_) break;
        if (Casts(get(xx, y0, z0))) { up = low = true; return; }
        if (!low && Casts(get(xx, y0, z0 + 1))) low = true;
        if (y0 + 1 <= top) {
            if (Casts(get(xx, y0 + 1, z0 + 1))) { up = low = true; return; }
            if (!up && Casts(get(xx, y0 + 1, z0))) up = true;
        }
        if (up && low) return;
    }
}

uint32_t World::ComputeOccluders(int (*px)(void* ctx, int x, int y), void* ctx) {
    uint32_t mask = 0;
    for (int id = 2; id <= kBlockCount; id++) {
        const int sy = (id - 1) * kRowH;
        bool full = true;
        for (int ly = 0; ly < 31 && full; ly++) {
            for (int lx = 0; lx < 32; lx++) {
                const Face f = FaceAt(lx, ly);
                if (f == Face::None) continue;
                // その画素を覆う面の絵のどれかが透過でなければよい
                bool hit = false;
                if (ly < 15 && px(ctx, lx, sy + ly)) hit = true;
                if (!hit && lx < 16 && ly >= 8 && px(ctx, 128 + lx, sy + ly - 8)) hit = true;
                if (!hit && lx >= 16 && ly >= 8 && px(ctx, 192 + lx - 16, sy + ly - 8)) hit = true;
                if (!hit) { full = false; break; }
            }
        }
        if (full) mask |= 1u << id;
    }
    return mask;
}

// 面を、日の光で決まった絵 sx に、松明の明るさ q(0〜3)のぶんだけ日なたの絵 lit を重ねて描く
void World::DrawLit(const Sink& sink, int sx, int lit, int q, int sy, int w, int h, int dx, int dy) {
    if (q >= 3 || sx == lit) {
        sink.draw(sink.ctx, q >= 3 ? lit : sx, sy, w, h, dx, dy);
        return;
    }
    sink.draw(sink.ctx, sx, sy, w, h, dx, dy);
    if (q <= 0) return;
    if (sink.dither) sink.dither(sink.ctx, lit, sy, w, h, dx, dy, q);
    else if (q >= 2) sink.draw(sink.ctx, lit, sy, w, h, dx, dy);
}

void World::DrawIcon(const Sink& sink, int id, int px, int py) {
    const int sy = (id - 1) * kRowH;
    sink.draw(sink.ctx, 0, sy, 32, 15, px, py);
    sink.draw(sink.ctx, 128, sy, 16, 23, px, py + 8);
    sink.draw(sink.ctx, 192, sy, 16, 23, px + 16, py + 8);
}

void World::render(const Sink& sink, int x0, int y0, int x1, int y1) {
    renderBlocks(sink, x0, y0, x1, y1, nullptr);
    if (sink.face_px && sink.image_px && sink.put) renderEntities(sink, x0, y0, x1, y1);
    if (sink.put) {
        renderShots(sink, x0, y0, x1, y1);
        renderOverlays(sink, x0, y0, x1, y1);
    }
    drawCursor(sink, x0, y0, x1, y1);
}

void World::drawCursor(const Sink& sink, int x0, int y0, int x1, int y1) const {
    if (!show_cursor_) return;
    int bx, by;
    blockPos(cx_, cy_, cz_, bx, by);
    if (bx < x1 && bx + 32 > x0 && by < y1 && by + 31 > y0) {
        const int sy = kCursorRow * kRowH;
        sink.draw(sink.ctx, 0, sy, 32, 15, bx, by);
        sink.draw(sink.ctx, 128, sy, 16, 23, bx, by + 8);
        sink.draw(sink.ctx, 192, sy, 16, 23, bx + 16, by + 8);
    }
}

void World::renderBlocks(const Sink& sink, int x0, int y0, int x1, int y1, const Box* front, int* cut) {
    if (!front) last_faces_ = 0;
    const int top = topAll();
    int faces = 0;
    if (open_ && top >= 0) {
        const int OX = OX_, OY = OY_, W = W_;
        const uint32_t occ = occluders_;
        const uint32_t solid = ~((1u << AIR) | (1u << WATER) | (1u << TORCH));   // 空気と水と松明以外
        const int umin = fdiv(x0 - OX - 32, 16) + 1, umax = -fdiv(OX - x1, 16) - 1;
        int smin = fdiv(OY - 16 * (H - 1) - y1, 8) + 1, smax = -fdiv(y0 - OY - 31, 8) - 1;
        if (smin < 0) smin = 0;
        if (smax > 2 * W - 2) smax = 2 * W - 2;
        for (int s = smax; s >= smin; s--) {
            const int base = OY - 8 * s;
            int ylo = fdiv(base - y1, 16) + 1;
            int yhi = -fdiv(y0 - base - 31, 16) - 1;
            if (ylo < 0) ylo = 0;
            if (yhi > top) yhi = top;
            if (ylo > yhi) continue;
            int ulo = imax(imax(umin, -s), s - 2 * W + 2);
            const int uhi = imin(imin(umax, s), 2 * W - 2 - s);
            if (((ulo - s) & 1) != 0) ulo++;
            for (int u = ulo; u <= uhi; u += 2) {
                const int x = (s + u) >> 1, z = (s - u) >> 1;
                const Chunk* c = find(x >> 3, z >> 3);
                if (!c) continue;
                const int i = (x & 7) * 8 + (z & 7);
                const int yh = imin(c->col[i] - 1, yhi);
                if (ylo > yh) continue;
                // 左(-x)と右(-z)の隣の柱。チャンクの境目なら隣のチャンク(読み込んでいなければ空気)
                const Chunk* lc = c;
                int li = i - 8;
                if ((x & 7) == 0) { lc = find((x >> 3) - 1, z >> 3); li = i + 56; }
                const Chunk* rc = c;
                int ri = i - 1;
                if ((z & 7) == 0) { rc = find(x >> 3, (z >> 3) - 1); ri = i + 7; }
                const int bx = OX + 16 * u;
                for (int y = ylo; y <= yh; y++) {
                    const int o = y * kLayer;
                    const uint8_t b = BlockOf(c, o + i);
                    if (!b) continue;
                    const uint8_t a = (y + 1 < H) ? BlockOf(c, o + kLayer + i) : 0;
                    const uint8_t l = lc ? BlockOf(lc, o + li) : 0;
                    const uint8_t r = rc ? BlockOf(rc, o + ri) : 0;
                    // 面を描くか: 透けない(occluder の)ブロックの面は、隣が透けないブロックでなければ描く
                    // (葉の穴から後ろが見えるので、葉に面した面も描く)。葉の面は隣が空気か水なら描く。
                    // 水の面は隣が空気なら描く
                    bool da, dl, dr;
                    if (b == TORCH) {
                        // 松明: 前の3つの隣が全部透けないブロックなら見えない
                        da = dl = dr = !(((occ >> a) & 1) && ((occ >> l) & 1) && ((occ >> r) & 1));
                    } else if (b >= 2) {
                        const uint32_t hide = ((occluders_ >> b) & 1) ? occ : solid;
                        da = !((hide >> a) & 1);
                        dl = !((hide >> l) & 1);
                        dr = !((hide >> r) & 1);
                    } else {
                        da = a == AIR || a == TORCH; dl = l == AIR || l == TORCH; dr = r == AIR || r == TORCH;
                    }
                    if (!da && !dl && !dr) continue;   // 見える面が無い
                    if (front && !InFront(*front, x, y, z, b)) continue;
                    // 隠れたブロック: (x-k, y+k, z-k) は画面のちょうど同じ所(同じ六角形)に重なり、後から描かれる。
                    // そこに透けない(occluder の)ブロックがあれば、このブロックの絵は完全に描き潰されるので描かない。
                    // 六角形のどの画素も、視線を手前からたどって最初に当たる透けないブロック G を通り、G のその面の
                    // 隣は透けないブロックではない(G が一番手前)ので、上の規則で G のその面は必ず描かれる(G は
                    // このブロックより手前なので後から描かれる)。葉に面した面も描くのはこのため
                    if (cull_) {
                        bool hidden = false;
                        for (int k = 1; y + k <= top && x - k >= 0 && z - k >= 0; k++) {
                            if ((occ >> get(x - k, y + k, z - k)) & 1) { hidden = true; break; }
                        }
                        if (hidden) continue;
                    }
                    const int by = base - 16 * y;
                    if (b == TORCH) {
                        // 松明は自分で光るので、影も明るさも無くいつも同じ絵
                        DrawIcon(sink, TORCH, bx, by);
                        faces += 3;
                    } else if (b >= 2) {
                        const int sy = (b - 1) * kRowH;
                        // 面の明るさは、その面が向いている隣のマス(上・左(-x)・右(-z))の松明の明るさ
                        if (da) {
                            int sx = 32;
                            if (sun_) {
                                bool f, n;
                                topShadow(x, y, z, top, f, n);
                                sx = f ? (n ? 32 : 64) : (n ? 96 : 0);
                            }
                            const int q = (y + 1 < H) ? LightOf(c, o + kLayer + i) : 0;
                            DrawLit(sink, sx, 0, q, sy, 32, 15, bx, by);
                            faces++;
                        }
                        if (dl) {
                            int sx = 144;
                            if (sun_) {
                                bool uu, w;
                                leftShadow(x, y, z, top, uu, w);
                                sx = uu ? (w ? 144 : 160) : (w ? 176 : 128);
                            }
                            DrawLit(sink, sx, 128, LightOf(lc, o + li), sy, 16, 23, bx, by + 8);
                            faces++;
                        }
                        if (dr) {
                            DrawLit(sink, 192, 208, LightOf(rc, o + ri), sy, 16, 23, bx + 16, by + 8);
                            faces++;
                        }
                    } else {
                        // 水: 空気に面した所だけ、市松模様で半分透けた面を描く。真上が水でない水面は
                        // 元(WATER_HALF)と同じく 2px 低く見せる(上面を2px下げ、横の面は上2行を抜いた絵)
                        const bool surf = a != 1;
                        // 人や物の箱と重なった水: 上面は、人や物の中心より手前(画面で下)の所だけ人や物を隠す
                        const bool cutting = cut && front && !Apart(*front, x, y, z);
                        if (cutting) {
                            float sx, sy;
                            project((front->x0 + front->x1) * 0.5f, (float)(y + 1), (front->z0 + front->z1) * 0.5f, sx, sy);
                            *cut = iround(sy) + (surf ? 2 : 0);
                        }
                        if (da) { sink.draw(sink.ctx, 0, 0, 32, 15, bx, by + 2); faces++; }
                        if (cutting) *cut = -32768;
                        if (dl) { sink.draw(sink.ctx, surf ? 144 : 128, 0, 16, 23, bx, by + 8); faces++; }
                        if (dr) { sink.draw(sink.ctx, surf ? 160 : 192, 0, 16, 23, bx + 16, by + 8); faces++; }
                    }
                }
            }
        }
    }
    if (!front) last_faces_ = faces;
}

Face World::pick(int px, int py, int& rx, int& ry, int& rz) const {
    const int u0 = fdiv(px - OX_, 16);
    // 絵の縦の範囲 [by, by+30] に py が入る s だけを見る(手前 = s の小さい順)
    const int s0 = imax(0, fdiv(OY_ - py - 30 - 16 * (H - 1), 8));
    const int s1 = imin(2 * W_ - 2, fdiv(OY_ - py, 8) + 1);
    for (int s = s0; s <= s1; s++) {
        const int u = (((u0 - s) & 1) == 0) ? u0 : (u0 - 1);
        const int x = (s + u) >> 1, z = (s - u) >> 1;
        if (x < 0 || z < 0 || x >= W_ || z >= W_) continue;
        const int base = OY_ - 8 * s;
        const int bx = OX_ + 16 * u;
        for (int y = H - 1; y >= 0; y--) {
            const int by = base - 16 * y;
            if (py >= by && py <= by + 30 && get(x, y, z) != 0) {
                const Face f = FaceAt(px - bx, py - by);
                if (f != Face::None) { rx = x; ry = y; rz = z; return f; }
            }
        }
    }
    return Face::None;
}

void World::dirtyBlock(const Sink& sink, int x, int y, int z) const {
    int bx, by;
    blockPos(x, y, z, bx, by);
    const int ax0 = imax(bx, vx_), ay0 = imax(by, vy_);
    const int ax1 = imin(bx + 32, vx_ + vw_), ay1 = imin(by + 31, vy_ + vh_);
    if (ax0 < ax1 && ay0 < ay1) sink.dirty(sink.ctx, ax0, ay0, ax1 - ax0, ay1 - ay0);
}

// (x, y, z) を置いた/壊したとき: 太陽へ向かう直線が P を通るのは、上面なら P + {(0,-1,0),(0,-1,-1),(1,-1,0),(1,-1,-1)}
// + k(1,-1,-1)、左面なら P + {(1,0,0),(1,-1,0),(1,0,-1),(1,-1,-1)} + k(1,-1,-1) のブロック。k ごとにその8個の絵を
// 覆う矩形(左上が P の絵から (32k, 16k-8)、64x63)を描き直す(P 自身と、面の見え方が変わる隣も入る)
void World::dirtyEdit(const Sink& sink, int x, int y, int z) {
    // 松明の明るさが変わったマスの範囲: そのマスに面を向けているブロック(下・+x・+z)の絵を覆う矩形
    if (ld_valid_) {
        ld_valid_ = false;
        const int x0 = ld_[0], y0 = ld_[1] - 1, z0 = ld_[2], x1 = ld_[3] + 1, y1 = ld_[4], z1 = ld_[5] + 1;
        const int rx0 = OX_ + 16 * (x0 - z1), rx1 = OX_ + 16 * (x1 - z0) + 32;
        const int ry0 = OY_ - 8 * (x1 + z1) - 16 * y1, ry1 = OY_ - 8 * (x0 + z0) - 16 * y0 + 31;
        const int ax0 = imax(rx0, vx_), ay0 = imax(ry0, vy_);
        const int ax1 = imin(rx1, vx_ + vw_), ay1 = imin(ry1, vy_ + vh_);
        if (ax0 < ax1 && ay0 < ay1) sink.dirty(sink.ctx, ax0, ay0, ax1 - ax0, ay1 - ay0);
    }
    // その柱に足元がかかっている人や物は、地面の影の高さが変わりうる
    for (int i = 0; i < ent_cap_; i++) {
        const Entity& e = ents_[i];
        if (e.used && fabsf(e.x - (x + 0.5f)) <= 0.5f + e.r && fabsf(e.z - (z + 0.5f)) <= 0.5f + e.r) {
            entityChanged(sink, ((int)e.gen << 8) | (i + 1));
        }
    }
    int bx, by;
    blockPos(x, y, z, bx, by);
    for (int k = 0; k <= y + 1; k++) {
        const int rx = bx + 32 * k, ry = by - 8 + 16 * k;
        const int ax0 = imax(rx, vx_), ay0 = imax(ry, vy_);
        const int ax1 = imin(rx + 64, vx_ + vw_), ay1 = imin(ry + 63, vy_ + vh_);
        if (ax0 < ax1 && ay0 < ay1) sink.dirty(sink.ctx, ax0, ay0, ax1 - ax0, ay1 - ay0);
    }
}

// ===================================================================
// 人や物(エンティティ)
// ===================================================================


bool World::InFront(const Box& e, int bx, int by, int bz, uint8_t b) {
    // 見る人は -x・-z・+y の側。どれかの軸で離れていれば、見る人の側にある方が手前
    const bool fy = (float)by >= e.y1 - kEps, ky = (float)(by + 1) <= e.y0 + kEps;
    const bool fx = (float)(bx + 1) <= e.x0 + kEps, kx = (float)bx >= e.x1 - kEps;
    const bool fz = (float)(bz + 1) <= e.z0 + kEps, kz = (float)bz >= e.z1 - kEps;
    const bool f = fx || fy || fz, k = kx || ky || kz;
    if (f && !k) return true;
    if (k && !f) return false;
    // 重なっている: 水の中なら水を手前に(沈んで見える)。ブロックにめり込んでいるなら絵を上に
    if (!f) return b == WATER;
    // 手前の軸と奥の軸の両方で離れている(箱どうしの絵は重ならない): 中心の奥行き(x - y + z)で決める
    return (float)(bx + bz - by) + 0.5f < (e.x0 + e.x1 + e.z0 + e.z1 - e.y0 - e.y1) * 0.5f;
}

bool World::Apart(const Box& e, int bx, int by, int bz) {
    // 重なっていない(どれかの軸で離れている)か
    return (float)by >= e.y1 - kEps || (float)(by + 1) <= e.y0 + kEps || (float)(bx + 1) <= e.x0 + kEps
        || (float)bx >= e.x1 - kEps || (float)(bz + 1) <= e.z0 + kEps || (float)bz >= e.z1 - kEps;
}

int World::ground(float x, float y, float z) const {
    const int ix = (int)floorf(x), iz = (int)floorf(z);
    int yy = (int)floorf(y + 0.001f) - 1;   // 上面 yy+1 が y 以下(小数の誤差は少し許す)
    if (yy > H - 1) yy = H - 1;
    for (; yy >= 0; yy--) {
        const uint8_t b = get(ix, yy, iz);
        if (b && b != TORCH) return yy + 1;
    }
    return -1;
}

bool World::shadowShape(const Entity& e, float& cx, float& cy, float& a, float& b, int& gy) const {
    if (!e.shadow) return false;
    // 足元の正方形の四隅と中心のうち一番高い地面(段の端に立っていても下の段に影が浮かないように。
    // 四隅は箱の角そのもの: 立つ高さを同じ四隅で決めるアプリ(mobs.lua)と食い違わないように)
    gy = ground(e.x, e.y, e.z);
    const float r = e.r;
    for (int k = 0; k < 4; k++) {
        const int g = ground(e.x + ((k & 1) ? r : -r), e.y, e.z + ((k & 2) ? r : -r));
        if (g > gy) gy = g;
    }
    if (gy < 0) return false;
    // 高く上がるほど小さく(8マスで半分より少し小さいところまで)
    float k = 1.0f - (e.y - (float)gy) / 8.0f;
    if (k < 0.35f) k = 0.35f;
    if (k > 1.0f) k = 1.0f;
    const float rs = (e.r + 0.08f) * k;
    project(e.x, (float)gy, e.z, cx, cy);
    a = 22.627417f * rs;   // 半径 rs の円は、横 16√2・縦 8√2 倍の楕円に写る
    b = a * 0.5f;
    return a >= 0.5f;
}

void World::entityScreen(const Entity& e, int& dx, int& dy) const {
    float fx, fy;
    project(e.x, e.y, e.z, fx, fy);
    dx = iround(fx) - e.ax;
    dy = iround(fy) - e.ay;
}

bool World::entityRect(const Entity& e, int& x0, int& y0, int& x1, int& y1) const {
    if (!e.used || !e.visible) return false;
    bool any = false;
    if (e.sw > 0 && e.sh > 0) {
        int dx, dy;
        entityScreen(e, dx, dy);
        x0 = dx; y0 = dy; x1 = dx + e.sw; y1 = dy + e.sh;
        any = true;
    }
    float cx, cy, a, b;
    int gy;
    if (shadowShape(e, cx, cy, a, b, gy)) {
        const int sx0 = (int)floorf(cx - a), sy0 = (int)floorf(cy - b);
        const int sx1 = (int)ceilf(cx + a) + 1, sy1 = (int)ceilf(cy + b) + 1;
        if (!any) { x0 = sx0; y0 = sy0; x1 = sx1; y1 = sy1; any = true; }
        else { x0 = imin(x0, sx0); y0 = imin(y0, sy0); x1 = imax(x1, sx1); y1 = imax(y1, sy1); }
    }
    int ox0, oy0, ox1, oy1;
    if (overlayRect(e, ox0, oy0, ox1, oy1)) {
        if (!any) { x0 = ox0; y0 = oy0; x1 = ox1; y1 = oy1; any = true; }
        else { x0 = imin(x0, ox0); y0 = imin(y0, oy0); x1 = imax(x1, ox1); y1 = imax(y1, oy1); }
    }
    return any;
}

void World::markRect(const Sink& sink, int x0, int y0, int x1, int y1) const {
    if (!sink.dirty) return;
    x0 = imax(x0, vx_); y0 = imax(y0, vy_);
    x1 = imin(x1, vx_ + vw_); y1 = imin(y1, vy_ + vh_);
    if (x0 < x1 && y0 < y1) sink.dirty(sink.ctx, x0, y0, x1 - x0, y1 - y0);
}

Entity* World::entity(int handle) {
    const int i = (handle & 0xFF) - 1;
    if (i < 0 || i >= ent_cap_) return nullptr;
    Entity& e = ents_[i];
    return (e.used && e.gen == (uint16_t)(handle >> 8)) ? &e : nullptr;
}

// 奥から順(描く順)。奥行き = 箱の中心の x - y + z が大きいほど奥
int World::sortEntities() const {
    int n = 0;
    for (int i = 0; i < ent_cap_; i++) {
        const Entity& e = ents_[i];
        if (!e.used || !e.visible) continue;
        const float k = e.x + e.z - (e.y + e.h * 0.5f);
        int j = n++;
        while (j > 0 && escr_[j - 1].key < k) {
            escr_[j].key = escr_[j - 1].key;
            escr_[j].order = escr_[j - 1].order;
            j--;
        }
        escr_[j].key = k;
        escr_[j].order = (int16_t)i;
    }
    return n;
}

void World::freeEntities() {
    delete[] ents_;
    delete[] escr_;
    ents_ = nullptr;
    escr_ = nullptr;
    ent_cap_ = 0;
}

bool World::growEntities() {
    if (ent_cap_ >= kMaxEntities) return false;
    int cap = ent_cap_ + kEntityChunk;
    if (cap > kMaxEntities) cap = kMaxEntities;
    Entity* ne = new (std::nothrow) Entity[cap];
    EntScratch* ns = new (std::nothrow) EntScratch[cap];
    if (!ne || !ns) {
        delete[] ne;
        delete[] ns;
        return false;
    }
    for (int i = 0; i < ent_cap_; i++) ne[i] = ents_[i];   // 番号(=ハンドル)はそのまま
    delete[] ents_;
    delete[] escr_;
    ents_ = ne;
    escr_ = ns;
    ent_cap_ = cap;
    return true;
}

bool World::ensureShots() {
    if (!shots_) shots_ = new (std::nothrow) Shot[kMaxShots];
    return shots_ != nullptr;
}

int World::entityCount() const {
    int n = 0;
    for (int i = 0; i < ent_cap_; i++) if (ents_[i].used) n++;
    return n;
}

int World::entityAdd(const Sink& sink, const Entity& src) {
    int i = 0;
    while (i < ent_cap_ && ents_[i].used) i++;
    if (i >= ent_cap_ && !growEntities()) return 0;   // 上限か、メモリが足りない
    Entity& e = ents_[i];
    const uint16_t gen = (uint16_t)(e.gen + 1 > 0x7FFF ? 1 : e.gen + 1);   // ハンドルを正の int に収める
    e = src;
    e.used = true;
    e.gen = gen;
    e.lw = e.lh = 0;
    const int handle = ((int)gen << 8) | (i + 1);
    entityChanged(sink, handle);
    return handle;
}

void World::entityChanged(const Sink& sink, int handle) {
    Entity* e = entity(handle);
    if (!e) return;
    if (e->lw > 0) markRect(sink, e->lx, e->ly, e->lx + e->lw, e->ly + e->lh);
    int x0, y0, x1, y1;
    if (entityRect(*e, x0, y0, x1, y1)) {
        e->lx = (int16_t)x0; e->ly = (int16_t)y0; e->lw = (int16_t)(x1 - x0); e->lh = (int16_t)(y1 - y0);
        markRect(sink, x0, y0, x1, y1);
    } else {
        e->lw = e->lh = 0;
    }
}

bool World::entityRemove(const Sink& sink, int handle) {
    Entity* e = entity(handle);
    if (!e) return false;
    if (e->lw > 0) markRect(sink, e->lx, e->ly, e->lx + e->lw, e->ly + e->lh);
    e->used = false;
    return true;
}

void World::entityClear(const Sink& sink) {
    for (int i = 0; i < ent_cap_; i++) {
        if (ents_[i].used) entityRemove(sink, ((int)ents_[i].gen << 8) | (i + 1));
    }
}

void World::refreshEntityRects() {
    for (int i = 0; i < ent_cap_; i++) {
        Entity& e = ents_[i];
        if (!e.used) continue;
        int x0, y0, x1, y1;
        if (entityRect(e, x0, y0, x1, y1)) {
            e.lx = (int16_t)x0; e.ly = (int16_t)y0; e.lw = (int16_t)(x1 - x0); e.lh = (int16_t)(y1 - y0);
        } else {
            e.lw = e.lh = 0;
        }
    }
}

namespace {

// 書いてよい画素を絞った描き先: 人や物の絵の不透明な画素だけ / 地面の影の画素だけ。
// ブロックの面を、そこへだけ1画素ずつ描き直す
struct Masked {
    const World::Sink* base;
    int cx0, cy0, cx1, cy1;      // クリップ
    int cut = -32768;            // この行より上は書かない(重なった水の上面の、人や物より奥の所)
    // 絵
    const Entity* e = nullptr;
    int dx = 0, dy = 0;
    // 影
    const World* world = nullptr;
    float scx = 0, scy = 0, sa = 1, sb = 1;
    int gy = 0;

    bool allow(int x, int y) const {
        if (y < cut) return false;
        if (e) {
            int lx = x - dx;
            const int ly = y - dy;
            if (lx < 0 || ly < 0 || lx >= e->sw || ly >= e->sh) return false;
            if (e->flip) lx = e->sw - 1 - lx;
            return base->image_px(base->ctx, e->image, e->sx + lx, e->sy + ly) >= 0;
        }
        return shadowAt(x, y);
    }
    // 影の画素: 楕円の中の市松模様で、その所が高さ gy の地面の上面(上が空いている)であること
    bool shadowAt(int x, int y) const {
        if (((x + y) & 1) != 0) return false;
        const float ux = ((float)x + 0.5f - scx) / sa, uy = ((float)y + 0.5f - scy) / sb;
        if (ux * ux + uy * uy > 1.0f) return false;
        // 画面の点を、高さ gy の平面の点へ戻す(X - Z と X + Z)。上が水でもよい(水の底の影は水越しに見える)
        const float d = ((float)x + 0.5f - (float)world->originX() - 16.0f) / 16.0f;
        const float s = ((float)world->originY() + 32.0f - 16.0f * (float)gy - ((float)y + 0.5f)) / 8.0f;
        const int bx = (int)floorf((s + d) * 0.5f), bz = (int)floorf((s - d) * 0.5f);
        const uint8_t below = world->get(bx, gy - 1, bz), here = world->get(bx, gy, bz);
        return below != AIR && below != TORCH && (here == AIR || here == TORCH || here == WATER);
    }

    static void Draw(void* p, int sx, int sy, int w, int h, int dx, int dy) {
        const Masked* m = static_cast<const Masked*>(p);
        const int x0 = imax(dx, m->cx0), y0 = imax(dy, m->cy0);
        const int x1 = imin(dx + w, m->cx1), y1 = imin(dy + h, m->cy1);
        for (int y = y0; y < y1; y++) {
            for (int x = x0; x < x1; x++) {
                const int c = m->base->face_px(m->base->ctx, sx + x - dx, sy + y - dy);
                if (c > 0 && m->allow(x, y)) m->base->put(m->base->ctx, x, y, c);
            }
        }
    }
    static void Dither(void* p, int sx, int sy, int w, int h, int dx, int dy, int level) {
        const Masked* m = static_cast<const Masked*>(p);
        const int x0 = imax(dx, m->cx0), y0 = imax(dy, m->cy0);
        const int x1 = imin(dx + w, m->cx1), y1 = imin(dy + h, m->cy1);
        for (int y = y0; y < y1; y++) {
            for (int x = x0; x < x1; x++) {
                if (!World::DitherOn(level, x - dx, y - dy)) continue;
                const int c = m->base->face_px(m->base->ctx, sx + x - dx, sy + y - dy);
                if (c > 0 && m->allow(x, y)) m->base->put(m->base->ctx, x, y, c);
            }
        }
    }
};

}  // namespace

void World::renderEntities(const Sink& sink, int x0, int y0, int x1, int y1) {
    const int n = sortEntities();
    if (n == 0) return;
    Masked m;
    m.base = &sink;
    Sink ms;
    ms.ctx = &m;
    ms.draw = Masked::Draw;
    ms.dither = Masked::Dither;
    // 影を先に全部(人や物の絵の下に来るように)。影ごとに、地面より手前のブロックを影の画素の上へ描き直す
    for (int k = 0; k < n; k++) {
        const Entity& e = ents_[escr_[k].order];
        float cx, cy, a, b;
        int gy;
        if (!shadowShape(e, cx, cy, a, b, gy)) continue;
        const int rx0 = imax(x0, (int)floorf(cx - a)), ry0 = imax(y0, (int)floorf(cy - b));
        const int rx1 = imin(x1, (int)ceilf(cx + a) + 1), ry1 = imin(y1, (int)ceilf(cy + b) + 1);
        if (rx0 >= rx1 || ry0 >= ry1) continue;
        m.e = nullptr;
        m.world = this;
        m.scx = cx; m.scy = cy; m.sa = a; m.sb = b; m.gy = gy;
        m.cx0 = rx0; m.cy0 = ry0; m.cx1 = rx1; m.cy1 = ry1;
        bool any = false;
        for (int y = ry0; y < ry1; y++) {
            for (int x = rx0; x < rx1; x++) {
                if (m.shadowAt(x, y)) { sink.put(sink.ctx, x, y, e.shadow_color); any = true; }
            }
        }
        if (!any) continue;
        const float rs = a / 22.627417f;
        const Box box{e.x - rs, (float)gy, e.z - rs, e.x + rs, (float)gy, e.z + rs};
        if (open_) renderBlocks(ms, rx0, ry0, rx1, ry1, &box);
    }
    // 絵を奥から順に。描いたら、その箱より手前のブロックを絵の不透明な画素の上へ描き直す
    for (int k = 0; k < n; k++) {
        const Entity& e = ents_[escr_[k].order];
        if (e.sw <= 0 || e.sh <= 0) continue;
        int dx, dy;
        entityScreen(e, dx, dy);
        const int rx0 = imax(x0, dx), ry0 = imax(y0, dy);
        const int rx1 = imin(x1, dx + e.sw), ry1 = imin(y1, dy + e.sh);
        if (rx0 >= rx1 || ry0 >= ry1) continue;
        bool any = false;
        for (int y = ry0; y < ry1; y++) {
            for (int x = rx0; x < rx1; x++) {
                int lx = x - dx;
                if (e.flip) lx = e.sw - 1 - lx;
                const int c = sink.image_px(sink.ctx, e.image, e.sx + lx, e.sy + y - dy);
                if (c >= 0) { sink.put(sink.ctx, x, y, c); any = true; }
            }
        }
        if (!any || !open_) continue;
        m.e = &e;
        m.dx = dx; m.dy = dy;
        m.cx0 = rx0; m.cy0 = ry0; m.cx1 = rx1; m.cy1 = ry1;
        const Box box{e.x - e.r, e.y, e.z - e.r, e.x + e.r, e.y + e.h, e.z + e.r};
        renderBlocks(ms, rx0, ry0, rx1, ry1, &box, &m.cut);
    }
}

int World::entityAt(const Sink& sink, int px, int py) const {
    if (!sink.image_px) return 0;
    const int n = sortEntities();
    for (int k = n - 1; k >= 0; k--) {   // 手前から
        const Entity& e = ents_[escr_[k].order];
        int dx, dy;
        entityScreen(e, dx, dy);
        int lx = px - dx;
        const int ly = py - dy;
        if (lx < 0 || ly < 0 || lx >= e.sw || ly >= e.sh) continue;
        if (e.flip) lx = e.sw - 1 - lx;
        if (sink.image_px(sink.ctx, e.image, e.sx + lx, e.sy + ly) >= 0) return ((int)e.gen << 8) | (escr_[k].order + 1);
    }
    return 0;
}

// ===================================================================
// タワーディフェンス向けの道具: HPバーと印・弾・押し合い・近くの人や物・視線・全体の読み込み
// ===================================================================

bool World::setKeepAll(bool on) {
    if (on && K_ * K_ > kMaxChunks) return false;
    if (on && open_) {
        // 全部を読み込むので、要る数の置き場をここで確保しておく(途中で足りなくなって空気に見えないように)
        for (int i = 0; i < K_ * K_ && i < kMaxChunks; i++) {
            if (slots_[i]) continue;
            Chunk* c = static_cast<Chunk*>(malloc(slotBytes()));
            if (!c) {
                LOG_APP_WARN("ブロック: チャンクの置き場を %d 個しか確保できません(要るのは %d 個)", allocated_, K_ * K_);
                return false;
            }
            c->cx = c->cz = -1;
            c->dirty = false;
            c->no_light = !lighting();
            c->compact = compactChunks();
            slots_[i] = c;
            allocated_++;
        }
    }
    if (on != keep_all_) {
        keep_all_ = on;
        window_stale_ = true;
    }
    return true;
}

// HPバー(幅12・高さ3)と印(幅5の下向きの三角)の範囲。絵の上端の上に置く
bool World::overlayRect(const Entity& e, int& x0, int& y0, int& x1, int& y1) const {
    if (!e.used || !e.visible || (e.bar < 0 && e.mark < 0)) return false;
    int dx, dy;
    entityScreen(e, dx, dy);
    const int cx = dx + e.ax;
    x0 = cx - 6; x1 = cx + 6;
    y1 = dy - 1;
    y0 = e.mark >= 0 ? dy - 9 : dy - 4;
    return true;
}

void World::renderOverlays(const Sink& sink, int x0, int y0, int x1, int y1) const {
    auto put = [&](int x, int y, int c) {
        if (x >= x0 && x < x1 && y >= y0 && y < y1) sink.put(sink.ctx, x, y, c);
    };
    for (int i = 0; i < ent_cap_; i++) {
        const Entity& e = ents_[i];
        int rx0, ry0, rx1, ry1;
        if (!overlayRect(e, rx0, ry0, rx1, ry1)) continue;
        if (rx0 >= x1 || rx1 <= x0 || ry0 >= y1 || ry1 <= y0) continue;
        int dx, dy;
        entityScreen(e, dx, dy);
        const int cx = dx + e.ax;
        if (e.bar >= 0) {
            // 黒の枠(12x3)の中に、残りの割合だけ色の線(最大10)
            const int by = dy - 4;
            for (int y = by; y < by + 3; y++) for (int x = cx - 6; x < cx + 6; x++) put(x, y, 0);
            const int fill = (e.bar * 10 + 50) / 100;
            const int len = e.bar > 0 && fill < 1 ? 1 : (fill > 10 ? 10 : fill);
            for (int x = cx - 5; x < cx - 5 + len; x++) put(x, by + 1, e.bar_color & 15);
        }
        if (e.mark >= 0) {
            const int my = dy - 9;
            for (int r = 0; r < 3; r++) for (int x = cx - 2 + r; x <= cx + 2 - r; x++) put(x, my + r, e.mark & 15);
        }
    }
}

int World::nearby(float x, float z, float range, uint32_t tagmask, int32_t* out, int max) const {
    if (!out || max <= 0) return 0;
    int n = 0;
    const float r2 = range * range;
    for (int i = 0; i < ent_cap_; i++) {
        const Entity& e = ents_[i];
        if (!e.used) continue;
        if (tagmask && !((tagmask >> (e.tag & 31)) & 1)) continue;
        const float dx = e.x - x, dz = e.z - z, d2 = dx * dx + dz * dz;
        if (d2 > r2) continue;
        // 近い順に挿入(溢れたら一番遠いものを捨てる)
        int j = n < max ? n++ : max;
        if (j == max) {
            if (d2 >= escr_[max - 1].key) continue;
            j = max - 1;
        }
        while (j > 0 && escr_[j - 1].key > d2) { escr_[j].key = escr_[j - 1].key; out[j] = out[j - 1]; j--; }
        escr_[j].key = d2;
        out[j] = ((int32_t)e.gen << 8) | (i + 1);
    }
    return n;
}

// 足の裏から height マスの体が、その位置(柱)に収まるか
bool World::bodyFree(float x, float y, float z, int height, uint32_t pass) const {
    const int ix = (int)floorf(x), iz = (int)floorf(z);
    if (ix < 0 || iz < 0 || ix >= W_ || iz >= W_) return false;
    const int y0 = (int)floorf(y + 0.01f);
    for (int yy = y0 < 0 ? 0 : y0; yy < y0 + height && yy < H; yy++) {
        const uint8_t b = get(ix, yy, iz);
        if (b != AIR && b != WATER && b != TORCH && !((pass >> b) & 1)) return false;
    }
    return true;
}

int World::crowdStep(const Sink& sink, int iterations, int height, uint32_t pass) {
    if (iterations < 1) iterations = 1;
    if (height < 1) height = 1;
    EntScratch* sc = escr_;
    int n = 0;
    for (int i = 0; i < ent_cap_; i++) {
        if (ents_[i].used && ents_[i].crowd) sc[n++].idx = (int16_t)i;
    }
    if (n < 2) return 0;
    for (int k = 0; k < n; k++) { sc[k].ox = ents_[sc[k].idx].x; sc[k].oz = ents_[sc[k].idx].z; }
    for (int it = 0; it < iterations; it++) {
        bool any = false;
        for (int a = 0; a < n; a++) {
            Entity& A = ents_[sc[a].idx];
            for (int b = a + 1; b < n; b++) {
                Entity& B = ents_[sc[b].idx];
                if (A.crowd == 2 && B.crowd == 2) continue;
                if (fabsf(A.y - B.y) >= (float)height) continue;
                const float rr = A.r + B.r;
                float dx = B.x - A.x, dz = B.z - A.z;
                const float d2 = dx * dx + dz * dz;
                if (d2 >= rr * rr) continue;
                float d = sqrtf(d2);
                const float push = rr - d;
                if (d < 1e-4f) {
                    // ちょうど重なっている: 番号で決まる向きへ離す(毎回同じ結果になるように)
                    const float ang = (float)((sc[a].idx * 7 + sc[b].idx * 13) % 16) * 0.39269908f;
                    dx = cosf(ang); dz = sinf(ang); d = 1.0f;
                }
                const float ia = A.crowd == 2 ? 0.0f : 1.0f / (A.mass > 1e-3f ? A.mass : 1e-3f);
                const float ib = B.crowd == 2 ? 0.0f : 1.0f / (B.mass > 1e-3f ? B.mass : 1e-3f);
                if (ia + ib <= 0) continue;
                const float nx = dx / d, nz = dz / d;
                const float ma = push * ia / (ia + ib), mb = push * ib / (ia + ib);
                if (ma > 0) {
                    const float px = A.x - nx * ma, pz = A.z - nz * ma;
                    if (bodyFree(px, A.y, A.z, height, pass)) A.x = px;
                    if (bodyFree(A.x, A.y, pz, height, pass)) A.z = pz;
                }
                if (mb > 0) {
                    const float px = B.x + nx * mb, pz = B.z + nz * mb;
                    if (bodyFree(px, B.y, B.z, height, pass)) B.x = px;
                    if (bodyFree(B.x, B.y, pz, height, pass)) B.z = pz;
                }
                any = true;
            }
        }
        if (!any) break;
    }
    int moved = 0;
    for (int k = 0; k < n; k++) {
        Entity& e = ents_[sc[k].idx];
        if (e.x != sc[k].ox || e.z != sc[k].oz) {
            moved++;
            entityChanged(sink, ((int)e.gen << 8) | (sc[k].idx + 1));
        }
    }
    return moved;
}

bool World::lineOfSight(float x0, float y0, float z0, float x1, float y1, float z1, uint32_t pass) const {
    // 3D のマスたどり(Amanatides & Woo)
    int ix = (int)floorf(x0), iy = (int)floorf(y0), iz = (int)floorf(z0);
    const int ex = (int)floorf(x1), ey = (int)floorf(y1), ez = (int)floorf(z1);
    const float dx = x1 - x0, dy = y1 - y0, dz = z1 - z0;
    const int sx = dx > 0 ? 1 : (dx < 0 ? -1 : 0);
    const int sy = dy > 0 ? 1 : (dy < 0 ? -1 : 0);
    const int sz = dz > 0 ? 1 : (dz < 0 ? -1 : 0);
    const float inf = 1e30f;
    const float tdx = sx ? fabsf(1.0f / dx) : inf, tdy = sy ? fabsf(1.0f / dy) : inf, tdz = sz ? fabsf(1.0f / dz) : inf;
    float tmx = sx > 0 ? ((float)(ix + 1) - x0) * tdx : (sx < 0 ? (x0 - (float)ix) * tdx : inf);
    float tmy = sy > 0 ? ((float)(iy + 1) - y0) * tdy : (sy < 0 ? (y0 - (float)iy) * tdy : inf);
    float tmz = sz > 0 ? ((float)(iz + 1) - z0) * tdz : (sz < 0 ? (z0 - (float)iz) * tdz : inf);
    for (int guard = 0; guard < 4 * (W_ + H) + 8; guard++) {
        if (ix == ex && iy == ey && iz == ez) return true;
        // 次のマスへ
        if (tmx <= tmy && tmx <= tmz) { if (tmx > 1.0f) return true; ix += sx; tmx += tdx; }
        else if (tmy <= tmz) { if (tmy > 1.0f) return true; iy += sy; tmy += tdy; }
        else { if (tmz > 1.0f) return true; iz += sz; tmz += tdz; }
        if (ix == ex && iy == ey && iz == ez) return true;
        const uint8_t b = get(ix, iy, iz);
        if (b != AIR && b != WATER && b != TORCH && !((pass >> b) & 1)) return false;
    }
    return true;
}

// ---- 弾 ----

Shot* World::shot(int handle) {
    const int i = (handle & 0xFF) - 1;
    if (!shots_ || i < 0 || i >= kMaxShots) return nullptr;
    Shot& s = shots_[i];
    return (s.used && s.gen == (uint16_t)(handle >> 8)) ? &s : nullptr;
}

int World::shotCount() const {
    int n = 0;
    if (!shots_) return 0;
    for (int i = 0; i < kMaxShots; i++) if (shots_[i].used) n++;
    return n;
}

void World::shotAim(Shot& s) const {
    if (!s.target) return;
    const Entity* e = entity(s.target);
    if (!e) return;
    s.tx = e->x; s.ty = e->y + e->h * 0.5f; s.tz = e->z;
}

void World::shotPlace(Shot& s) const {
    const float p = s.p;
    s.x = s.ox + (s.tx - s.ox) * p;
    s.y = s.oy + (s.ty - s.oy) * p;
    s.z = s.oz + (s.tz - s.oz) * p;
    if (s.arc > 0) {
        const float ddx = s.tx - s.ox, ddz = s.tz - s.oz;
        s.y += s.arc * sqrtf(ddx * ddx + ddz * ddz) * 4.0f * p * (1.0f - p);
    }
}

bool World::shotRect(const Shot& s, int& x0, int& y0, int& x1, int& y1) const {
    if (!s.used) return false;
    float fx, fy;
    project(s.x, s.y, s.z, fx, fy);
    const int sz = s.size < 1 ? 1 : s.size;
    x0 = iround(fx) - sz / 2;
    y0 = iround(fy) - sz / 2;
    x1 = x0 + sz;
    y1 = y0 + sz;
    return true;
}

void World::shotChanged(const Sink& sink, Shot& s) {
    if (s.lw > 0) markRect(sink, s.lx, s.ly, s.lx + s.lw, s.ly + s.lh);
    int x0, y0, x1, y1;
    if (shotRect(s, x0, y0, x1, y1)) {
        s.lx = (int16_t)x0; s.ly = (int16_t)y0; s.lw = (int16_t)(x1 - x0); s.lh = (int16_t)(y1 - y0);
        markRect(sink, x0, y0, x1, y1);
    } else {
        s.lw = s.lh = 0;
    }
}

int World::shotAdd(const Sink& sink, const Shot& src) {
    if (!ensureShots()) return 0;
    for (int i = 0; i < kMaxShots; i++) {
        Shot& s = shots_[i];
        if (s.used) continue;
        const uint16_t gen = (uint16_t)(s.gen + 1 > 0x7FFF ? 1 : s.gen + 1);
        s = src;
        s.used = true;
        s.gen = gen;
        s.lw = s.lh = 0;
        s.ox = s.x; s.oy = s.y; s.oz = s.z;
        s.p = 0;
        if (s.target && !entity(s.target)) s.target = 0;
        shotAim(s);
        shotPlace(s);
        shotChanged(sink, s);
        return ((int)gen << 8) | (i + 1);
    }
    return 0;
}

bool World::shotRemove(const Sink& sink, int handle) {
    Shot* s = shot(handle);
    if (!s) return false;
    if (s->lw > 0) markRect(sink, s->lx, s->ly, s->lx + s->lw, s->ly + s->lh);
    s->used = false;
    return true;
}

void World::shotClear(const Sink& sink) {
    if (!shots_) return;
    for (int i = 0; i < kMaxShots; i++) {
        if (shots_[i].used) shotRemove(sink, ((int)shots_[i].gen << 8) | (i + 1));
    }
}

int World::shotsStep(const Sink& sink, float dt, ShotHit* out, int max) {
    int n = 0;
    if (!shots_) return 0;
    if (!(dt > 0)) dt = 0;
    for (int i = 0; i < kMaxShots; i++) {
        Shot& s = shots_[i];
        if (!s.used) continue;
        const int handle = ((int)s.gen << 8) | (i + 1);
        // 狙った人や物が消えていたら、最後に見た位置へ飛んで終わる(ハンドルは世代つきなので使い回されない)
        const bool lost = s.target && !entity(s.target);
        if (!lost) shotAim(s);
        if (s.p < 1.0f) {
            // 残りの距離(水平+高さ)を、狙う点が動いても割合で詰める
            const float ddx = s.tx - s.ox, ddy = s.ty - s.oy, ddz = s.tz - s.oz;
            float len = sqrtf(ddx * ddx + ddy * ddy + ddz * ddz);
            if (len < 0.05f) len = 0.05f;
            s.p += s.speed * dt / len;
            if (s.p > 1.0f) s.p = 1.0f;
        }
        shotPlace(s);
        if (s.p >= 1.0f) {
            if (n >= max || !out) { shotChanged(sink, s); continue; }   // 返しきれない分は次へ
            ShotHit& h = out[n++];
            h.shot = handle;
            h.target = lost ? 0 : s.target;
            h.tag = s.tag;
            h.lost = lost;
            h.x = s.x; h.y = s.y; h.z = s.z;
            shotRemove(sink, handle);
        } else {
            shotChanged(sink, s);
        }
    }
    return n;
}

void World::renderShots(const Sink& sink, int x0, int y0, int x1, int y1) const {
    if (!shots_) return;
    for (int i = 0; i < kMaxShots; i++) {
        const Shot& s = shots_[i];
        int rx0, ry0, rx1, ry1;
        if (!shotRect(s, rx0, ry0, rx1, ry1)) continue;
        rx0 = imax(rx0, x0); ry0 = imax(ry0, y0); rx1 = imin(rx1, x1); ry1 = imin(ry1, y1);
        for (int y = ry0; y < ry1; y++) for (int x = rx0; x < rx1; x++) sink.put(sink.ctx, x, y, s.color & 15);
    }
}

}  // namespace Iso
