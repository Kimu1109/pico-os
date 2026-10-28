// Png_Codec.hpp
// SDの.pngを解く。4bppへ寄せる・フルカラーのまま描くのは Image_Codec(ここは画素を順に渡すだけ)。
//
// 解くのは LovyanGFX に入っている pngle(lgfx_pngle_*。kikuchan氏のもの、展開は同梱の miniz の tinfl)。新しいライブラリは足していない。
// - 色の種類は全部(グレー・パレット・RGB・アルファ付き、1〜16bit、tRNS)。インターレース(Adam7)も読める
// - 1/2・1/4・1/8 に縮めて解ける(Jpeg と同じ段階)。インターレースでない絵は縮めるとき四角の中を平均する
//   (インターレースの絵は四角の左上の1画素を取る。画素が飛び飛びに届くため)
// - 作業に使うのは pngle の作業場所(約45KB = 展開の辞書32KB + 展開器11KB + 1行ぶん。解く間だけmalloc)と、
//   縮めるときの1行ぶんの足し算の場所(縮めた後の幅 x 8B)。解き終えたら全部返す
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "SdFat.h"

namespace Png {

struct Info {
    uint16_t width = 0;   // 元の大きさ
    uint16_t height = 0;
    uint8_t color_type = 0;   // PNGの色の種類(0=グレー 2=RGB 3=パレット 4=グレー+α 6=RGB+α)
    uint8_t depth = 0;
    bool interlaced = false;
    bool alpha = false;       // アルファの列を持つ(4/6)。tRNSは解くまで分からないので入れない
};

// ファイルの先頭がPNG(89 50 4E 47 0D 0A 1A 0A)か
bool LooksLikePng(const uint8_t* head, size_t n);

// 大きさと色の種類を読む(ファイルの頭のIHDRだけ。作業場所は取らない)。読めなければfalse
bool ReadInfo(FsFile& f, Info& out);

// 縮めたときの大きさ(Jpeg::ScaledSize と同じ)
inline uint16_t ScaledSize(uint16_t v, uint8_t scale){
    return (uint16_t)((v + (1u << scale) - 1) >> scale);
}

// 縮めた後の画素を左から n 個ぶん受け取る(rgba は r,g,b,a の順に 4n バイト)。
// y が上から順に来るとは限らない(インターレースの絵)。falseを返すとそこで打ち切る(もう要らない。成功として扱う)
using PixelFn = bool (*)(void* ctx, int x, int y, int n, const uint8_t* rgba);

// ファイルの頭から解いて、縮めた後(scale 0〜3)の画素を fn へ渡す。
// stop_y 以上の行を渡し終えたら(インターレースでない絵は)残りを読まずに終える。-1なら最後まで。
// 途中で読めなくなった・壊れていた・作業場所を取れなかったらfalse(渡したところまでは残る)
bool Decode(FsFile& f, uint8_t scale, PixelFn fn, void* ctx, int stop_y = -1);

}  // namespace Png
