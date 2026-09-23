#include "gui/widgets/CanvasRaster.hpp"
#include "OS_Data.hpp"
#include "functions/GFX_Functions.hpp"
#include <cmath>
#include <algorithm>

CanvasRaster::CanvasRaster(int16_t x, int16_t y, int16_t w, int16_t h){
    this->l_rect = {x, y, w, h};

    sp = new LGFX_Sprite(OSData::frame);
    sp->setColorDepth(4);
    sp->createSprite(w, h);
    for(int i = 0; i < 16; i++){
        sp->setPaletteColor(i, PICO_GFX::COLORS[i]);
    }
    sp->setBaseColor(PICO_WHITE);
    sp->clear(PICO_WHITE);
    sp->setFont(&lgfxJapanGothicP_24);
    sp->setTextColor(PICO_BLACK);
    sp->setTextWrap(false, false);
}

CanvasRaster::~CanvasRaster(){
    if(sp){
        sp->deleteSprite(); //ピクセルバッファを先に解放する
        delete sp;
        sp = nullptr;
    }
}

void CanvasRaster::render(){
    if(!this->visible) return;
    if(!this->needs_redraw) return;

    const Rect g_rect = this->getScreenRect();

    //グローバル座標で前回位置と比較し、移動していれば旧位置を再描画対象にする
    if(this->prev_screen_rect != g_rect)
        markdirty(this->prev_screen_rect);

    sp->pushSprite(OSData::frame, g_rect.x, g_rect.y);

    if(is_pressing) {
        int touchX = relX(OSData::touchX);
        int touchY = relY(OSData::touchY);

        switch(mode){
            case Canvas::Mode::Rect:
                OSData::frame->drawRect(
                    absX(sx), absY(sy),
                    abs(touchX - sx), abs(touchY - sy),
                    this->brush_color
                );
                break;
            case Canvas::Mode::Ellipse:
                OSData::frame->drawEllipse(
                    absX((sx + touchX) * 0.5), absY((sy + touchY) * 0.5),
                    abs(sx - touchX) * 0.5, abs(sy - touchY) * 0.5,
                    this->brush_color
                );
                break;
            case Canvas::Mode::Arrow:
                this->drawArrow(OSData::frame, absX(sx), absY(sy), OSData::touchX, OSData::touchY);
                break;
        }
    }

    //移動したときだけ新しい位置もdirtyにする。移動していなければ、
    //ここへ来る前にneedsRender()(または描いた範囲だけのMarkDirty())が
    //既にdirtyを積んでいる。以前は毎回g_rectを積み直していたため、
    //needsRender()ぶんと合わせてキャンバス全体が1フレームに2回合成・転送されていた
    if(this->prev_screen_rect != g_rect)
        markdirty(g_rect);
    this->prev_screen_rect = g_rect;

    this->needs_redraw = false;
}

void CanvasRaster::causeOnPressStart(){
    Widget::causeOnPressStart();
    
    sx = relX(OSData::touchX);
    sy = relY(OSData::touchY);

    //触れた瞬間に点を打つ。以前は動いて初めて線を引いていたため、
    //短いストローク(点・読点・短い払い)が一切残らなかった
    if(mode == Canvas::Mode::Line){
        this->strokeTo(sx, sy);
    }
}

void CanvasRaster::causeOnPressMove(){
    Widget::causeOnPressMove();

    int touchX = relX(OSData::touchX);
    int touchY = relY(OSData::touchY);

    if(mode == Canvas::Mode::Line){
        //描いた線分の周りだけを合成・転送する(needsRender()だとキャンバス全体になる)
        if(sx != touchX || sy != touchY){
            this->strokeTo(touchX, touchY);
        }
    }else{
        this->needsRender();
    }
}

void CanvasRaster::causeOnPressEnd(){
    Widget::causeOnPressEnd();

    int touchX = relX(OSData::touchX);
    int touchY = relY(OSData::touchY);

    switch (mode)
    {
        case Canvas::Mode::Line:
            //指を離したフレームは、WidgetFunctions::UpdateAll()がupdate()より先に
            //causeOnPressEnd()を呼んでis_pressingを下ろすため、そのフレームの
            //causeOnPressMove()は来ない。最後の区間はここで繋ぐ
            if(sx != touchX || sy != touchY){
                this->strokeTo(touchX, touchY);
            }
            break;
        case Canvas::Mode::Rect:
            sp->drawRect(
                sx, sy,
                abs(touchX - sx), abs(touchY - sy),
                this->brush_color
            );
            this->needsRender();
            break;
        case Canvas::Mode::Ellipse:
            sp->drawEllipse(
                (sx + touchX) * 0.5, (sy + touchY) * 0.5,
                abs(sx - touchX) * 0.5, abs(sy - touchY) * 0.5,
                this->brush_color
            );
            this->needsRender();
            break;
        case Canvas::Mode::Arrow:
            this->drawArrow(sp, sx, sy, touchX, touchY);
            this->needsRender();
            break;
        default:
            break;
    }
}

void CanvasRaster::canvasClear(){
    sp->clear(PICO_WHITE);
    this->needsRender();
}

