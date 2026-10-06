// Fill4bpp.hpp
// OSData::frame(4bppパレット)へ、塗りの図形をバッファへ直接書いて描く。
//
// LovyanGFXのfillRect/fillCircle等は1本の横線ごとに仮想関数・クリップ・色変換を通るので、
// 図形1つで数十〜数百回それを繰り返す。ここではバッファ・1行のバイト数・クリップを
// 図形1つにつき1回だけ求め、横線は「端の半端な画素 + 間はmemset」で書く。
// 図形の形(どの横線を塗るか)はLovyanGFXと同じ手順を写してあるので、描かれる画素は変わらない。
//
// frameが4bppでない・回転しているとき(ホストテストのスタブ等)は Span::ok が偽になり、
// 呼び出し側は今までどおりLovyanGFXの関数で描く。
#pragma once

#include <LovyanGFX.hpp>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include "OS_Data.hpp"

namespace Fill4bpp {

struct Span {
    uint8_t* buf = nullptr;
    int stride = 0;
    int cl = 0, ct = 0, cr = -1, cb = -1;  // クリップ(右端・下端を含む)
    uint8_t c = 0, cc = 0;                  // 色(4bit)と、1バイトに2画素ぶん並べたもの
    bool ok = false;

    explicit Span(int color) {
        LGFX_Sprite& f = *OSData::frame;
        buf = static_cast<uint8_t*>(f.getBuffer());
        if (!buf || (((int)f.getColorDepth() & 0xFF) != 4) || f.getRotation() != 0) return;
        const int w = f.width(), h = f.height();
        stride = ((w + 1) & ~1) >> 1;
        int32_t x, y, cw, ch;
        f.getClipRect(&x, &y, &cw, &ch);
        cl = std::max((int)x, 0);
        ct = std::max((int)y, 0);
        cr = std::min((int)(x + cw), w) - 1;
        cb = std::min((int)(y + ch), h) - 1;
        c = (uint8_t)(color & 0x0F);
        cc = (uint8_t)(c | (c << 4));
        ok = true;
    }

    // クリップ済みの1本(w>=1)
    void row(int x, int y, int w) const {
        uint8_t* r = buf + (size_t)y * stride;
        if (x & 1) {
            uint8_t& d = r[x >> 1];
            d = (uint8_t)((d & 0xF0) | c);
            x++;
            w--;
        }
        const int n = w >> 1;
        if (n > 0) memset(r + (x >> 1), cc, (size_t)n);
        if (w & 1) {
            uint8_t& d = r[(x + n * 2) >> 1];
            d = (uint8_t)((d & 0x0F) | (c << 4));
        }
    }

    // LGFXBase::writeFastHLine と同じ(wが1未満なら何もしない)
    void hline(int x, int y, int w) const {
        if (y < ct || y > cb) return;
        if (x < cl) { w += x - cl; x = cl; }
        const int r = cr + 1 - x;
        if (w > r) w = r;
        if (w < 1) return;
        row(x, y, w);
    }

