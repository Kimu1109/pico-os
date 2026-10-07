// 2.5Dボクセルの箱庭のエンジン。説明は Iso_World.hpp。
// 地形の生成・描く順・影・引き当ては Lua 版(pc/sdcard/lua/apps/ブロック/ の旧 view.lua / world.lua)と
// 同じ答えになるように移してある(同じ種なら同じ地形。既存のセーブの「書き換えていないチャンク」を作り直すため)。

#include "iso/Iso_World.hpp"

#include <cstdlib>
#include <cstring>
#include <cstdio>

#include <Arduino.h>
#include <SdFat.h>

#include "OS_Data.hpp"
#include "functions/Log_Functions.hpp"

namespace Iso {

namespace {

constexpr int kWaterLevel = 5;
constexpr char kMagic[4] = {'B', 'L', 'K', '2'};
constexpr int kHead = 17;

// 床のある割り算(Lua の //)。b > 0
inline int fdiv(int a, int b) {
    const int q = a / b;
    return (a % b != 0 && a < 0) ? q - 1 : q;
}

inline int imax(int a, int b) { return a > b ? a : b; }
inline int imin(int a, int b) { return a < b ? a : b; }

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

void World::generate(int cx, int cz, uint8_t* b) const {
    memset(b, 0, kChunkBytes);
    const int ox = cx * 8, oz = cz * 8;
    if (kind_ == EMPTY) return;
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
    for (int i = 0; i < c->top * kLayer; i++) n += c->b[i] == TORCH;
    c->torches = (uint8_t)(n > 255 ? 255 : n);
}

void World::Finish(Chunk* c) {
    int top = 0;
    for (int i = 0; i < kLayer; i++) {
        int t = 0;
        for (int y = H - 1; y >= 0; y--) {
            if (c->b[y * kLayer + i]) { t = y + 1; break; }
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
    if (!WriteFile(path.c_str(), c->b, (size_t)c->top * kLayer)) return false;
    c->dirty = false;
    markSaved(c->cx * K_ + c->cz);
    return true;
}

void World::unload(Chunk* c) {
    if (c->dirty) {
        if (!writeChunk(c)) LOG_APP_WARN("ブロック: チャンク %d,%d を書き出せません", c->cx, c->cz);
    }
    uint8_t& m = map_[((c->cx & 31) << 5) | (c->cz & 31)];
    if (m && &pool_[m - 1] == c) m = 0;
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
    if (map_[mi]) unload(&pool_[map_[mi] - 1]);
    Chunk* best = nullptr;
    int best_d = -1;
    const int ac2 = wa0_ + wa1_, bc2 = wb0_ + wb1_;
    for (int i = 0; i < kMaxChunks; i++) {
        Chunk* c = &pool_[i];
        if (c->cx < 0) { best = c; best_d = -2; break; }
        if (inWindow(c->cx, c->cz, 0)) continue;
        const int a = c->cx - c->cz, b = c->cx + c->cz;
        const int d = abs(2 * a - ac2) + abs(2 * b - bc2);
        if (d > best_d) { best = c; best_d = d; }
    }
    if (!best) {
        if (!warned_full_) {
            LOG_APP_WARN("ブロック: チャンクの置き場(%d個)が足りません", kMaxChunks);
            warned_full_ = true;
        }
        return nullptr;
    }
    if (best->cx >= 0) unload(best);
    best->cx = (int16_t)cx;
    best->cz = (int16_t)cz;
    best->dirty = false;
    best->torches = 0;
    memset(best->light, 0, sizeof(best->light));
    map_[mi] = (uint8_t)(best - pool_ + 1);
    return best;
}

Chunk* World::loadChunk(int cx, int cz) {
    if (!pool_ || cx < 0 || cz < 0 || cx >= K_ || cz >= K_) return nullptr;
    Chunk* c = findMut(cx, cz);
    if (c) return c;
    c = allocSlot(cx, cz);
    if (!c) return nullptr;
    bool from_file = false;
    if (dir_.length() && isSaved(cx * K_ + cz)) {
        from_file = readChunkFile(cx, cz, c->b);
        if (!from_file) LOG_APP_WARN("ブロック: チャンク %d,%d を読めません。作り直します", cx, cz);
    }
    if (!from_file) generate(cx, cz, c->b);
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
    if (!pool_) return;
    for (int i = 0; i < kMaxChunks; i++) {
        if (pool_[i].cx >= 0) relight(pool_[i].cx, pool_[i].cz, false);
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
    if (!c) return;
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
    if (!pool_) return 0;
    int n = 0;
    for (int i = 0; i < kMaxChunks; i++) n += pool_[i].cx >= 0;
    return n;
}

int World::topAll() const {
    if (!pool_) return -1;
    int t = 0;
    for (int i = 0; i < kMaxChunks; i++) {
        if (pool_[i].cx >= 0 && pool_[i].top > t) t = pool_[i].top;
    }
    return t - 1;
}

void World::set(int x, int y, int z, uint8_t v) {
    if (x < 0 || z < 0 || y < 0 || x >= W_ || z >= W_ || y >= H || v > kBlockCount) return;
    Chunk* c = loadChunk(x >> 3, z >> 3);
    if (!c) return;
    const int i = (x & 7) * 8 + (z & 7);
    const uint8_t old = c->b[y * kLayer + i];
    if (old == v) return;
    c->b[y * kLayer + i] = v;
    c->dirty = true;
    // その柱の高さと、チャンクの段の数だけ数え直す
    int t = 0;
    for (int yy = H - 1; yy >= 0; yy--) {
        if (c->b[yy * kLayer + i]) { t = yy + 1; break; }
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
    if (!pool_) return;
    const int amin = -fdiv(7 - umin, 8), amax = fdiv(umax + 7, 8);
    const int bmin = -fdiv(14 - smin, 8), bmax = fdiv(smax, 8);
    wa0_ = amin; wa1_ = amax; wb0_ = bmin; wb1_ = bmax;
    // 範囲より1チャンクより外のチャンクは手放す(1チャンクぶんは残して、行き来で読み直さない)
    for (int i = 0; i < kMaxChunks; i++) {
        Chunk* c = &pool_[i];
        if (c->cx >= 0 && !inWindow(c->cx, c->cz, 1)) unload(c);
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
    if (!pool_) return 0;
    if (window_stale_) {
        int umin, umax, smin, smax;
        loadRange(umin, umax, smin, smax);
        window(umin, umax, smin, smax);
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
    pool_ = static_cast<Chunk*>(malloc(sizeof(Chunk) * kMaxChunks));
    scratch_ = static_cast<uint8_t*>(malloc(kScratchBytes));
    if (!pool_ || !scratch_) {
        LOG_APP_WARN("ブロック: チャンクの置き場(%u バイト)を確保できません",
                     (unsigned)(sizeof(Chunk) * kMaxChunks + kScratchBytes));
        free(pool_);
        free(scratch_);
        pool_ = nullptr;
        scratch_ = nullptr;
        return false;
    }
    ld_valid_ = false;
    for (int i = 0; i < kMaxChunks; i++) { pool_[i].cx = pool_[i].cz = -1; pool_[i].dirty = false; }
    memset(map_, 0, sizeof(map_));
    memset(saved_, 0, sizeof(saved_));
    dir_.assign(dir ? dir : "");
    kind_ = kind;
    K_ = k;
    W_ = k * 8;
    seed_ = seed;
    qlen_ = qpos_ = 0;
    wa0_ = wb0_ = 1; wa1_ = wb1_ = 0;
    window_stale_ = true;
    warned_full_ = false;
    return true;
}

void World::close() {
    free(pool_);
    pool_ = nullptr;
    free(scratch_);
    scratch_ = nullptr;
    ld_valid_ = false;
    memset(map_, 0, sizeof(map_));
    dir_.assign("");
    kind_ = EMPTY;
    K_ = 6;
    W_ = 48;
    qlen_ = qpos_ = 0;
}

bool World::create(const char* dir, uint8_t kind, uint32_t seed, int k, int& px, int& py, int& pz) {
    if (kind > EMPTY || !begin(dir, kind, k, seed)) return false;
    if (kind == DEMO) { px = 0; py = 1; pz = 0; return true; }
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
    if (kind > EMPTY || kk < 1) { err = "壊れています"; return false; }
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
    if (!pool_ || dir_.length() == 0) return false;
    bool ok = true;
    for (int i = 0; i < kMaxChunks; i++) {
        Chunk* c = &pool_[i];
        if (c->cx >= 0 && c->dirty && !writeChunk(c)) ok = false;
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
}

void World::setOrigin(int ox, int oy) {
    if (ox == OX_ && oy == OY_) return;
    OX_ = ox; OY_ = oy;
    window_stale_ = true;
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
    last_faces_ = 0;
    const int top = topAll();
    int faces = 0;
    if (pool_ && top >= 0) {
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
                    const uint8_t b = c->b[o + i];
                    if (!b) continue;
                    const uint8_t a = (y + 1 < H) ? c->b[o + kLayer + i] : 0;
                    const uint8_t l = lc ? lc->b[o + li] : 0;
                    const uint8_t r = rc ? rc->b[o + ri] : 0;
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
                        if (da) { sink.draw(sink.ctx, 0, 0, 32, 15, bx, by + 2); faces++; }
                        if (dl) { sink.draw(sink.ctx, surf ? 144 : 128, 0, 16, 23, bx, by + 8); faces++; }
                        if (dr) { sink.draw(sink.ctx, surf ? 160 : 192, 0, 16, 23, bx + 16, by + 8); faces++; }
                    }
                }
            }
        }
    }
    last_faces_ = faces;
    if (show_cursor_) {
        int bx, by;
        blockPos(cx_, cy_, cz_, bx, by);
        if (bx < x1 && bx + 32 > x0 && by < y1 && by + 31 > y0) {
            const int sy = kCursorRow * kRowH;
            sink.draw(sink.ctx, 0, sy, 32, 15, bx, by);
            sink.draw(sink.ctx, 128, sy, 16, 23, bx, by + 8);
            sink.draw(sink.ctx, 192, sy, 16, 23, bx + 16, by + 8);
        }
    }
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
    int bx, by;
    blockPos(x, y, z, bx, by);
    for (int k = 0; k <= y + 1; k++) {
        const int rx = bx + 32 * k, ry = by - 8 + 16 * k;
        const int ax0 = imax(rx, vx_), ay0 = imax(ry, vy_);
        const int ax1 = imin(rx + 64, vx_ + vw_), ay1 = imin(ry + 63, vy_ + vh_);
        if (ax0 < ax1 && ay0 < ay1) sink.dirty(sink.ctx, ax0, ay0, ax1 - ax0, ay1 - ay0);
    }
}

}  // namespace Iso
