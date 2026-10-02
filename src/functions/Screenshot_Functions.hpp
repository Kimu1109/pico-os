#pragma once

#include <stddef.h>

// スクリーンショット。今の画面(OSData::frame、4bppパレット)をSDへ4bppのBMPで書く。
// BMPにしたのは母艦のPCでそのまま開けるため。保存先は /screenshots/shot_0001.bmp, 0002, ...
namespace ScreenshotFunctions {

    constexpr const char* kDir = "/screenshots";

    // 撮って保存する。成功したらtrueで、保存したパスをout_pathへ入れる(失敗時は理由を入れる)。
    // SDが使えない/フレームが無い/書き込み失敗でfalse。
    bool Capture(char* out_path, size_t out_size);

    // ステータスバーのカメラのボタンを押したと見なすx座標(画面の右端のこの幅の内側)
    constexpr int kButtonWidth = 24;
}
