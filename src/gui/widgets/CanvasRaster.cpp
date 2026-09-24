#include "gui/widgets/CanvasRaster.hpp"
#include "OS_Data.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <algorithm>

namespace {
    //2つの矩形を両方含む最小の矩形。面積ゼロの側は無視する
    Rect UnionRect(const Rect& a, const Rect& b){
        if(a.w <= 0 || a.h <= 0) return b;
        if(b.w <= 0 || b.h <= 0) return a;
        const int16_t l = std::min(a.x, b.x);
        const int16_t t = std::min(a.y, b.y);
        const int16_t r = std::max<int16_t>(a.x + a.w, b.x + b.w);
        const int16_t btm = std::max<int16_t>(a.y + a.h, b.y + b.h);
        return { l, t, (int16_t)(r - l), (int16_t)(btm - t) };
    }

    //ドラッグで形が決まる図形か(指を離したときに焼き込み、ドラッグ中はプレビューを出す)
    bool IsShapeMode(Canvas::Mode m){
        return m == Canvas::Mode::Rect || m == Canvas::Mode::Ellipse ||
               m == Canvas::Mode::Arrow || m == Canvas::Mode::Straight;
    }
}

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
    freeUndo();
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

    //ドラッグ中の図形はスプライトへ焼き込まず、frameへ直接プレビューを重ねる。
    //キャンバスの外へはみ出さないよう、今のクリップ(FlushDirty()が掛けたdirty矩形)と
    //自分の矩形の重なりへ絞る
    if(is_pressing && IsShapeMode(mode)) {
        int32_t cx, cy, cw, ch;
        OSData::frame->getClipRect(&cx, &cy, &cw, &ch);
        const Rect clip = Rect{(int16_t)cx, (int16_t)cy, (int16_t)cw, (int16_t)ch}.intersection(g_rect);
        if(clip.w > 0 && clip.h > 0){
            OSData::frame->setClipRect(clip.x, clip.y, clip.w, clip.h);
            this->drawShape(OSData::frame, g_rect.x, g_rect.y,
                            sx, sy, relX(OSData::touchX), relY(OSData::touchY));
            OSData::frame->setClipRect(cx, cy, cw, ch);
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
        this->snapshot();
        this->strokeTo(sx, sy);
    }else if(mode == Canvas::Mode::Fill){
        const Rect filled = this->floodFill(sx, sy);
        if(filled.w > 0 && filled.h > 0){
            const Rect g_rect = this->getScreenRect();
            markdirty(Rect{(int16_t)(g_rect.x + filled.x), (int16_t)(g_rect.y + filled.y),
                           filled.w, filled.h}.intersection(g_rect));
        }
    }else{
        this->preview_rect = {0, 0, 0, 0};
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
    }else if(IsShapeMode(mode)){
        //前回のプレビュー(消す)と今回のプレビュー(描く)の範囲だけを描き直す
        const Rect g_rect = this->getScreenRect();
        const Rect b = this->shapeBounds(sx, sy, touchX, touchY);
        const Rect now = Rect{(int16_t)(g_rect.x + b.x), (int16_t)(g_rect.y + b.y), b.w, b.h}.intersection(g_rect);
        markdirty(UnionRect(this->preview_rect, now));
        this->preview_rect = now;
    }
}

void CanvasRaster::causeOnPressEnd(){
    Widget::causeOnPressEnd();

    int touchX = relX(OSData::touchX);
    int touchY = relY(OSData::touchY);

    if(mode == Canvas::Mode::Line){
        //指を離したフレームは、WidgetFunctions::UpdateAll()がupdate()より先に
        //causeOnPressEnd()を呼んでis_pressingを下ろすため、そのフレームの
        //causeOnPressMove()は来ない。最後の区間はここで繋ぐ
        if(sx != touchX || sy != touchY){
            this->strokeTo(touchX, touchY);
        }
    }else if(IsShapeMode(mode)){
        this->snapshot();
        this->drawShape(sp, 0, 0, sx, sy, touchX, touchY);

        const Rect g_rect = this->getScreenRect();
        const Rect b = this->shapeBounds(sx, sy, touchX, touchY);
        const Rect now = Rect{(int16_t)(g_rect.x + b.x), (int16_t)(g_rect.y + b.y), b.w, b.h}.intersection(g_rect);
        markdirty(UnionRect(this->preview_rect, now));
        this->preview_rect = {0, 0, 0, 0};
    }
}

void CanvasRaster::canvasClear(){
    this->snapshot();
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

    //大きさが変わったので「元に戻す」のバッファも取り直す(前の内容には戻せない)
    if(undo_buf){
        freeUndo();
        allocUndo();
    }

    this->needsRender();
}

