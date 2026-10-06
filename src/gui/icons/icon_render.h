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

// 4bppのsrcの(sx, sy, w, h)を、OSData::frameの(dx, dy)へ直接写す(frameの今のクリップの内側だけ)。
// LovyanGFXのpushSprite()(とくに透過つき)は1画素ごとに色変換・透過の判定を通って重いので、
// 4bppのバッファどうしで写す。transparentなら0番の色を飛ばす。flip_x/flip_yで左右/上下を反転する。
// srcの外を指す分は削る。srcかframeが4bppでなければ何もせずfalse(呼び出し側が従来の道で描く)
bool Blit4bpp(LGFX_Sprite& src, int sx, int sy, int w, int h, int dx, int dy,
              bool transparent, bool flip_x = false, bool flip_y = false);

// LoadPimgToSprite()は新規にスプライトを確保するが、こちらは呼び出し側が既に
// width×heightでcreateSprite()済みのspriteへ、ファイルのヘッダ以降(RLE本体)を
// そのままデコードして書き込む(pico.canvas_load()がCanvasRasterの自前スプライトへ
// 直接読み込みたい場合向け。新規にスプライトを作らないのでImageウィジェット同様
// 二重確保しない)。fはあらかじめヘッダを読める位置(seek不要、内部でseekする)。
// ピクセルがheight行ぶん埋まらなかった場合(壊れたファイル)はfalseを返す。
bool DecodePimgBody(FsFile& f, LGFX_Sprite& sprite, uint16_t width, uint16_t height);

// .pimgのうち画像座標(src_x, src_y)からw×hの範囲だけを、dstの(0,0)へデコードする。
// 範囲の外のランは読み飛ばし、範囲の最後の行を過ぎたらファイルの残りは読まない。
// SDからは512Bずつまとめて読む(2バイトずつのf.read()は遅い)。ランは行ごとに
// drawFastHLine()でまとめて書く。dstはあらかじめw×h以上でcreateSprite()済みのこと。
// 透過(index 0)の画素も書く(透過の扱いはpushSprite()側で決める)。
bool DecodePimgWindow(FsFile& f, const PimgHeader& header, LGFX_Sprite& dst,
                      int src_x, int src_y, int w, int h);

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