    // LGFXBase::writeFillRect と同じ
    void rect(int x, int y, int w, int h) const {
        if (x < cl) { w += x - cl; x = cl; }
        const int r = cr + 1 - x;
        if (w > r) w = r;
        if (w < 1) return;
        if (y < ct) { h += y - ct; y = ct; }
        const int b = cb + 1 - y;
        if (h > b) h = b;
        if (h < 1) return;
        // 行ごとの端の形は同じなので、先に1回だけ求めて行を進めるだけにする
        const bool lead = x & 1;
        const int x2 = x + lead;
        const int n = (w - lead) >> 1;
        const bool tail = (w - lead) & 1;
        uint8_t* const top = buf + (size_t)y * stride;
        uint8_t* const mid0 = top + (x2 >> 1);
        uint8_t* const lp = top + (x >> 1);
        uint8_t* const tp = top + ((x2 + n * 2) >> 1);
        if (lead) { uint8_t* d = lp; for (int i = 0; i < h; i++, d += stride) *d = (uint8_t)((*d & 0xF0) | c); }
        if (n > 0) { uint8_t* d = mid0; for (int i = 0; i < h; i++, d += stride) memset(d, cc, (size_t)n); }
        if (tail) { uint8_t* d = tp; for (int i = 0; i < h; i++, d += stride) *d = (uint8_t)((*d & 0x0F) | (c << 4)); }
    }
};

// fillRect(x, y, w, h, color)。負の幅/高さは反対向きとして扱う(LovyanGFXと同じ)
inline bool FillRect(int x, int y, int w, int h, int color) {
    const Span s(color);
    if (!s.ok) return false;
    if (w < 0) { x += w; w = -w; }
    if (h < 0) { y += h; h = -h; }
    s.rect(x, y, w, h);
    return true;
}

// drawFastHLine(x, y, w, color)
inline bool HLine(int x, int y, int w, int color) {
    const Span s(color);
    if (!s.ok) return false;
    if (w < 0) { x += w; w = -w; }
    s.hline(x, y, w);
    return true;
}

// fillCircle(x, y, r, color)。LGFXBase::fillCircle + fillCircleHelper(corners=3, delta=0)の写し
inline bool FillCircle(int x, int y, int r, int color) {
    const Span s(color);
    if (!s.ok) return false;
    s.hline(x - r, y, (r << 1) + 1);
    if (r <= 0) return true;
    const int delta = 1;
    int f = 1 - r, ddF_y = -(r << 1), ddF_x = 1, i = 0;
    do {
        int len = 0;
        while (f < 0) { f += (ddF_x += 2); ++len; }
        i += len;
        f += (ddF_y += 2);
        if (len) s.rect(x - r, y + i - len + 1, (r << 1) + delta, len);
        s.hline(x - i, y + r, (i << 1) + delta);
        s.hline(x - i, y - r, (i << 1) + delta);
        if (len) s.rect(x - r, y - i, (r << 1) + delta, len);
    } while (i < --r);
    return true;
}

// fillEllipse(x, y, rx, ry, color)。LGFXBase::fillEllipse の写し
inline bool FillEllipse(int x, int y, int rx, int ry, int color) {
    const Span s(color);
    if (!s.ok) return false;
    if (ry == 0) {
        int w = (rx << 1) + 1, xx = x - rx;
        if (w < 0) { xx += w; w = -w; }
        s.hline(xx, y, w);
        return true;
    }
    if (rx == 0) {
        int h = (ry << 1) + 1, yy = y - ry;
        if (h < 0) { yy += h; h = -h; }
        s.rect(x, yy, 1, h);
        return true;
    }
    if (rx < 0 || ry < 0) return true;

    const int32_t rx2 = rx * rx, ry2 = ry * ry;
    s.hline(x - rx, y, (rx << 1) + 1);
    int32_t i = 0, yt = 0, xt = rx;
    int32_t t = (rx2 << 1) + ry2 * (1 - (rx << 1));
    do {
        while (t < 0) t += rx2 * ((++yt << 2) + 2);
        s.rect(x - xt, y - yt, (xt << 1) + 1, yt - i);
        s.rect(x - xt, y + i + 1, (xt << 1) + 1, yt - i);
        i = yt;
        t -= (--xt) * ry2 << 2;
    } while (rx2 * yt <= ry2 * xt);

    xt = 0;
    yt = ry;
    t = (ry2 << 1) + rx2 * (1 - (ry << 1));
    do {
        while (t < 0) t += ry2 * ((++xt << 2) + 2);
        s.hline(x - xt, y - yt, (xt << 1) + 1);
        s.hline(x - xt, y + yt, (xt << 1) + 1);
        t -= (--yt) * rx2 << 2;
    } while (ry2 * xt <= rx2 * yt);
    return true;
}

// fillTriangle(...)。LGFXBase::fillTriangle の写し。3点が一直線ならLovyanGFXのdrawLineに任せる
inline bool FillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, int color) {
    const Span s(color);
    if (!s.ok) return false;
    int a, b;
    if (y0 > y1) { std::swap(y0, y1); std::swap(x0, x1); }
    if (y1 > y2) { std::swap(y2, y1); std::swap(x2, x1); }
    if (y0 > y1) { std::swap(y0, y1); std::swap(x0, x1); }

    if (y0 == y2) {
        a = b = x0;
        if (x1 < a) a = x1; else if (x1 > b) b = x1;
        if (x2 < a) a = x2; else if (x2 > b) b = x2;
        s.hline(a, y0, b - a + 1);
        return true;
    }
    if ((x1 - x0) * (y2 - y0) == (x2 - x0) * (y1 - y0)) {
        OSData::frame->drawLine(x0, y0, x2, y2, (int8_t)color);
        return true;
    }

    int dy1 = y1 - y0, dy2 = y2 - y0;
    const bool change = ((x1 - x0) * dy2 > (x2 - x0) * dy1);
    int dx1 = std::abs(x1 - x0), dx2 = std::abs(x2 - x0);
    int xstep1 = x1 < x0 ? -1 : 1, xstep2 = x2 < x0 ? -1 : 1;
    a = b = x0;
    if (change) {
        std::swap(dx1, dx2);
        std::swap(dy1, dy2);
        std::swap(xstep1, xstep2);
    }
    int err1 = (std::max(dx1, dy1) >> 1) + (xstep1 < 0 ? std::min(dx1, dy1) : dx1);
    int err2 = (std::max(dx2, dy2) >> 1) + (xstep2 > 0 ? std::min(dx2, dy2) : dx2);
    if (y0 != y1) {
        do {
            err1 -= dx1;
            while (err1 < 0) { err1 += dy1; a += xstep1; }
            err2 -= dx2;
            while (err2 < 0) { err2 += dy2; b += xstep2; }
            s.hline(a, y0, b - a + 1);
        } while (++y0 < y1);
    }

    if (change) {
        b = x1;
        xstep2 = x2 < x1 ? -1 : 1;
        dx2 = std::abs(x2 - x1);
        dy2 = y2 - y1;
        err2 = (std::max(dx2, dy2) >> 1) + (xstep2 > 0 ? std::min(dx2, dy2) : dx2);
    } else {
        a = x1;
        dx1 = std::abs(x2 - x1);
        dy1 = y2 - y1;
        xstep1 = x2 < x1 ? -1 : 1;
        err1 = (std::max(dx1, dy1) >> 1) + (xstep1 < 0 ? std::min(dx1, dy1) : dx1);
    }
    do {
        err1 -= dx1;
        while (err1 < 0) { err1 += dy1; if ((a += xstep1) == x2) break; }
        err2 -= dx2;
        while (err2 < 0) { err2 += dy2; if ((b += xstep2) == x2) break; }
        s.hline(a, y0, b - a + 1);
    } while (++y0 <= y2);
    return true;
}

}  // namespace Fill4bpp
