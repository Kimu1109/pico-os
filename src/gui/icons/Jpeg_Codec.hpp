// Jpeg_Codec.hpp
// SDの.jpgを、OSのパレット(PICO_GFX::COLORS)の16色へ寄せた4bppのスプライトへ読む(Luaの pico.image_load)。
//
// 解くのは LovyanGFX に入っている TJpgDec(lgfx_jd_prepare / lgfx_jd_decomp。ChaN氏のもの)。新しいライブラリは足していない。
// - ベースライン(順次)のJPEGだけ。プログレッシブ・算術符号は TJpgDec が断る(false)
// - 1/1・1/2・1/4・1/8 に縮めて解ける(Options::scale)。縮めるとき8x8の中を平均するので、大きな写真を小さく見せても汚くならない
// - フルカラーを16色へ寄せるので、既定で4x4の組織的ディザを掛ける(Bmp_Codec と同じ考え方・同じ近い色の選び方)
// - 作業に使うのは TJpgDec の作業場所(約3.9KB、解く間だけmalloc)だけ。出てくるのは8x8〜16x16の小さな四角ごと
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "SdFat.h"
#include <LovyanGFX.hpp>
#include "gui/icons/icon_render.h"

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

// max_w x max_h に収まる一番小さい縮め方(0〜3。1/8でも収まらなければ3)。0以下の上限は見ない
uint8_t FitScale(const Info& info, int max_w, int max_h);

struct Options {
    bool dither = true;   // 16色へ寄せるとき組織的ディザを掛ける
    uint8_t scale = 0;    // 0〜3
};

// 画像全体を新しい4bppのスプライトへ読む(ファイルの頭から)。LoadPimgToSprite()/Bmp::LoadToSprite()のJPEG版
bool LoadToSprite(FsFile& f, IconRender::PimgSprite& out, const Options& opt = Options());

}  // namespace Jpeg
