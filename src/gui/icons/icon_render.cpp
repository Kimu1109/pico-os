// icon_render.cpp
#include "icon_render.h"
#include "OS_Data.hpp"
#include "functions/GFX_Functions.hpp"

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
 
    // 描画範囲外への書き込みを防ぐ（画面端に配置した場合の安全策）
    OSData::frame->setClipRect(x, y, width, height);
 
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
 
    OSData::frame->clearClipRect();
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

void DrawPimgSprite(PimgSprite& s, int x, int y) {
    if (s.transparent) {
        s.sprite.pushSprite(OSData::frame, x, y, 0); // index0を透過キーとして使う
    } else {
        s.sprite.pushSprite(OSData::frame, x, y);
    }
}

}  // namespace IconRender
