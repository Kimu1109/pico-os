#include "gui/widgets/apps/GraphView.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Font_Functions.hpp"
#include "OS_Data.hpp"

#include <cmath>
#include <cstdio>

const int8_t GraphView::kColors[GraphView::kMaxFunctions] = { PICO_RED, PICO_BLUE, PICO_DARKGREEN };

namespace {
    // タップとドラッグを見分ける距離(px)
    constexpr int kDragThreshold = 4;

    // 範囲を5〜10目盛りに分ける「きりのいい」間隔(1, 2, 5 × 10^n)
    double NiceStep(double range){
        if(!(range > 0.0)) return 1.0;
        const double raw = range / 8.0;
        const double mag = std::pow(10.0, std::floor(std::log10(raw)));
        const double n = raw / mag;
        if(n < 1.5) return mag;
        if(n < 3.5) return 2.0 * mag;
        if(n < 7.5) return 5.0 * mag;
        return 10.0 * mag;
    }

    // 目盛りの数字(短く。0.30000000000000004 を 0.3 にする)
    void FormatTick(double v, char* out, size_t n){
        if(std::fabs(v) < 1e-12) v = 0.0;
        snprintf(out, n, "%.4g", v);
    }

    void FormatValue(double v, char* out, size_t n){
        if(std::fabs(v) < 1e-12) v = 0.0;
        snprintf(out, n, "%.6g", v);
    }

    // 画面外の大きな座標をdrawLine()へ渡しても桁あふれしないように丸める
    int ClampCoord(double v){
        if(v < -4000.0) return -4000;
        if(v > 4000.0) return 4000;
        return (int)std::lround(v);
    }
}

