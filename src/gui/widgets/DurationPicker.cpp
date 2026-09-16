#include "gui/widgets/DurationPicker.hpp"

#include "functions/GFX_Functions.hpp"
#include "OS_Data.hpp"
#include "Arduino.h"

#include <cstdio>

uint32_t DurationPicker::displaySeconds() const {
    //切り上げ。残り0.4秒を00:00:00と出すと、まだ鳴っていないのに終わったように見える
    uint32_t sec = (this->total_ms + 999u) / 1000u;
    if(sec > kMaxSeconds) sec = kMaxSeconds;
    return sec;
}

void DurationPicker::setTotalMs(uint32_t ms){
    if(ms > kMaxMs) ms = kMaxMs;
    if(ms == this->total_ms) return;

    const uint32_t before = this->displaySeconds();
    this->total_ms = ms;

    //描いているのは秒までなので、秒が変わらないミリ秒の刻みでは描き直さない
    //(カウントダウン中は毎フレームここへ来る)
    if(this->displaySeconds() != before) this->needsRender();
}

void DurationPicker::setEditable(bool v){
    if(v == this->editable) return;
    this->editable = v;
    this->repeat_field = -1;
    this->needsRender();
}

DurationPicker::Layout DurationPicker::computeLayout(){
    Layout lay = {};

    const Rect g_rect = this->getScreenRect();

    this->fontApply();
    const int digit_w = OSData::frame->textWidth("00");
    const int colon_w = OSData::frame->textWidth(":");
    const int digit_h = OSData::frame->fontHeight();
    this->fontDefault();

    lay.col_w    = digit_w;
    lay.colon_w  = colon_w;
    lay.digits_h = digit_h;

    const int total_w = digit_w * 3 + colon_w * 2;
    const int left = g_rect.x + (g_rect.w - total_w) / 2;
    for(int i = 0; i < 3; i++) lay.col_x[i] = left + i * (digit_w + colon_w);

    //▲▼を出していないときはそのぶん詰めて、数字が矩形の中央に来るようにする
    const int arrow_block = this->editable ? (kArrowH + kArrowGap) : 0;
    const int block_h = arrow_block * 2 + digit_h;
    const int top = g_rect.y + (g_rect.h - block_h) / 2;

    lay.arrow_up_y   = top;
    lay.digits_y     = top + arrow_block;
    lay.arrow_down_y = lay.digits_y + digit_h + kArrowGap;

    return lay;
}

int DurationPicker::hitField(int px, int py, int& step_out){
    step_out = 0;
    if(!this->editable) return -1;

    const Layout lay = this->computeLayout();

    if(py >= lay.arrow_up_y - kTouchPad && py < lay.digits_y){
        step_out = 1;
    }else if(py >= lay.digits_y + lay.digits_h &&
             py < lay.arrow_down_y + kArrowH + kTouchPad){
        step_out = -1;
    }else{
        return -1;
    }

    //桁の間(コロン)は左右どちらかへ寄せて拾う。指の太さに対して2桁ぶんの幅は狭い
    for(int i = 0; i < 3; i++){
        const int x0 = lay.col_x[i] - lay.colon_w / 2;
        const int x1 = lay.col_x[i] + lay.col_w + lay.colon_w / 2;
        if(px >= x0 && px < x1) return i;
    }
    return -1;
}

void DurationPicker::applyStep(int field, int step){
    if(!this->editable) return;

    const uint32_t sec = this->total_ms / 1000u;
    int h = (int)(sec / 3600u);
    int m = (int)((sec / 60u) % 60u);
    int s = (int)(sec % 60u);

    //繰り上がりはさせずその桁だけを巡回させる。
    //「59分から1つ進めたら0分」の方が、時が勝手に増えるより設定しやすい
    switch(field){
        case 0: h = (h + step + 24) % 24; break;
        case 1: m = (m + step + 60) % 60; break;
        case 2: s = (s + step + 60) % 60; break;
        default: return;
    }

    const uint32_t next = (uint32_t)(h * 3600 + m * 60 + s) * 1000u;
    if(next == this->total_ms) return;

    this->total_ms = next;
    this->needsRender();

    if(this->on_changed) this->on_changed(this->total_ms);
}

