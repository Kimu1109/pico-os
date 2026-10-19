#pragma once

#include "config/LGFX_Config.hpp"
#include <LovyanGFX.h>
#include <SdFat.h>

// キーボードは Widget のポインタとして持つだけなので前方宣言で足りる
// (Widget.hpp を読むと、どこからでも使う土台の段が UI の段へ依存してしまう)
class Widget;

namespace OSData {
    //タッチ系統
    inline int touchX = 0;
    inline int touchY = 0;
    inline int touchZ = 0;
    inline bool isTouched = false;
    inline bool isTouchStart = false;
    inline bool isTouchEnd = false;
    inline bool isTouchMove = false;

    //グラフィック系統
    // lcd と SD は OS_Data.cpp で1回だけ定義する。inline にすると、作る処理(new LGFX()・SdFatの
    // コンストラクタ)と「作ったか」の確認がこのヘッダを読む .cpp の数だけフラッシュに入る
    // (2026-10-19に測ったら1つあたり数百バイト。ファイルを分けるほど増えていた)
    extern LGFX* lcd;
    inline LGFX_Sprite* frame;

    //SD系統
    inline bool SD_usable = false;
    extern SdFat SD;

    //キーボード
    inline Widget* keyboard_jpn;
    inline Widget* keyboard_eng;
    inline Widget* keyboard_num;
}