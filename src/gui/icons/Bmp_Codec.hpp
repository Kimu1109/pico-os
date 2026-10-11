// Bmp_Codec.hpp
// Windows BMP の読み書き。スクリーンショットの保存(4bpp・パレット付き)と、SDに置いた.bmpの表示に使う。
//
// 読める形式: 無圧縮(BI_RGB)と BI_BITFIELDS の 1/4/8/16/24/32bpp、上から/下からの並び。
// RLE圧縮(BI_RLE4/8)・JPEG/PNG埋め込みは読まない(ReadInfo()がfalse)。
//
// 出し方は2通り:
//   - DecodeTo4bpp(): OSのパレット(PICO_GFX::COLORS)の16色へ寄せて4bppのスプライトへ書く。
//     パレット付きのBMPはパレットの各色を1回だけ近い色へ対応づける(1画素ごとに探さない)。
//     raw_indices=true なら、4bpp以下のBMPの番号をそのままOSのパレットの番号として使う
//     (OS向けに描いた4bitの絵。パレットをpico.set_paletteで差し替えていても番号が保たれる)。
//     フルカラー(16/24/32bpp)は1画素ごとに近い色を探す。dither=true なら4x4の組織的ディザを掛ける。
//   - DecodeRgb565(): フルカラーのまま1行ずつRGB565で返す(呼び出し側が16bppのスプライトや液晶へ描く)。
// どちらも画像の一部(窓)だけを読める(大きな画像を表示欄の大きさだけ読むため)。
// 作業に使うのは1行ぶんのバッファ(malloc、読み終えたら返す)とパレットの対応表(256B)だけ。
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "SdFat.h"
#include <LovyanGFX.hpp>
#include "gui/icons/icon_render.h"

namespace Bmp {

struct Info {
    uint16_t width = 0;
    uint16_t height = 0;
    uint8_t  bpp = 0;
    bool     top_down = false;     // 高さが負(上の行から並ぶ)
    uint32_t data_offset = 0;
    uint32_t row_bytes = 0;        // 1行のバイト数(4の倍数へ詰め物込み)
    uint16_t palette_count = 0;    // bpp<=8 のとき。パレットはファイルのpalette_offsetから
    uint32_t palette_offset = 0;
    // 16/32bppの色の取り出し方(BI_BITFIELDS。無ければ既定: 16bpp=555, 32bpp=8888)
    uint32_t mask[3] = {0, 0, 0};
};

// 見出しを読む。対応していない形式・壊れた値ならfalse
bool ReadInfo(FsFile& f, Info& out);

struct Options {
    bool dither = false;       // フルカラーを4bppへ寄せるとき組織的ディザを掛ける
    bool raw_indices = false;  // 4bpp以下: 番号をそのままOSのパレットの番号に使う
};

// 画像の(src_x, src_y, w, h)を、dstの(0,0)から書く(dstは呼び出し側が確保した4bppのスプライト)。
// 画像の外は書かない。途中で読めなくなったらfalse(書けたところまでは残る)
bool DecodeTo4bpp(FsFile& f, const Info& info, LGFX_Sprite& dst,
                  int src_x, int src_y, int w, int h, const Options& opt = Options());

// 1行ぶんのRGB565を受け取る。falseを返すとそこで打ち切る
using RowFn = bool (*)(void* ctx, int y, const uint16_t* rgb565, int w);
// 画像の(src_x, src_y, w, h)をフルカラーで1行ずつ渡す(yは窓の中の行番号、上から順)。
// row は呼び出し側の w 個ぶんのバッファ
bool DecodeRgb565(FsFile& f, const Info& info, int src_x, int src_y, int w, int h,
                  uint16_t* row, RowFn fn, void* ctx);

// 16bppのスプライトへフルカラーで書く(dstは呼び出し側が確保した16bppのスプライト)
bool DecodeToSprite16(FsFile& f, const Info& info, LGFX_Sprite& dst,
                      int src_x, int src_y, int w, int h);

// 画像全体を新しい4bppのスプライトへ読む(LoadPimgToSprite()のBMP版。Luaのpico.image_load)
bool LoadToSprite(FsFile& f, IconRender::PimgSprite& out, const Options& opt = Options());

// 4bppのスプライトの(0,0,w,h)を、今のOSのパレット(16色)付きの4bpp BMPとして書く
bool Encode4bpp(LGFX_Sprite& src, uint16_t w, uint16_t h, FsFile& f);

// RGB(8bitずつ)に一番近いOSのパレットの番号。pal888 は16色ぶんの{r,g,b}
uint8_t NearestIndex(const uint8_t (*pal888)[3], int r, int g, int b);
// PICO_GFX::COLORS(RGB565)を{r,g,b}へ広げる
void CurrentPalette888(uint8_t (*out)[3]);

}  // namespace Bmp
