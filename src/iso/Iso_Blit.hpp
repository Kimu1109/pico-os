#pragma once
// 2.5Dの箱庭(Iso_World)の面の絵を、4bpp の faces.pimg から 4bpp の画面(OSData::frame のバッファ)へ写す。
//
// ブロックの面の絵は「上面(32x15 のひし形)・左面・右面(16x23 の平行四辺形)」の3つの形しかなく、
// 透けた所の無いブロック(石・草…)の面は、その形の内側がすべて不透明で外側がすべて透過になっている。
// そういう面は1行につき「左端〜右端」の1区間を透過の判定なしにそのまま写せばよい(memcpy で済む)。
// set_image のときに各ブロック・各面の絵がこの形どおりかを調べておき(analyze)、そうでない絵(葉の穴・
// 水の市松模様・カーソル)だけ1画素ずつ透過を判定して写す。
//
// LovyanGFX に依存しない(バッファとクリップを渡すだけ)ので、ホストテストで画素を突き合わせられる。

#include <cstdint>
#include <cstring>

#include "iso/Iso_World.hpp"

namespace Iso {

class FaceBlitter {
public:
    static constexpr int kRows = 25;   // faces.pimg の段の数(水 + 23種 + カーソル)
    static constexpr int kCols = 9;    // 段の中の絵の数(上面4・左面4・右面1)
    static constexpr int kColX[kCols] = {0, 32, 64, 96, 128, 144, 160, 176, 192};

    // 形: 0 = 上面、1 = 左面、2 = 右面。行 r の不透明な区間 [Left, Right)
    static int ShapeOf(int col) { return col < 4 ? 0 : (col < 8 ? 1 : 2); }
    static int Height(int shape) { return shape == 0 ? 15 : 23; }
    static void Span(int shape, int r, int& l, int& rr) {
        if (shape == 0) {
            const int half = (r <= 7) ? 2 * (r + 1) : 2 * (15 - r);
            l = 16 - half; rr = 16 + half;
        } else if (shape == 1) {
            l = 2 * (r - 15); rr = 2 * r + 2;          // lx/2 が [r-15, r]
            if (l < 0) l = 0;
            if (rr > 16) rr = 16;
        } else {
            l = 2 * (7 - r); rr = 2 * (22 - r) + 2;    // rx/2 が [7-r, 22-r]
            if (l < 0) l = 0;
            if (rr > 16) rr = 16;
        }
    }

    static inline uint8_t Get(const uint8_t* row, int x) {
        return (x & 1) ? (row[x >> 1] & 0x0F) : (row[x >> 1] >> 4);
    }
    static inline void Put(uint8_t* row, int x, uint8_t c) {
        uint8_t& d = row[x >> 1];
        d = (x & 1) ? (uint8_t)((d & 0xF0) | c) : (uint8_t)((d & 0x0F) | (c << 4));
    }

    // 元の画像(4bpp、1行 ((w+1)/2) バイト)。各段・各絵が形どおりか調べる
    void setSource(const uint8_t* buf, int w, int h) {
        src_ = buf; sw_ = w; sh_ = h; sstride_ = (w + 1) >> 1;
        memset(exact_, 0, sizeof(exact_));
        if (!buf || w < 208) return;
        for (int row = 0; row < kRows; row++) {
            const int sy = row * kRowH;
            if (sy + kRowH > h) break;
            for (int c = 0; c < kCols; c++) {
                const int shape = ShapeOf(c), sx = kColX[c];
                bool ok = true;
                for (int r = 0; r < Height(shape) && ok; r++) {
                    int l, rr;
                    Span(shape, r, l, rr);
                    const uint8_t* srow = src_ + (size_t)(sy + r) * sstride_;
                    const int w2 = shape == 0 ? 32 : 16;
                    for (int x = 0; x < w2; x++) {
                        const bool in = x >= l && x < rr;
                        if ((Get(srow, sx + x) != 0) != in) { ok = false; break; }
                    }
                }
                if (ok) exact_[row] |= (uint16_t)(1u << c);
            }
        }
    }
    bool exact(int row, int col) const { return (exact_[row] >> col) & 1; }

