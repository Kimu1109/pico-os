// Jpeg_Codec.hpp
// SDの.jpgを解く。4bppへ寄せる・フルカラーのまま描くのは Image_Codec(ここは解いた四角を順に渡すだけ)。
//
// 解くのは LovyanGFX に入っている TJpgDec(lgfx_jd_prepare / lgfx_jd_decomp。ChaN氏のもの)。新しいライブラリは足していない。
// - ベースライン(順次)のJPEGだけ。プログレッシブ・算術符号は TJpgDec が断る(false)
// - 1/1・1/2・1/4・1/8 に縮めて解ける(Decode()の scale)。縮めるとき8x8の中を平均するので、大きな写真を小さく見せても汚くならない
// - 作業に使うのは TJpgDec の作業場所(約3.9KB、解く間だけmalloc)だけ。出てくるのは8x8〜16x16の小さな四角ごと
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "SdFat.h"

namespace Jpeg {

struct Info {
    uint16_t width = 0;   // 元の大きさ
    uint16_t height = 0;
};

// ファイルの先頭がJPEG(FF D8)か
bool LooksLikeJpeg(const uint8_t* head, size_t n);

// 大きさを読む(ファイルの頭から。読み終えたら位置は不定)。読めない/対応していなければfalse
bool ReadInfo(FsFile& f, Info& out);

// 縮める段階(0=等倍、1=1/2、2=1/4、3=1/8)で解いたときの大きさ
inline uint16_t ScaledSize(uint16_t v, uint8_t scale){
    return (uint16_t)((v + (1u << scale) - 1) >> scale);
}

// 縮めた後の小さな四角(8x8〜16x16)を受け取る。rgb は r,g,b の順に w*h*3 バイト(行の幅は w)。
// 四角は上の行から順(左から右)に来る。falseを返すとそこで打ち切る(もう要らない。成功として扱う)
using BlockFn = bool (*)(void* ctx, int x, int y, int w, int h, const uint8_t* rgb);

// ファイルの頭から縮めて(scale 0〜3)解き、四角を fn へ渡す。stop_y より下の四角に来たら残りを解かずに終える(-1なら最後まで)。
// 読めない・対応していない・途中で切れていたらfalse(渡したところまでは残る)
bool Decode(FsFile& f, uint8_t scale, BlockFn fn, void* ctx, int stop_y = -1);

}  // namespace Jpeg
