// icon_render.cpp
#include "icon_render.h"
#include "OS_Data.hpp"
#include "util/ScopedClip.hpp"
#include "functions/GFX_Functions.hpp"
#include <algorithm>
#include <cstring>

namespace IconRender {

bool DrawIconRaw(const IconAsset& asset,
                  int32_t x, int32_t y, uint8_t fgColor) {
    if (asset.data == nullptr || asset.data_len == 0) {
        // このIconID x IconSizeの組み合わせは generate_icons.py で
        // 生成されていない。
        return false;
    }
 
    const uint8_t* p = asset.data;
    const uint8_t* end = asset.data + asset.data_len;
    const int32_t width = asset.width;
    const int32_t height = asset.height;
 
    int32_t px = 0;
    int32_t py = 0;
 
    // データは必ず「off, on, off, on, ...」の順で始まる(生成側で保証済み)。
    bool opaque = false;
 
    // 描画範囲外への書き込みを防ぐ（画面端に配置した場合の安全策）。今のクリップ(FlushDirty()の
    // dirty矩形)との重なりへ狭め、抜けるときに元へ戻す(clearClipRect()すると、この後に描くものが
    // dirty矩形の外へはみ出す。util/ScopedClip.hpp参照)
    ScopedClip clip(x, y, width, height);
 
    while (p < end && py < height) {
        const uint8_t run_len = *p++;
 
        for (uint8_t i = 0; i < run_len; ++i) {
            if (py >= height) break;  // データ破損時のフェイルセーフ
 
            if (opaque) {
                // 新しい色を合成しない。既存パレット色をそのまま書き込むだけ。
                OSData::frame->writePixel(x + px, y + py, fgColor);
            }
            // opaque == false は書き込みスキップ(readPixelすら行わない)
 
            ++px;
            if (px >= width) {
                px = 0;
                ++py;
            }
        }
 
        opaque = !opaque;  // off/onを交互に切り替える
    }

    return true;
}

bool DrawIcon(IconID id, IconSize size,
              int32_t x, int32_t y, uint8_t fgColor) {
    const IconAsset& asset = GetIcon(id, size);
    return DrawIconRaw(asset, x, y, fgColor);
}

void DrawImageRLE4bpp(FsFile& f, int x, int y) {
    PimgHeader header;
    if (!ReadPimgHeader(f, header)) return;

    const bool transparent = header.flags & kPimgFlagTransparent;

    uint16_t px = 0, py = 0;
    uint8_t buf[2];
    f.seek(kPimgHeaderSize);
    while (py < header.height && f.read(buf, 2) == 2) {
        uint8_t run = buf[0], idx = buf[1];
        while (run--) {
            if (!transparent || idx != 0) {
                OSData::frame->writePixel(x + px, y + py, idx); // 4bit直書き
            }
            if (++px >= header.width) { px = 0; py++; }
        }
    }
}

bool LoadPimgToSprite(FsFile& f, PimgSprite& out) {
    PimgHeader header;
    f.seek(0);
    if (!ReadPimgHeader(f, header)) {
        out.usable = false;
        return false;
    }

    out.width = header.width;
    out.height = header.height;
    out.transparent = header.flags & kPimgFlagTransparent;

    out.sprite.setColorDepth(4);
    out.sprite.createSprite(out.width, out.height);
    for(int i = 0; i < 16; i++){
        out.sprite.setPaletteColor(i, PICO_GFX::COLORS[i]);
    }
    out.usable = true;

    // ロード時に一度だけRLEをデコード（以降このsprite上では発生しない）
    uint16_t px = 0, py = 0;
    uint8_t buf[2];
    while (py < out.height && f.read(buf, 2) == 2) {
        uint8_t run = buf[0], idx = buf[1];
        while (run--) {
            out.sprite.writePixel(px, py, idx);
            if (++px >= out.width) { px = 0; py++; }
        }
    }
    return true;
}

bool Blit4bpp(LGFX_Sprite& src, int sx, int sy, int w, int h, int dx, int dy,
              bool transparent, bool flip_x, bool flip_y) {
    LGFX_Sprite& dst = *OSData::frame;
    uint8_t* sbuf = static_cast<uint8_t*>(src.getBuffer());
    uint8_t* dbuf = static_cast<uint8_t*>(dst.getBuffer());
    if (!sbuf || !dbuf || (((int)src.getColorDepth() & 0xFF) != 4) || (((int)dst.getColorDepth() & 0xFF) != 4)
        || src.getRotation() != 0 || dst.getRotation() != 0) {
        return false;
    }
    const int sw = src.width(), sh = src.height();

    // srcの外を指す分を削る。反転するときは、削った列/行が描き先の反対側の端に当たる
    if (sx < 0) { if (!flip_x) dx -= sx; w += sx; sx = 0; }
    if (sx + w > sw) { const int e = sx + w - sw; if (flip_x) dx += e; w -= e; }
    if (sy < 0) { if (!flip_y) dy -= sy; h += sy; sy = 0; }
    if (sy + h > sh) { const int e = sy + h - sh; if (flip_y) dy += e; h -= e; }
    if (w <= 0 || h <= 0) return true;

    // 描き先: (dx, dy, w, h) ∩ 今のクリップ ∩ frame
    int32_t kx = 0, ky = 0, kw = 0, kh = 0;
    dst.getClipRect(&kx, &ky, &kw, &kh);
    const int dw = dst.width(), dh = dst.height();
    const int x0 = std::max({dx, (int)kx, 0}), x1 = std::min({dx + w, (int)(kx + kw), dw});
    const int y0 = std::max({dy, (int)ky, 0}), y1 = std::min({dy + h, (int)(ky + kh), dh});
    if (x0 >= x1 || y0 >= y1) return true;

    const int sstride = ((sw + 1) & ~1) >> 1;
    const int dstride = ((dw + 1) & ~1) >> 1;
    // 4bppは1バイトに2画素(左=上位4bit)
    auto getpx = [](const uint8_t* row, int x) -> uint8_t {
        return (x & 1) ? (row[x >> 1] & 0x0F) : (row[x >> 1] >> 4);
    };
    auto putpx = [](uint8_t* row, int x, uint8_t c) {
        uint8_t& d = row[x >> 1];
        d = (x & 1) ? (uint8_t)((d & 0xF0) | c) : (uint8_t)((d & 0x0F) | (c << 4));
    };

    for (int py = y0; py < y1; py++) {
        const int iy = flip_y ? (sy + h - 1 - (py - dy)) : (sy + (py - dy));
        const uint8_t* srow = sbuf + (size_t)iy * sstride;
        uint8_t* drow = dbuf + (size_t)py * dstride;

        if (flip_x) {
            const int base = sx + w - 1 + dx;  // 描き先pxの元は base - px
            for (int px = x0; px < x1; px++) {
                const uint8_t c = getpx(srow, base - px);
                if (transparent && c == 0) continue;
                putpx(drow, px, c);
            }
            continue;
        }

        const int ox = sx - dx;  // 描き先pxの元は px + ox
        int px = x0;
        if ((ox & 1) == 0) {
            // 元と描き先で画素の上位/下位が揃っている: 端の半端な1画素ずつと、間はバイト(2画素)単位
            if (px & 1) {
                const uint8_t c = getpx(srow, px + ox);
                if (!(transparent && c == 0)) putpx(drow, px, c);
                px++;
            }
            const int bytes = (x1 - px) >> 1;
            const uint8_t* sp = srow + ((px + ox) >> 1);
            uint8_t* dp = drow + (px >> 1);
            if (!transparent) {
                memmove(dp, sp, (size_t)bytes);
            } else {
                for (int i = 0; i < bytes; i++) {
                    const uint8_t b = sp[i];
                    if (!b) continue;
                    if ((b & 0xF0) && (b & 0x0F)) dp[i] = b;
                    else if (b & 0xF0) dp[i] = (uint8_t)((dp[i] & 0x0F) | (b & 0xF0));
                    else dp[i] = (uint8_t)((dp[i] & 0xF0) | (b & 0x0F));
                }
            }
            px += bytes * 2;
        } else {
            // ずれている(元が奇数ぶんずれる): 描き先の1バイト(2画素)を、元の隣り合う2バイトの
            // 下位4bitと上位4bitから組み立てる
            if (px & 1) {
                const uint8_t c = getpx(srow, px + ox);
                if (!(transparent && c == 0)) putpx(drow, px, c);
                px++;
            }
            const int bytes = (x1 - px) >> 1;
            const uint8_t* sp = srow + ((px + ox) >> 1);  // px+oxは奇数: このバイトの下位4bitから始まる
            uint8_t* dp = drow + (px >> 1);
            for (int i = 0; i < bytes; i++) {
                const uint8_t b = (uint8_t)((sp[i] << 4) | (sp[i + 1] >> 4));
                if (!transparent) { dp[i] = b; continue; }
                if (!b) continue;
                if ((b & 0xF0) && (b & 0x0F)) dp[i] = b;
                else if (b & 0xF0) dp[i] = (uint8_t)((dp[i] & 0x0F) | (b & 0xF0));
                else dp[i] = (uint8_t)((dp[i] & 0xF0) | (b & 0x0F));
            }
            px += bytes * 2;
        }
        for (; px < x1; px++) {
            const uint8_t c = getpx(srow, px + ox);
            if (transparent && c == 0) continue;
            putpx(drow, px, c);
        }
    }
    return true;
}

void DrawPimgSprite(PimgSprite& s, int x, int y) {
    if (Blit4bpp(s.sprite, 0, 0, s.width, s.height, x, y, s.transparent)) return;

    if (s.transparent) {
        s.sprite.pushSprite(OSData::frame, x, y, 0); // index0を透過キーとして使う
    } else {
        s.sprite.pushSprite(OSData::frame, x, y);
    }
}

bool DecodePimgBody(FsFile& f, LGFX_Sprite& sprite, uint16_t width, uint16_t height) {
    f.seek(kPimgHeaderSize);

    uint16_t px = 0, py = 0;
    uint8_t buf[2];
    while (py < height && f.read(buf, 2) == 2) {
        uint8_t run = buf[0], idx = buf[1];
        while (run--) {
            sprite.writePixel(px, py, idx);
            if (++px >= width) { px = 0; py++; }
        }
    }
    return py >= height; // 全ピクセルが埋まっていなければ壊れたファイル
}

bool DecodePimgWindow(FsFile& f, const PimgHeader& header, LGFX_Sprite& dst,
                      int src_x, int src_y, int w, int h) {
    if (w <= 0 || h <= 0 || header.width == 0) return false;
    const int x_end = src_x + w;
    const int y_end = src_y + h;
    if (y_end > header.height) return false;

    f.seek(kPimgHeaderSize);

    uint8_t buf[512];
    int len = 0, pos = 0;
    int px = 0, py = 0;
    while (py < y_end) {
        if (pos + 2 > len) {
            //読み残しの1バイトを先頭へ寄せてから続きを読む
            const int rest = len - pos;
            if (rest > 0) buf[0] = buf[pos];
            const int got = f.read(buf + rest, sizeof(buf) - rest);
            if (got <= 0) return false;
            len = rest + got;
            pos = 0;
            if (len < 2) return false;
        }
        int run = buf[pos];
        const uint8_t idx = buf[pos + 1];
        pos += 2;

        //ランは行をまたぎうるので、行ごとに切って書く
        while (run > 0 && py < y_end) {
            const int n = (header.width - px < run) ? header.width - px : run;
            if (py >= src_y) {
                const int a = px > src_x ? px : src_x;
                const int b = (px + n) < x_end ? (px + n) : x_end;
                if (b > a) dst.drawFastHLine(a - src_x, py - src_y, b - a, idx);
            }
            px += n;
            run -= n;
            if (px >= header.width) { px = 0; py++; }
        }
    }
    return true;
}

bool EncodePimg(LGFX_Sprite& sprite, uint16_t width, uint16_t height, FsFile& f, bool transparent) {
    if (width == 0 || height == 0) return false;

    uint8_t header[kPimgHeaderSize] = {
        (uint8_t)(width & 0xFF), (uint8_t)((width >> 8) & 0xFF),
        (uint8_t)(height & 0xFF), (uint8_t)((height >> 8) & 0xFF),
        (uint8_t)(transparent ? kPimgFlagTransparent : 0),
    };
    if (f.write(header, kPimgHeaderSize) != kPimgHeaderSize) return false;

    // 1回のf.write()呼び出しを減らすため、(run,idx)ペアを小さなバッファへ
    // ためてからまとめて書き出す(generate_pimg.pyと同じRLE規則: run=1〜255)
    uint8_t out_buf[256];
    size_t out_len = 0;
    auto flush = [&](void) -> bool {
        if (out_len == 0) return true;
        const bool ok = (f.write(out_buf, out_len) == out_len);
        out_len = 0;
        return ok;
    };
    auto emit = [&](uint8_t run, uint8_t idx) -> bool {
        if (out_len + 2 > sizeof(out_buf) && !flush()) return false;
        out_buf[out_len++] = run;
        out_buf[out_len++] = idx;
        return true;
    };

    uint8_t run = 0;
    uint8_t prev_idx = 0;
    for (uint16_t y = 0; y < height; ++y) {
        for (uint16_t x = 0; x < width; ++x) {
            const uint8_t idx = (uint8_t)(sprite.readPixelValue(x, y) & 0x0F);
            if (run > 0 && idx == prev_idx && run < 255) {
                run++;
                continue;
            }
            if (run > 0 && !emit(run, prev_idx)) return false;
            prev_idx = idx;
            run = 1;
        }
    }
    if (run > 0 && !emit(run, prev_idx)) return false;
    return flush();
}

}  // namespace IconRender
