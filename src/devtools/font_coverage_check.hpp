#pragma once
//
// font_coverage_check.hpp
//
// skk_body.tsv内の全候補文字を走査し、指定フォント(LovyanGFXのIFont)に
// グリフが存在しない文字を洗い出してSDに書き出すデバッグ用ツール。
//
// 想定用途: 常時実行する機能ではなく、辞書やフォントを更新したときに
// 一度だけ(例: setup()から一時的に呼ぶ、またはデバッグメニューから)
// 実行して missing_chars.txt を生成し、それをPCに持って行って
// convert_skk_dict.py --exclude-chars-file で辞書側をフィルタする運用。
//

#include <LovyanGFX.hpp>
#include <SdFat.h>

// sd: 初期化済みのSdFatインスタンス
// font: チェック対象のフォント(例: fonts::lgfxJapanGothicP_16)
// dictBodyPath: skk_body.tsv のパス(convert_skk_dict.pyの出力)
// outputPath: 結果を書き出すファイルパス(例: "/dic/missing_chars.txt")
//
// 出力フォーマット(1行1文字):
//   U+XXXX<TAB>実際の文字(UTF-8)
// ただしBMP範囲外(U+10000以上)の文字は「未対応扱い」として
//   U+XXXXXX (BMP範囲外のため未対応扱い)
// の形式で出力される(u8g2フォントAPIが16bitコードまでしか
// 扱えないための制約)。
void checkFontCoverage(SdFat &sd, const lgfx::IFont &font,
                        const char* dictBodyPath, const char* outputPath);