void CanvasRaster::allocUndo(){
    undo_valid = false;
    undo_len = sp->bufferLength();
    if(undo_len == 0) return;
    undo_buf = (uint8_t*)malloc(undo_len);
    if(!undo_buf){
        LOG_SYS_WARN("CanvasRaster: 元に戻す用のバッファ(%uB)を確保できません", (unsigned)undo_len);
        undo_len = 0;
    }
}

void CanvasRaster::freeUndo(){
    free(undo_buf);
    undo_buf = nullptr;
    undo_len = 0;
    undo_valid = false;
}

void CanvasRaster::setUndoEnabled(bool enabled){
    if(enabled == (undo_buf != nullptr)) return;
    if(enabled) allocUndo();
    else freeUndo();
}

void CanvasRaster::snapshot(){
    if(!undo_buf) return;
    const void* pixels = sp->getBuffer();
    if(!pixels) return;
    memcpy(undo_buf, pixels, undo_len);
    undo_valid = true;
}

bool CanvasRaster::undo(){
    if(!undo_buf || !undo_valid) return false;
    uint8_t* pixels = (uint8_t*)sp->getBuffer();
    if(!pixels) return false;

    //入れ替えるので、もう一度呼ぶとやり直しになる(バッファを2枚持たずに済む)
    for(uint32_t i = 0; i < undo_len; i++){
        const uint8_t t = pixels[i];
        pixels[i] = undo_buf[i];
        undo_buf[i] = t;
    }
    this->needsRender();
    return true;
}

Rect CanvasRaster::shapeBounds(int x0, int y0, int x1, int y1){
    int pad = (int)lroundf(brush_radius) + 1;
    //矢じりは軸線より横へ張り出す(drawArrow()のhead_len=12・開き25度で約6px)
    if(mode == Canvas::Mode::Arrow) pad = std::max(pad, 8);
    const int l = std::min(x0, x1) - pad;
    const int t = std::min(y0, y1) - pad;
    const int r = std::max(x0, x1) + pad;
    const int b = std::max(y0, y1) + pad;
    return { (int16_t)l, (int16_t)t, (int16_t)(r - l + 1), (int16_t)(b - t + 1) };
}

void CanvasRaster::drawShape(LGFX_Sprite* target, int ox, int oy, int x0, int y0, int x1, int y1){
    const int r = (int)lroundf(brush_radius);

    switch(mode){
        case Canvas::Mode::Straight:
            DrawThickLine(target, ox + x0, oy + y0, ox + x1, oy + y1, brush_radius, brush_color);
            break;
        case Canvas::Mode::Arrow:
            this->drawArrow(target, ox + x0, oy + y0, ox + x1, oy + y1);
            break;
        case Canvas::Mode::Rect: {
            //どちらの向きへドラッグしても同じ四角形になるよう、左上/右下へ揃える
            const int l = ox + std::min(x0, x1);
            const int t = oy + std::min(y0, y1);
            const int rr = ox + std::max(x0, x1);
            const int b = oy + std::max(y0, y1);
            if(fill_shape){
                target->fillRect(l, t, rr - l + 1, b - t + 1, brush_color);
            }else if(r < 1){
                target->drawRect(l, t, rr - l + 1, b - t + 1, brush_color);
            }else{
                DrawThickLine(target, l, t, rr, t, brush_radius, brush_color);
                DrawThickLine(target, rr, t, rr, b, brush_radius, brush_color);
                DrawThickLine(target, rr, b, l, b, brush_radius, brush_color);
                DrawThickLine(target, l, b, l, t, brush_radius, brush_color);
            }
            break;
        }
        case Canvas::Mode::Ellipse: {
            const int cx = ox + (x0 + x1) / 2;
            const int cy = oy + (y0 + y1) / 2;
            const int rx = abs(x1 - x0) / 2;
            const int ry = abs(y1 - y0) / 2;
            if(fill_shape){
                target->fillEllipse(cx, cy, rx, ry, brush_color);
            }else if(r < 1){
                target->drawEllipse(cx, cy, rx, ry, brush_color);
            }else{
                //太い輪郭は周を折れ線で近似し、各辺をDrawThickLine()で塗る
                //(drawEllipse()を半径をずらして重ねると隙間が縞模様に残る)
                const float perimeter = 6.2832f * sqrtf((rx * rx + ry * ry) * 0.5f);
                const int n = std::max(12, std::min(64, (int)(perimeter / 6)));
                int px = cx + rx, py = cy;
                for(int i = 1; i <= n; i++){
                    const float a = 6.2832f * i / n;
                    const int nx = cx + (int)lroundf(rx * cosf(a));
                    const int ny = cy + (int)lroundf(ry * sinf(a));
                    DrawThickLine(target, px, py, nx, ny, brush_radius, brush_color);
                    px = nx;
                    py = ny;
                }
            }
            break;
        }
        default:
            break;
    }
}