void CanvasRaster::resize(int16_t w, int16_t h){
    if(w == this->l_rect.w && h == this->l_rect.h) return;
    if(w <= 0 || h <= 0) return;

    this->l_rect.w = w;
    this->l_rect.h = h;

    // createSprite()を同じspriteへ呼び直すと、内部が古いバッファを解放してから
    // 新しいサイズで確保し直す(コンストラクタと同じ手順を丸ごとやり直す必要がある —
    // パレット/基準色/フォント設定はスプライトを作り直すたびに失われるため)
    sp->createSprite(w, h);
    for(int i = 0; i < 16; i++){
        sp->setPaletteColor(i, PICO_GFX::COLORS[i]);
    }
    sp->setBaseColor(PICO_WHITE);
    sp->clear(PICO_WHITE);
    sp->setFont(&lgfxJapanGothicP_24);
    sp->setTextColor(PICO_BLACK);
    sp->setTextWrap(false, false);

    this->needsRender();
}

void CanvasRaster::strokeTo(int16_t x, int16_t y){
    DrawThickLine(sp, sx, sy, x, y, brush_radius, brush_color);

    //線分の外接矩形(太さぶん広げる)だけをdirtyにする。スプライト座標→画面座標
    const int16_t r = (int16_t)lroundf(brush_radius) + 1;
    const int16_t x0 = std::min(sx, x) - r;
    const int16_t y0 = std::min(sy, y) - r;
    const int16_t x1 = std::max(sx, x) + r;
    const int16_t y1 = std::max(sy, y) + r;
    const Rect g_rect = this->getScreenRect();
    const Rect stroke{
        (int16_t)(g_rect.x + x0), (int16_t)(g_rect.y + y0),
        (int16_t)(x1 - x0 + 1), (int16_t)(y1 - y0 + 1)
    };
    markdirty(stroke.intersection(g_rect));

    sx = x;
    sy = y;
}

// 太さのある線分を「両端の円 + 胴体の四角形(三角形2枚)」で塗る。
// drawWideLine()は使わない: アンチエイリアスのためreadRect()で既存ピクセルを読み戻して
// アルファ合成する経路を通り、4bppパレットのスプライトでは遅い上にPCビルドではSEGVする
// (AnalogClockの針をfillTriangle()で描いているのと同じ理由)。
// fillCircle()/fillTriangle()は単純な塗りつぶしだけで済む
void CanvasRaster::DrawThickLine(LGFX_Sprite* canvas, int x0, int y0, int x1, int y1, float radius, int8_t color){
    const int r = (int)lroundf(radius);
    if(r < 1){
        canvas->drawLine(x0, y0, x1, y1, color);
        return;
    }

    canvas->fillCircle(x0, y0, r, color);
    if(x0 == x1 && y0 == y1) return;
    canvas->fillCircle(x1, y1, r, color);

    const float dx = (float)(x1 - x0);
    const float dy = (float)(y1 - y0);
    const float len = sqrtf(dx * dx + dy * dy);
    //進行方向に垂直で長さrのベクトル
    const int px = (int)lroundf(-dy / len * r);
    const int py = (int)lroundf( dx / len * r);

    canvas->fillTriangle(x0 + px, y0 + py, x1 + px, y1 + py, x1 - px, y1 - py, color);
    canvas->fillTriangle(x0 + px, y0 + py, x1 - px, y1 - py, x0 - px, y0 - py, color);
}

// 矢印描画: 始点(x0,y0) → 終点(x1,y1)、先端は三角形の矢じり
// canvas       : 描画先スプライト(LGFX_Sprite)
// head_len     : 矢じりの長さ(px)
// head_angle_deg: 矢じりの開き角度(軸線からの片側角度、度)
void CanvasRaster::drawArrow(LGFX_Sprite *canvas, int x0, int y0, int x1, int y1)
{
    const float head_len = 12.0f;
    const float head_angle_deg = 25.0f;

    float dx = x1 - x0;
    float dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1e-3f) return; // 始点=終点は描画しない

    float ux = dx / len;
    float uy = dy / len;

    // 矢印全長より矢じりが長い場合はclamp(潰れ防止)
    float hl = (head_len > len) ? len : head_len;

    // 矢じり根元の座標(軸線の終端はここまで)
    float bx = x1 - ux * hl;
    float by = y1 - uy * hl;

    DrawThickLine(canvas, x0, y0, lroundf(bx), lroundf(by), this->brush_radius, this->brush_color);

    // --- 矢じり(三角形) ---
    float rad = head_angle_deg * (float)M_PI / 180.0f;
    float perp_x = -uy; // 進行方向に垂直な単位ベクトル
    float perp_y =  ux;
    float spread = hl * tanf(rad);

    float bax = bx + perp_x * spread;
    float bay = by + perp_y * spread;
    float bbx = bx - perp_x * spread;
    float bby = by - perp_y * spread;

    canvas->fillTriangle(lroundf(x1), lroundf(y1),
                         lroundf(bax), lroundf(bay),
                         lroundf(bbx), lroundf(bby),
                         this->brush_color);
}