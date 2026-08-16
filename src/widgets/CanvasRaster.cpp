#include "widgets/CanvasRaster.hpp"
#include "OS_Data.hpp"
#include "functions/GFX_Functions.hpp"
#include <cmath>

CanvasRaster::CanvasRaster(int16_t x, int16_t y, int16_t w, int16_t h){
    this->rect = {x, y, w, h};

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

void CanvasRaster::render(){
    if(!this->visible) return;
    if(!this->needs_redraw) return;

    sp->pushSprite(OSData::frame, this->rect.x, this->rect.y);

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

    PICO_GFX::markDirty(this->rect);

    this->needs_redraw = false;
}

void CanvasRaster::onPressStart(){
    if(this->on_press_start) this->on_press_start();
    
    sx = relX(OSData::touchX);
    sy = relY(OSData::touchY);
}

void CanvasRaster::onPressMove(){
    if(this->on_press_move) this->on_press_move();

    int touchX = relX(OSData::touchX);
    int touchY = relY(OSData::touchY);

    if(mode == Canvas::Mode::Line){
        if(
            abs(sx - touchX) >= 2 ||
            abs(sy - touchY) >= 2
        ){
            sp->drawWideLine(
                sx,
                sy,
                touchX,
                touchY,
                this->brush_radius,
                this->brush_color
            );
            sx = touchX;
            sy = touchY;
            this->needsRender();
        }
    }else{
        this->needsRender();
    }
}

void CanvasRaster::onPressEnd(){
    int touchX = relX(OSData::touchX);
    int touchY = relY(OSData::touchY);

    switch (mode)
    {
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

void CanvasRaster::CanvasClear(){
    sp->clear(PICO_WHITE);
    this->needsRender();
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

    canvas->drawWideLine(x0, y0, bx, by, this->brush_radius, this->brush_color);

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