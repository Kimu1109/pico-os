// ホストテスト(script/host_test/run.sh)専用のダミーヘッダ。実機ビルドでは使われない。
#pragma once
#include <vector>
#define TFT_BLACK 0
#define TFT_NAVY 1
#define TFT_DARKGREEN 2
#define TFT_DARKCYAN 3
#define TFT_MAROON 4
#define TFT_PURPLE 5
#define TFT_OLIVE 6
#define TFT_LIGHTGREY 7
#define TFT_DARKGREY 8
#define TFT_BLUE 9
#define TFT_GREEN 10
#define TFT_CYAN 11
#define TFT_RED 12
#define TFT_MAGENTA 13
#define TFT_YELLOW 14
#define TFT_WHITE 15

#include <cstdint>
#include <cstddef>
namespace lgfx { namespace v1 { struct U8g2font { U8g2font(const uint8_t* = nullptr){} }; } }
using U8g2font = lgfx::v1::U8g2font;
struct LGFX_Sprite {
    LGFX_Sprite(void* = nullptr){}
    void setColorDepth(int){}
    void* createSprite(int, int){ return nullptr; }
    void deleteSprite(){}
    void setPaletteColor(int, int){}
    void setBaseColor(int){}
    void clear(int = 0){}
    void setFont(const void*){}
    void setTextColor(int, int = 0){}
    void setTextWrap(bool, bool = false){}
    void setClipRect(int, int, int, int){}
    void clearClipRect(){}
    void fillRect(int, int, int, int, int){}
    void drawRect(int, int, int, int, int){}
    void drawLine(int, int, int, int, int){}
    void drawPixel(int, int, int){}
    void fillCircle(int, int, int, int){}
    void drawCircle(int, int, int, int){}
    void fillTriangle(int, int, int, int, int, int, int){}
    void drawFastHLine(int, int, int, int){}
    void drawFastVLine(int, int, int, int){}
    void pushSprite(void*, int, int){}
    void pushImage(int, int, int, int, const void*){}
    int textWidth(const char*){ return 0; }
    int fontHeight(){ return 0; }
    int drawString(const char*, int, int){ return 0; }
    void setCursor(int, int){}
    int print(const char*){ return 0; }
    int width(){ return 0; }
    int height(){ return 0; }
    void* getBuffer(){ return nullptr; }
};
