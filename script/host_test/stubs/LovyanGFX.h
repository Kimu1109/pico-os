// ホストテスト(script/host_test/*.sh)専用のダミーヘッダ。実機ビルドでは使われない。
//
// 描画そのものは行わないが、textWidth()/fontHeight()だけは実寸に近い値を返す。
// Labelの折り返し(relayout)はこの2つの戻り値で分岐するため、0を返すと
// 「常に1行」になってしまい、確保パターンの計測(mem_probe)が実機とかけ離れてしまう。
#pragma once
#include <vector>
#include <algorithm>
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
namespace lgfx { namespace v1 { struct U8g2font {
    int px = 24;
    U8g2font(const uint8_t* = nullptr){}
    explicit U8g2font(int px) : px(px) {}
}; } }
using U8g2font = lgfx::v1::U8g2font;

//実機と同じ名前でフォント実体を用意する(Font_Functions.cppがこの名前を参照する)
inline const lgfx::v1::U8g2font lgfxJapanGothicP_16{16};
inline const lgfx::v1::U8g2font lgfxJapanGothicP_24{24};

struct LGFX_Sprite {
    int font_px = 24;   //現在のフォントの1文字高(px)
    int text_size = 1;  //拡大率

    // writePixel/readPixelValueだけは実際にバッファへ読み書きする(1byte/pixelの
    // 簡略版。実機/PCビルドは4bppだが、ここではパレット番号0〜15を素直に格納できれば
    // 十分)。IconRender::EncodePimg/DecodePimgBody(pico.canvas_save/canvas_load)を
    // ホストテストで検証するために追加した。他のメソッドは元々どおり無描画のまま
    int sp_w_ = 0, sp_h_ = 0;
    std::vector<uint8_t> pixels_;

    LGFX_Sprite(void* = nullptr){}
    void setColorDepth(int){}
    void* createSprite(int w, int h){
        sp_w_ = (w > 0) ? w : 0;
        sp_h_ = (h > 0) ? h : 0;
        pixels_.assign((size_t)sp_w_ * (size_t)sp_h_, 0);
        return pixels_.empty() ? nullptr : pixels_.data();
    }
    void deleteSprite(){ pixels_.clear(); sp_w_ = 0; sp_h_ = 0; }
    void setPaletteColor(int, int){}
    void setBaseColor(int){}
    void clear(int color = 0){ std::fill(pixels_.begin(), pixels_.end(), (uint8_t)color); }
    void setFont(const void* font){
        if(font) font_px = ((const lgfx::v1::U8g2font*)font)->px;
    }
    void setTextSize(int size){ text_size = (size > 0) ? size : 1; }
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
    void drawEllipse(int, int, int, int, int){}
    void fillEllipse(int, int, int, int, int){}
    void drawWideLine(int, int, int, int, float, int){}
    void getClipRect(int32_t* x, int32_t* y, int32_t* w, int32_t* h){
        if(x) *x = 0; if(y) *y = 0; if(w) *w = 0; if(h) *h = 0;
    }
    void fillTriangle(int, int, int, int, int, int, int){}
    void drawFastHLine(int, int, int, int){}
    void drawFastVLine(int, int, int, int){}
    void pushSprite(void*, int, int){}
    void pushSprite(void*, int, int, int){} //透過色つき
    void writePixel(int x, int y, int color){
        if(x < 0 || y < 0 || x >= sp_w_ || y >= sp_h_) return;
        pixels_[(size_t)y * sp_w_ + (size_t)x] = (uint8_t)color;
    }
    uint32_t readPixelValue(int x, int y){
        if(x < 0 || y < 0 || x >= sp_w_ || y >= sp_h_) return 0;
        return pixels_[(size_t)y * sp_w_ + (size_t)x];
    }
    void startWrite(){}
    void endWrite(){}
    void pushImage(int, int, int, int, const void*){}

    //ASCIIは半角(fontHeightの半分)、UTF-8マルチバイト文字は全角として概算する
    int textWidth(const char* s){
        if(!s) return 0;
        const int full = font_px * text_size;
        int width = 0;
        for(const unsigned char* p = (const unsigned char*)s; *p; p++){
            if((*p & 0xC0) == 0x80) continue; //継続バイトは数えない
            width += (*p < 0x80) ? (full / 2) : full;
        }
        return width;
    }
    int fontHeight(){ return font_px * text_size; }
    int drawString(const char*, int, int){ return 0; }
    void setCursor(int, int){}
    int print(const char*){ return 0; }
    int width(){ return sp_w_; }
    int height(){ return sp_h_; }
    void* getBuffer(){ return pixels_.empty() ? nullptr : pixels_.data(); }
};
