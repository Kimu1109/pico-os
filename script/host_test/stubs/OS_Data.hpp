// ホストテスト用のOS_Data差し替えスタブ(実機ビルドには使わない)
#pragma once
#include <LovyanGFX.h>
#include <SdFat.h>
#include "gui/widgets/Widget.hpp"
struct LGFX { void init(){} void setBaseColor(int){} void clear(int){} void setFont(const void*){}
    void setTextColor(int){} void setClipRect(int,int,int,int){} void clearClipRect(){} };
namespace OSData {
    inline int touchX = 0, touchY = 0, touchZ = 0;
    inline bool isTouched = false, isTouchStart = false, isTouchEnd = false, isTouchMove = false;
    inline LGFX* lcd = nullptr;
    inline LGFX_Sprite* frame = new LGFX_Sprite();
    inline bool SD_usable = false;
    inline SdFat SD;
    inline Widget* keyboard_jpn = nullptr;
    inline Widget* keyboard_eng = nullptr;
    inline Widget* keyboard_num = nullptr;
}
