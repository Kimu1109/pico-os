#include "gui/widgets/apps/AnalogClock.hpp"

#include "functions/GFX_Functions.hpp"
#include "functions/Font_Functions.hpp"
#include "OS_Data.hpp"

#include <math.h>

namespace {
    constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

    //12時の位置を0°として時計回りに測った角度から、単位ベクトルを作る
    inline void angleToVec(float angle_deg, float& dx, float& dy){
        const float rad = angle_deg * kDegToRad;
        dx =  sinf(rad);
        dy = -cosf(rad); //画面のYは下が正なので、12時方向は負になる
    }

    //12/3/6/9の位置に置く数字。インデックスは「3時間ごと」の順(0=12時)
    const char* const kNumerals[4] = { "12", "3", "6", "9" };
}

int AnalogClock::radius() const {
    const int shorter = (this->l_rect.w < this->l_rect.h) ? this->l_rect.w : this->l_rect.h;
    return (shorter / 2) - 1; //枠線が矩形からはみ出さないように1px内側へ
}

void AnalogClock::setTime(int h, int m, int s){
    if(h == this->hour && m == this->minute && s == this->second) return;

    this->hour   = h;
    this->minute = m;
    this->second = s;
    this->needsRender();
}

void AnalogClock::drawHand(int cx, int cy, float angle_deg, int length, int half_width,
                           int tail, int8_t color){
    float dx, dy;
    angleToVec(angle_deg, dx, dy);

    //針の軸に対する法線(根元の幅を取る方向)
    const float nx = -dy;
    const float ny =  dx;

    const int tip_x = cx + (int)lroundf(dx * length);
    const int tip_y = cy + (int)lroundf(dy * length);

    const int base_x = cx - (int)lroundf(dx * tail);
    const int base_y = cy - (int)lroundf(dy * tail);

    const int ox = (int)lroundf(nx * half_width);
    const int oy = (int)lroundf(ny * half_width);

    OSData::frame->fillTriangle(
        tip_x, tip_y,
        base_x + ox, base_y + oy,
        base_x - ox, base_y - oy,
        color
    );
}

void AnalogClock::drawFace(int cx, int cy, int r){
    //外周(2重に描いて線を太らせる。4bitパレットでは細線が沈んで見えるため)
    OSData::frame->drawCircle(cx, cy, r, this->border_color);
    OSData::frame->drawCircle(cx, cy, r - 1, this->border_color);

    //目盛り。3時間ごとは長く太く
    for(int i = 0; i < 12; i++){
        const bool is_quarter = (i % 3 == 0);

        float dx, dy;
        angleToVec(i * 30.0f, dx, dy);

        const int outer = r - 3;
        const int inner = outer - (is_quarter ? 8 : 4);

        const int x0 = cx + (int)lroundf(dx * inner);
        const int y0 = cy + (int)lroundf(dy * inner);
        const int x1 = cx + (int)lroundf(dx * outer);
        const int y1 = cy + (int)lroundf(dy * outer);

        OSData::frame->drawLine(x0, y0, x1, y1, this->border_color);
        if(is_quarter){
            //1px横にずらした線を重ねて太らせる
            OSData::frame->drawLine(x0 + 1, y0, x1 + 1, y1, this->border_color);
            OSData::frame->drawLine(x0, y0 + 1, x1, y1 + 1, this->border_color);
        }
    }

    //12/3/6/9の数字。小さい文字盤では潰れるので、入る余地がある時だけ描く
    const int numeral_radius = r - 22;
    if(numeral_radius <= 0) return;

    FontFn::SetSmall();
    OSData::frame->setTextColor(this->border_color);

    for(int i = 0; i < 4; i++){
        float dx, dy;
        angleToVec(i * 90.0f, dx, dy);

        const char* str = kNumerals[i];
        const int str_w = OSData::frame->textWidth(str);
        const int str_h = OSData::frame->fontHeight();

        OSData::frame->setCursor(
            cx + (int)lroundf(dx * numeral_radius) - str_w / 2,
            cy + (int)lroundf(dy * numeral_radius) - str_h / 2
        );
        OSData::frame->print(str);
    }

    OSData::frame->setTextColor(PICO_BLACK);
    FontFn::SetDefault();
}

void AnalogClock::render(){
    if(!this->needs_redraw) return;
    if(!this->visible) return;

    //前回の描画内容の変更(削除)
    if(this->prev_l_rect != this->l_rect)
        markdirty(getScreenPrevRect());

    const Rect g_rect = this->getScreenRect();
    markdirty(g_rect);

    const int r = this->radius();
    if(r > 0){
        const int cx = g_rect.x + g_rect.w / 2;
        const int cy = g_rect.y + g_rect.h / 2;

        this->drawFace(cx, cy, r);

        //まだ時刻が入っていない間は針を描かない(0時0分0秒を一瞬見せないため)
        if(this->hour >= 0){
            //時針は「分」のぶんだけ進む。分針も同様に「秒」で滑らかに動かす
            const float minute_f = this->minute + this->second / 60.0f;
            const float hour_f   = (this->hour % 12) + minute_f / 60.0f;

            this->drawHand(cx, cy, hour_f * 30.0f,   r * 0.52f, 3, 6, this->hand_color);
            this->drawHand(cx, cy, minute_f * 6.0f,  r * 0.78f, 2, 8, this->hand_color);

            //秒針だけは細い線。三角形にすると先端まで色が乗らず見づらい
            float dx, dy;
            angleToVec(this->second * 6.0f, dx, dy);
            OSData::frame->drawLine(
                cx - (int)lroundf(dx * 10), cy - (int)lroundf(dy * 10),
                cx + (int)lroundf(dx * (r * 0.84f)), cy + (int)lroundf(dy * (r * 0.84f)),
                this->second_hand_color
            );
        }

        //軸
        OSData::frame->fillCircle(cx, cy, 3, this->hand_color);
        OSData::frame->fillCircle(cx, cy, 1, this->second_hand_color);
    }

    this->prev_l_rect.copy(this->getLocalRect());
    this->needs_redraw = false;
}