    // 描き先(4bpp、1行 stride バイト)とクリップ [cx0, cx1) x [cy0, cy1)
    void setTarget(uint8_t* buf, int stride, int cx0, int cy0, int cx1, int cy1) {
        dst_ = buf; dstride_ = stride;
        cx0_ = cx0; cy0_ = cy0; cx1_ = cx1; cy1_ = cy1;
    }

    // 元の (sx, sy, w, h) を (dx, dy) へ。0番は透過
    void draw(int sx, int sy, int w, int h, int dx, int dy) const {
        if (!src_ || !dst_) return;
        // 形どおりの面か(段の境目にぴったり合う呼び出しだけ)
        int shape = -1;
        if (sy % kRowH == 0 && sy / kRowH < kRows) {
            const int row = sy / kRowH;
            for (int c = 0; c < kCols; c++) {
                if (kColX[c] == sx && exact(row, c)) {
                    const int s = ShapeOf(c);
                    if (w == (s == 0 ? 32 : 16) && h == Height(s)) shape = s;
                    break;
                }
            }
        }
        const int y0 = dy < cy0_ ? cy0_ : dy;
        const int y1 = dy + h > cy1_ ? cy1_ : dy + h;
        if (y0 >= y1 || dx >= cx1_ || dx + w <= cx0_) return;
        for (int py = y0; py < y1; py++) {
            const int r = py - dy;
            const uint8_t* srow = src_ + (size_t)(sy + r) * sstride_;
            uint8_t* drow = dst_ + (size_t)py * dstride_;
            if (shape >= 0) {
                int l, rr;
                Span(shape, r, l, rr);
                int a = dx + l, b = dx + rr;
                if (a < cx0_) a = cx0_;
                if (b > cx1_) b = cx1_;
                if (a < b) CopySpan(srow, sx + (a - dx), drow, a, b - a);
            } else {
                int a = dx, b = dx + w;
                if (a < cx0_) a = cx0_;
                if (b > cx1_) b = cx1_;
                const int o = sx - dx;
                for (int x = a; x < b; x++) {
                    const uint8_t c = Get(srow, x + o);
                    if (c) Put(drow, x, c);
                }
            }
        }
    }

    // 透過の判定なしに n 画素を写す
    static void CopySpan(const uint8_t* srow, int sx, uint8_t* drow, int dx, int n) {
        if (((sx ^ dx) & 1) == 0) {
            // 元と描き先で画素の上位/下位が揃っている: 端の半端な1画素と、間はバイト単位
            if (dx & 1) { Put(drow, dx, Get(srow, sx)); sx++; dx++; n--; }
            const int bytes = n >> 1;
            if (bytes > 0) memcpy(drow + (dx >> 1), srow + (sx >> 1), (size_t)bytes);
            if (n & 1) Put(drow, dx + n - 1, Get(srow, sx + n - 1));
        } else {
            // ずれている: 描き先の1バイトを、元の隣り合う2バイトの下位と上位から組み立てる
            if (dx & 1) { Put(drow, dx, Get(srow, sx)); sx++; dx++; n--; }
            const int bytes = n >> 1;
            const uint8_t* sp = srow + (sx >> 1);   // sx は奇数: このバイトの下位4bitから
            uint8_t* dp = drow + (dx >> 1);
            for (int i = 0; i < bytes; i++) dp[i] = (uint8_t)((sp[i] << 4) | (sp[i + 1] >> 4));
            if (n & 1) Put(drow, dx + n - 1, Get(srow, sx + n - 1));
        }
    }

private:
    const uint8_t* src_ = nullptr;
    int sw_ = 0, sh_ = 0, sstride_ = 0;
    uint16_t exact_[kRows] = {};
    uint8_t* dst_ = nullptr;
    int dstride_ = 0;
    int cx0_ = 0, cy0_ = 0, cx1_ = 0, cy1_ = 0;
};

}  // namespace Iso
