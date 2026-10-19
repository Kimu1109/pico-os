// 2.5Dの箱庭: 人や物(エンティティ)の置き場・前後の判定・影・描画

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

}  // namespace Iso