// スキャンライン方式の塗りつぶし。種(シード)の置き場は固定長で、溢れた場合は
// 「塗った画素に隣接する、まだ塗っていない同色の画素」を全面走査して拾い直す。
// 塗ったかどうかは色では判定できない(元から塗り色だった画素と区別できない)ので、
// 1bit/画素の印を別に持つ。どちらも塗る間だけの一時確保(全面キャンバスで約7KB)
Rect CanvasRaster::floodFill(int16_t x, int16_t y){
    const int w = this->l_rect.w;
    const int h = this->l_rect.h;
    if(x < 0 || y < 0 || x >= w || y >= h) return {0, 0, 0, 0};

    const uint8_t target = (uint8_t)sp->readPixelValue(x, y);
    const uint8_t color = (uint8_t)(brush_color & 0x0F);
    if(target == color) return {0, 0, 0, 0};

    struct Seed { int16_t x, y; };
    constexpr int kMaxSeeds = 256;
    Seed* seeds = (Seed*)malloc(sizeof(Seed) * kMaxSeeds);
    uint8_t* mask = (uint8_t*)calloc(((size_t)w * h + 7) / 8, 1);
    if(!seeds || !mask){
        LOG_SYS_WARN("CanvasRaster: 塗りつぶし用の作業領域を確保できません");
        free(seeds);
        free(mask);
        return {0, 0, 0, 0};
    }

    this->snapshot();

    auto isMarked = [&](int px, int py) -> bool {
        const size_t i = (size_t)py * w + px;
        return (mask[i >> 3] >> (i & 7)) & 1;
    };
    auto mark = [&](int px, int py){
        const size_t i = (size_t)py * w + px;
        mask[i >> 3] |= (uint8_t)(1 << (i & 7));
    };
    auto isTarget = [&](int px, int py) -> bool {
        return !isMarked(px, py) && (uint8_t)sp->readPixelValue(px, py) == target;
    };

    int n = 0;
    bool overflow = false;
    auto push = [&](int px, int py){
        if(n < kMaxSeeds) seeds[n++] = { (int16_t)px, (int16_t)py };
        else overflow = true;
    };

    int min_x = x, max_x = x, min_y = y, max_y = y;
    push(x, y);

    while(true){
        while(n > 0){
            const Seed s = seeds[--n];
            if(!isTarget(s.x, s.y)) continue;

            int lx = s.x, rx = s.x;
            while(lx > 0 && isTarget(lx - 1, s.y)) lx--;
            while(rx < w - 1 && isTarget(rx + 1, s.y)) rx++;

            for(int i = lx; i <= rx; i++) mark(i, s.y);
            sp->drawFastHLine(lx, s.y, rx - lx + 1, brush_color);
            min_x = std::min(min_x, lx);
            max_x = std::max(max_x, rx);
            min_y = std::min<int>(min_y, s.y);
            max_y = std::max<int>(max_y, s.y);

            //上下の行は、塗れる画素の連なりごとに種を1つだけ置く
            for(int ny = s.y - 1; ny <= s.y + 1; ny += 2){
                if(ny < 0 || ny >= h) continue;
                bool in_run = false;
                for(int i = lx; i <= rx; i++){
                    const bool t = isTarget(i, ny);
                    if(t && !in_run) push(i, ny);
                    in_run = t;
                }
            }
        }

        if(!overflow) break;

        //種が溢れて取りこぼした分を拾い直す
        overflow = false;
        for(int py = 0; py < h && n < kMaxSeeds; py++){
            for(int px = 0; px < w && n < kMaxSeeds; px++){
                if(!isMarked(px, py)) continue;
                if(px > 0     && isTarget(px - 1, py)) push(px - 1, py);
                if(px < w - 1 && isTarget(px + 1, py)) push(px + 1, py);
                if(py > 0     && isTarget(px, py - 1)) push(px, py - 1);
                if(py < h - 1 && isTarget(px, py + 1)) push(px, py + 1);
            }
        }
        if(n == 0) break;
        //このパスで種を置き切れなかった可能性があるので、次の周回でもう一度走査する
        overflow = true;
    }

    free(seeds);
    free(mask);
    return { (int16_t)min_x, (int16_t)min_y, (int16_t)(max_x - min_x + 1), (int16_t)(max_y - min_y + 1) };
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