GraphView::GraphView(int x, int y, int w, int h){
    if(w > kMaxColumns) w = kMaxColumns;
    this->l_rect = { (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h };
    this->resetView();
}

void GraphView::setFunction(int i, const char* expr){
    if(i < 0 || i >= kMaxFunctions) return;
    this->exprs[i] = expr;
    this->changed();
}

void GraphView::resetView(){
    // 縦横の目盛りの間隔を揃える(円が円に見える)。横は-10〜10、縦は画面の比で決める
    const int w = this->l_rect.w > 0 ? this->l_rect.w : 1;
    const int h = this->l_rect.h > 0 ? this->l_rect.h : 1;
    const double half_w = 10.0;
    const double half_h = half_w * h / w;
    this->setView(-half_w, half_w, -half_h, half_h);
}

void GraphView::setView(double xmin, double xmax, double ymin, double ymax){
    // 細かすぎ/粗すぎで計算が壊れないよう、範囲の幅を1e-6〜1e8に収める
    auto valid = [](double lo, double hi){ return std::isfinite(lo) && std::isfinite(hi) && (hi - lo) >= 1e-6 && (hi - lo) <= 1e8; };
    if(!valid(xmin, xmax) || !valid(ymin, ymax)) return;
    this->x_min = xmin; this->x_max = xmax;
    this->y_min = ymin; this->y_max = ymax;
    this->changed();
}

void GraphView::zoom(double factor){
    const double cx = (this->x_min + this->x_max) / 2.0;
    const double cy = (this->y_min + this->y_max) / 2.0;
    const double hw = (this->x_max - this->x_min) / 2.0 * factor;
    const double hh = (this->y_max - this->y_min) / 2.0 * factor;
    this->setView(cx - hw, cx + hw, cy - hh, cy + hh);
}

void GraphView::pan(double fx, double fy){
    const double dx = (this->x_max - this->x_min) * fx;
    const double dy = (this->y_max - this->y_min) * fy;
    this->setView(this->x_min + dx, this->x_max + dx, this->y_min + dy, this->y_max + dy);
}

double GraphView::pixelToX(double px) const {
    return this->x_min + (this->x_max - this->x_min) * px / (double)this->l_rect.w;
}
double GraphView::pixelToY(double py) const {
    return this->y_max - (this->y_max - this->y_min) * py / (double)this->l_rect.h;
}
double GraphView::xToPixel(double x) const {
    return (x - this->x_min) / (this->x_max - this->x_min) * (double)this->l_rect.w;
}
double GraphView::yToPixel(double y) const {
    return (this->y_max - y) / (this->y_max - this->y_min) * (double)this->l_rect.h;
}

void GraphView::computeSamples(){
    const int w = this->l_rect.w;
    CalcEval::Context c = this->ctx;
    c.has_x = true;
    for(int i = 0; i < kMaxFunctions; i++){
        const char* e = this->exprs[i];
        const bool active = e && e[0] != '\0';
        for(int col = 0; col < w; col++){
            if(!active){ this->samples[i][col] = NAN; continue; }
            // 画素の中心のx
            c.x = this->pixelToX(col + 0.5);
            const CalcEval::Result r = CalcEval::Evaluate(e, c);
            this->samples[i][col] = r.ok() ? (float)r.value : NAN;
        }
    }
    this->samples_valid = true;
}

float GraphView::sampleAt(int i, int col){
    if(i < 0 || i >= kMaxFunctions || col < 0 || col >= this->l_rect.w) return NAN;
    if(!this->samples_valid) this->computeSamples();
    return this->samples[i][col];
}

void GraphView::drawGrid(const Rect& g){
    LGFX_Sprite* f = OSData::frame;

    const double xs = NiceStep(this->x_max - this->x_min);
    const double ys = NiceStep(this->y_max - this->y_min);

    // 目盛りの線(薄い灰色)
    for(double v = std::ceil(this->x_min / xs) * xs; v <= this->x_max; v += xs){
        const int px = g.x + (int)std::lround(this->xToPixel(v));
        f->drawFastVLine(px, g.y, g.h, PICO_LIGHTGREY);
    }
    for(double v = std::ceil(this->y_min / ys) * ys; v <= this->y_max; v += ys){
        const int py = g.y + (int)std::lround(this->yToPixel(v));
        f->drawFastHLine(g.x, py, g.w, PICO_LIGHTGREY);
    }

    // 軸(画面の外なら端に寄せて数字だけ出す)
    const double ax = this->yToPixel(0.0);
    const double ay = this->xToPixel(0.0);
    const bool x_axis_in = ax >= 0 && ax < g.h;
    const bool y_axis_in = ay >= 0 && ay < g.w;
    if(x_axis_in) f->drawFastHLine(g.x, g.y + (int)std::lround(ax), g.w, PICO_BLACK);
    if(y_axis_in) f->drawFastVLine(g.x + (int)std::lround(ay), g.y, g.h, PICO_BLACK);

    // 目盛りの数字(Font0。6x8)
    f->setFont(&fonts::Font0);
    f->setTextSize(1);
    f->setTextColor(PICO_DARKGREY);
    char buf[16];

    int label_y = x_axis_in ? (int)std::lround(ax) + 2 : g.h - 9;
    if(label_y > g.h - 9) label_y = (int)std::lround(ax) - 9;
    for(double v = std::ceil(this->x_min / xs) * xs; v <= this->x_max; v += xs){
        if(std::fabs(v) < xs * 1e-6) continue; // 原点は縦の方で出す
        FormatTick(v, buf, sizeof(buf));
        const int px = (int)std::lround(this->xToPixel(v));
        const int tw = f->textWidth(buf);
        int tx = px - tw / 2;
        if(tx < 1 || tx + tw > g.w - 1) continue;
        f->drawString(buf, g.x + tx, g.y + label_y);
    }

    int label_x_right = y_axis_in ? (int)std::lround(ay) - 2 : -1; // 軸の左に右揃え
    for(double v = std::ceil(this->y_min / ys) * ys; v <= this->y_max; v += ys){
        FormatTick(v, buf, sizeof(buf));
        const int py = (int)std::lround(this->yToPixel(v));
        if(py < 5 || py > g.h - 5) continue;
        const int tw = f->textWidth(buf);
        int tx = label_x_right >= 0 ? label_x_right - tw : 2;
        if(tx < 2) tx = (y_axis_in ? (int)std::lround(ay) + 3 : 2);
        f->drawString(buf, g.x + tx, g.y + py - 4);
    }
}

void GraphView::drawCurves(const Rect& g){
    LGFX_Sprite* f = OSData::frame;
    const double h = g.h;

    for(int i = 0; i < kMaxFunctions; i++){
        bool has_prev = false;
        double prev_py = 0.0;
        for(int col = 0; col < g.w; col++){
            const float v = this->samples[i][col];
            if(!std::isfinite(v)){ has_prev = false; continue; }
            const double py = this->yToPixel(v);
            if(has_prev){
                // 漸近線(tan等)をまたぐ縦線を引かない: 画面の外どうしを大きく跳んでいたら繋がない
                const bool jump = std::fabs(py - prev_py) > h &&
                                  ((prev_py < 0 && py > h) || (prev_py > h && py < 0));
                if(!jump){
                    f->drawLine(g.x + col - 1, g.y + ClampCoord(prev_py),
                                g.x + col,     g.y + ClampCoord(py), kColors[i]);
                }
            }else{
                f->drawPixel(g.x + col, g.y + ClampCoord(py), kColors[i]);
            }
            prev_py = py;
            has_prev = true;
        }
    }
}

void GraphView::drawTrace(const Rect& g){
    LGFX_Sprite* f = OSData::frame;

    const double px = this->xToPixel(this->trace_x);
    if(px >= 0 && px < g.w){
        const int sx = g.x + (int)std::lround(px);
        for(int y = g.y; y < g.y + g.h; y += 4) f->drawFastVLine(sx, y, 2, PICO_DARKGREY);
    }

    CalcEval::Context c = this->ctx;
    c.has_x = true;
    c.x = this->trace_x;

    f->setFont(&fonts::Font0);
    f->setTextSize(1);

    char num[24], line[40];
    FormatValue(this->trace_x, num, sizeof(num));
    snprintf(line, sizeof(line), "x=%s", num);

    // 値の欄は白地に枠(曲線の上でも読めるように)
    int lines = 1;
    for(int i = 0; i < kMaxFunctions; i++) if(this->exprs[i] && this->exprs[i][0]) lines++;
    const int box_w = 6 * 18 + 6;
    const int box_h = lines * 10 + 4;
    f->fillRect(g.x + 2, g.y + 2, box_w, box_h, PICO_WHITE);
    f->drawRect(g.x + 2, g.y + 2, box_w, box_h, PICO_BLACK);

    int ty = g.y + 5;
    f->setTextColor(PICO_BLACK);
    f->drawString(line, g.x + 5, ty);
    ty += 10;

    for(int i = 0; i < kMaxFunctions; i++){
        const char* e = this->exprs[i];
        if(!e || !e[0]) continue;
        const CalcEval::Result r = CalcEval::Evaluate(e, c);
        if(r.ok()){
            FormatValue(r.value, num, sizeof(num));
            snprintf(line, sizeof(line), "y%d=%s", i + 1, num);
            const double py = this->yToPixel(r.value);
            if(px >= 0 && px < g.w && py >= 0 && py < g.h){
                f->fillCircle(g.x + (int)std::lround(px), g.y + (int)std::lround(py), 2, kColors[i]);
            }
        }else{
            snprintf(line, sizeof(line), "y%d=---", i + 1);
        }
        f->setTextColor(kColors[i]);
        f->drawString(line, g.x + 5, ty);
        ty += 10;
    }
}

void GraphView::causeOnPressStart(){
    Widget::causeOnPressStart();
    this->press_x = this->last_x = OSData::touchX;
    this->press_y = this->last_y = OSData::touchY;
    this->dragged = false;
}

void GraphView::causeOnPressMove(){
    Widget::causeOnPressMove();
    const int16_t tx = OSData::touchX, ty = OSData::touchY;
    if(!this->dragged){
        if(std::abs(tx - this->press_x) < kDragThreshold && std::abs(ty - this->press_y) < kDragThreshold) return;
        this->dragged = true;
    }
    const int dx = tx - this->last_x;
    const int dy = ty - this->last_y;
    if(dx == 0 && dy == 0) return;
    this->last_x = tx;
    this->last_y = ty;

    const double ux = (this->x_max - this->x_min) / this->l_rect.w;
    const double uy = (this->y_max - this->y_min) / this->l_rect.h;
    this->setView(this->x_min - dx * ux, this->x_max - dx * ux,
                  this->y_min + dy * uy, this->y_max + dy * uy);
}

void GraphView::causeOnPressEnd(){
    Widget::causeOnPressEnd();
    if(this->dragged) return;
    // タップ: その列のxでトレース
    const Rect g = this->getScreenRect();
    const int col = OSData::touchX - g.x;
    if(col < 0 || col >= g.w) return;
    this->trace_on = true;
    this->trace_x = this->pixelToX(col + 0.5);
    this->needsRender();
}

void GraphView::render(){
    if(!this->visible) return;

    // UpdateAll()から来た場合: 描かずにdirtyだけ積む(描くのはFlushDirty()の中)
    if(!PICO_GFX::isDirtyDeactivates){
        if(this->needs_redraw){
            if(this->prev_l_rect != this->l_rect) this->markdirty(this->getScreenPrevRect());
            this->markdirty(this->getScreenRect());
            this->prev_l_rect = this->l_rect;
            this->needs_redraw = false;
        }
        return;
    }
    this->needs_redraw = false;

    if(!this->samples_valid) this->computeSamples();

    const Rect g = this->getScreenRect();
    // 背景はOPAQUEなのでFlushDirty()が塗ってある
    this->drawGrid(g);
    this->drawCurves(g);
    if(this->trace_on) this->drawTrace(g);
    OSData::frame->drawRect(g.x, g.y, g.w, g.h, PICO_BLACK);

    OSData::frame->setTextColor(PICO_BLACK);
    FontFn::SetDefault();
}
