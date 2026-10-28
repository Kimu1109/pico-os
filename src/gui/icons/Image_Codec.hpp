// Image_Codec.hpp
// SDの画像(.pimg / .bmp / .jpg / .png)をまとめて扱う窓口。形式はファイルの先頭で見分ける(拡張子に頼らない)。
// 使うのは pico.image_load(Lua)・ImageView(ファイルビューワー)・Image(MarkdownView の画像)。
//
// 出し方は2通り:
//   - DecodeTo4bpp() / LoadToSprite(): OSのパレット(PICO_GFX::COLORS)の16色へ寄せて4bppのスプライトへ書く。
//     フルカラーの画素は既定で4x4の組織的ディザを掛ける(パレットの色にほぼ等しい画素には掛けない。ドット絵が汚れないように)。
//   - DecodeRgb565(): フルカラーのまま小さな四角ごとに RGB565 で渡す(液晶へそのまま送る。ImageView のフルカラー表示)。
// どちらも画像の一部(窓)だけを受け取れる。JPEG・PNG は 1/2・1/4・1/8 に縮めて解ける(座標は縮めた後のもの)。
// 透過: .pimg は透過の印、PNG はアルファ(96未満を透過。generate_pimg.py と同じ)。透過にしない絵の半透明は background の色と混ぜる。
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "SdFat.h"
#include <LovyanGFX.hpp>
#include "consts.hpp"
#include "gui/icons/icon_render.h"
#include "gui/icons/Bmp_Codec.hpp"
#include "gui/icons/Jpeg_Codec.hpp"
#include "gui/icons/Png_Codec.hpp"

namespace ImageCodec {

enum class Format : uint8_t { Unknown, Pimg, Bmp, Jpeg, Png };

struct Info {
    Format format = Format::Unknown;
    uint16_t width = 0;    // 元の大きさ
    uint16_t height = 0;
    bool alpha = false;    // .pimg の透過の印 / PNG のアルファの列
    IconRender::PimgHeader pimg = {0, 0, 0};
    Bmp::Info bmp;
    Png::Info png;
};

// 先頭を見て形式を見分け、大きさを読む。読めない・対応していなければfalse。
// .pimg は印が無いので、BMP/JPEG/PNG のどれでもなければ .pimg として読む(今までどおり)
bool ReadInfo(FsFile& f, Info& out);

// 縮めて解ける形式か(JPEG・PNG)
inline bool CanScale(const Info& info){ return info.format == Format::Jpeg || info.format == Format::Png; }
// 16色に収まらない絵か(JPEG・PNG・8bpp以上のBMP)。フルカラーで見せる意味があるもの
bool IsFullColor(const Info& info);

inline uint16_t ScaledSize(uint16_t v, uint8_t scale){
    return (uint16_t)((v + (1u << scale) - 1) >> scale);
}
inline uint16_t ScaledW(const Info& info, uint8_t scale){ return CanScale(info) ? ScaledSize(info.width, scale) : info.width; }
inline uint16_t ScaledH(const Info& info, uint8_t scale){ return CanScale(info) ? ScaledSize(info.height, scale) : info.height; }
// 4bppで持つときのバイト数(端数は切り上げ)
inline size_t Bytes4bpp(int w, int h){ return ((size_t)w * (size_t)h + 1) / 2; }

// max_w x max_h に収まり、4bppで max_bytes 以下になる一番小さい縮め方(0〜3。1/8でも収まらなければ3)。
// 0以下の上限は見ない。縮められない形式は常に0
uint8_t FitScale(const Info& info, int max_w, int max_h, size_t max_bytes = 0);

struct Options {
    uint8_t scale = 0;          // JPEG・PNG だけ(0〜3)
    int8_t dither = -1;         // -1: フルカラーの絵なら掛ける(BMPは8bpp超だけ。Bmp::Optionsと同じ)
    bool raw_indices = false;   // BMP の4bpp以下: 番号をそのままOSのパレットの番号に使う
    int8_t transparent = -1;    // PNG: -1=アルファの列があれば透過にする、1=常に(tRNSの絵)、0=しない
    uint8_t background = PICO_BACKGROUND;   // 透過にしない絵の半透明を混ぜる色(パレットの番号)
};

// PNG を透過の絵として読むか(Options::transparent の -1 を解く)。.pimg はファイルの印
bool UsesTransparency(const Info& info, const Options& opt);

// 画像の(x, y, w, h)(縮めた後の座標)を、dstの(0,0)から書く(dstは呼び出し側が確保した4bppのスプライト)。
// 画像の外は書かない。途中で読めなくなったらfalse(書けたところまでは残る)
bool DecodeTo4bpp(FsFile& f, const Info& info, LGFX_Sprite& dst, int x, int y, int w, int h, const Options& opt = Options());

// 画像全体を新しい4bppのスプライトへ読む(縮めた後の大きさ。LoadPimgToSprite()の全形式版)
bool LoadToSprite(FsFile& f, const Info& info, IconRender::PimgSprite& out, const Options& opt = Options());

// RGB565(上位から r5 g6 b5、この機械の並び)の四角を受け取る。x, y は窓の中の位置、rgb の行の幅は w
using Rgb565Fn = void (*)(void* ctx, int x, int y, int w, int h, const uint16_t* rgb);

// 画像の(x, y, w, h)(縮めた後の座標)をフルカラーのまま fn へ渡す。窓の外は渡さない。
// .pimg はフルカラーで持たないのでfalse。途中で読めなくなったらfalse(渡したところまでは残る)
bool DecodeRgb565(FsFile& f, const Info& info, int x, int y, int w, int h, const Options& opt,
                  Rgb565Fn fn, void* ctx);

}  // namespace ImageCodec
