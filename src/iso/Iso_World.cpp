// 2.5Dボクセルの箱庭のエンジンの土台: チャンクの置き場・読み書き・松明の光・読み込む範囲。説明は Iso_World.hpp。
// 生成は Iso_Gen.cpp、開く/保存は Iso_Files.cpp、描画は Iso_Render.cpp、人や物は Iso_Entity.cpp、
// タワーディフェンス向けの道具は Iso_Td.cpp

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

namespace Detail {

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


}  // namespace Detail

World::~World() { close(); }

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

}  // namespace Iso