void DurationPicker::tickRepeat(){
    if(this->repeat_field < 0) return;

    //指が離れていれば止める(causeOnPressEnd()が来ない経路への保険)
    if(!this->is_pressing){
        this->repeat_field = -1;
        return;
    }

    const unsigned long now = millis();
    if(now - this->repeat_started_ms < kRepeatDelayMs) return;
    if(now - this->repeat_last_ms < kRepeatIntervalMs) return;

    //押したまま▲▼の外へずらしたら止める
    int step = 0;
    if(this->hitField(OSData::touchX, OSData::touchY, step) != this->repeat_field ||
       step != this->repeat_step){
        this->repeat_field = -1;
        return;
    }

    this->repeat_last_ms = now;
    this->applyStep(this->repeat_field, this->repeat_step);
}

void DurationPicker::causeOnPressStart(){
    Widget::causeOnPressStart();
    if(!this->editable) return;

    int step = 0;
    const int field = this->hitField(OSData::touchX, OSData::touchY, step);
    if(field < 0) return;

    this->applyStep(field, step);

    this->repeat_field      = field;
    this->repeat_step       = step;
    this->repeat_started_ms = millis();
    this->repeat_last_ms    = this->repeat_started_ms;
}

void DurationPicker::causeOnPressEnd(){
    this->repeat_field = -1;
    Widget::causeOnPressEnd();
}

void DurationPicker::render(){
    // 長押しの連続加算は「描くかどうか」とは無関係に毎フレーム見る必要がある。
    // update()は非仮想なので、毎フレーム必ず呼ばれるrender()の入口を使う。
    // FlushDirty()のrenderForce()経由で同じフレームに2回来ることがあるが、
    // kRepeatIntervalMsの時間ゲートがあるので二重に進むことはない
    this->tickRepeat();

    if(!this->needs_redraw) return;
    if(!this->visible) return;

    //前回の描画内容の変更(削除)
    if(this->prev_l_rect != this->l_rect)
        markdirty(getScreenPrevRect());

    const Rect g_rect = this->getScreenRect();
    markdirty(g_rect);

    const Layout lay = this->computeLayout();

    const uint32_t sec = this->displaySeconds();
    const int values[3] = {
        (int)(sec / 3600u),
        (int)((sec / 60u) % 60u),
        (int)(sec % 60u)
    };

    this->fontApply();
    OSData::frame->setTextColor(this->text_color);

    char buf[4];
    for(int i = 0; i < 3; i++){
        snprintf(buf, sizeof(buf), "%02d", values[i]);

        //フォントは等幅ではないので、2桁ぶんの枠の中で中央へ寄せる
        const int w = OSData::frame->textWidth(buf);
        OSData::frame->setCursor(lay.col_x[i] + (lay.col_w - w) / 2, lay.digits_y);
        OSData::frame->print(buf);

        if(i < 2){
            OSData::frame->setCursor(lay.col_x[i] + lay.col_w, lay.digits_y);
            OSData::frame->print(":");
        }
    }

    OSData::frame->setTextColor(PICO_BLACK);
    this->fontDefault();

    if(this->editable){
        for(int i = 0; i < 3; i++){
            const int cx = lay.col_x[i] + lay.col_w / 2;

            OSData::frame->fillTriangle(
                cx, lay.arrow_up_y,
                cx - kArrowW / 2, lay.arrow_up_y + kArrowH,
                cx + kArrowW / 2, lay.arrow_up_y + kArrowH,
                this->border_color
            );
            OSData::frame->fillTriangle(
                cx, lay.arrow_down_y + kArrowH,
                cx - kArrowW / 2, lay.arrow_down_y,
                cx + kArrowW / 2, lay.arrow_down_y,
                this->border_color
            );
        }
    }

    this->prev_l_rect.copy(this->getLocalRect());
    this->needs_redraw = false;
}
