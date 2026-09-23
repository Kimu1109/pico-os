// icon_render.h
// icons_data.h (generate_icons.pyで自動生成) のRLEデータをスプライトへ描画する。
#pragma once

#include "icons_data.h"
#include "SdFat.h"
#include <LovyanGFX.hpp>

namespace IconRender {

struct PimgHeader {
    uint16_t width;
    uint16_t height;
    uint8_t  flags;
};

struct PimgSprite {
    LGFX_Sprite sprite;
    uint16_t width = 0, height = 0;
    bool transparent = false;
    bool usable = false;
};

// IconID + IconSize を指定して描画する（通常はこちらを使う）。
// fgColor: 前景色 (RGB565)。アイコンは単色前提。
// 戻り値: 指定サイズのデータが存在すれば true。存在しなければ false（何も描かない）。
bool DrawIcon(IconID id, IconSize size,
              int32_t x, int32_t y, uint8_t fgColor);

// IconAssetを直接指定する低レベル版（テーブルを介さず使いたい場合）。
bool DrawIconRaw(const IconAsset& asset,
                  int32_t x, int32_t y, uint8_t fgColor);

static constexpr uint32_t kPimgHeaderSize = 5;

void DrawImageRLE4bpp(FsFile& f, int x, int y);
bool LoadPimgToSprite(FsFile& f, PimgSprite& out);

static constexpr uint8_t kPimgFlagTransparent = 0x01;

inline bool ReadPimgHeader(FsFile& f, PimgHeader& header) {
    uint8_t buf[5];
    f.seek(0);
    if (f.read(buf, 5) != 5) return false;
    header.width  = buf[0] | (static_cast<uint16_t>(buf[1]) << 8);
    header.height = buf[2] | (static_cast<uint16_t>(buf[3]) << 8);
    header.flags  = buf[4];
    return true;
}

void DrawPimgSprite(PimgSprite& s, int x, int y);

// LoadPimgToSprite()は新規にスプライトを確保するが、こちらは呼び出し側が既に
// width×heightでcreateSprite()済みのspriteへ、ファイルのヘッダ以降(RLE本体)を
// そのままデコードして書き込む(pico.canvas_load()がCanvasRasterの自前スプライトへ
// 直接読み込みたい場合向け。新規にスプライトを作らないのでImageウィジェット同様
// 二重確保しない)。fはあらかじめヘッダを読める位置(seek不要、内部でseekする)。
// ピクセルがheight行ぶん埋まらなかった場合(壊れたファイル)はfalseを返す。
bool DecodePimgBody(FsFile& f, LGFX_Sprite& sprite, uint16_t width, uint16_t height);

// sprite(4bpp、readPixelValue()でパレット番号0〜15を読める前提)の中身を
// 行優先(ラスタスキャン)でRLE符号化し、.pimg形式でfへ書き出す
// (script/generate_pimg.pyのC++版エンコーダ)。width/heightは呼び出し側が
// 把握しているサイズ(通常はspriteを作った時のサイズ)をそのまま渡す。
// 書き込みに失敗した場合(SD容量不足等)はfalseを返す。
bool EncodePimg(LGFX_Sprite& sprite, uint16_t width, uint16_t height, FsFile& f, bool transparent = false);

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

inline IconSize GetIconSize(int8_t size){
    switch (size) {
        case 16: return IconSize::Px16;
        case 24: return IconSize::Px24;
        case 32: return IconSize::Px32;
        case 48: return IconSize::Px48;
        case 64: return IconSize::Px64;
        default: return IconSize::Px24;
    }
}

}  // namespace IconRender
