// icon_render.h
// icons_data.h (generate_icons.pyで自動生成) のRLEデータをスプライトへ描画する。
#pragma once

#include "icons_data.h"

namespace IconRender {

// IconID + IconSize を指定して描画する（通常はこちらを使う）。
// fgColor: 前景色 (RGB565)。アイコンは単色前提。
// 戻り値: 指定サイズのデータが存在すれば true。存在しなければ false（何も描かない）。
bool DrawIcon(IconID id, IconSize size,
              int32_t x, int32_t y, uint8_t fgColor);

// IconAssetを直接指定する低レベル版（テーブルを介さず使いたい場合）。
bool DrawIconRaw(const IconAsset& asset,
                  int32_t x, int32_t y, uint8_t fgColor);

// IconSize -> 実ピクセルサイズ（正方形前提）。
inline int32_t IconPixelSize(IconSize size) {
    switch (size) {
        case IconSize::Px16: return 16;
        case IconSize::Px24: return 24;
        case IconSize::Px32: return 32;
        case IconSize::Px48: return 48;
        case IconSize::Px64: return 64;
        default: return 0;
    }
}

}  // namespace IconRender
