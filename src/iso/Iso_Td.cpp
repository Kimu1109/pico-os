// 2.5Dの箱庭: タワーディフェンス向けの道具(HPバーと印・弾・押し合い・近くの人や物・視線・全体の読み込み)

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
