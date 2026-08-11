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

}  // namespace IconRender
