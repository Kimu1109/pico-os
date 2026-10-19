// 2.5Dの箱庭: 描画(画家のアルゴリズム・影・松明の明るさ・カーソル)・引き当て・描き直す範囲

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

}  // namespace Iso
