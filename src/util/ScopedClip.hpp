#pragma once

#include "OS_Data.hpp"
#include "functions/GFX_Functions.hpp"

// OSData::frameのクリップを「今のクリップ ∩ (x, y, w, h)」へ一時的に狭め、抜けるときに元へ戻す。
//
// 部品の中で一部だけをクリップして描くとき(文字の右端・アイコンの枠・表のセル)に使う。
// setClipRect()→clearClipRect()と書くと、FlushDirty()がウィジェットごとに掛けている
// 「dirty矩形の内側だけ」のクリップまで外れ、その後に描くものがdirty矩形の外へはみ出す。
// はみ出した分は液晶へ送られないので、frameと液晶の中身が食い違い、次にその行が
// 変わらない限り古い絵が残る(FlushDirty()は変わっていない行を送らないため)。
class ScopedClip {
    public:
        ScopedClip(int32_t x, int32_t y, int32_t w, int32_t h) {
            OSData::frame->getClipRect(&sx_, &sy_, &sw_, &sh_);
            // クリップが無いとき、実機のLovyanGFXはスプライト全体を返す。ホストテストのスタブは0を返すので
            // 「クリップ無し」として扱う
            had_ = sw_ > 0 && sh_ > 0;
            // 合成の外のrender()(Widget::update())はクリップを空にして書き込みを捨てている。
            // 空のクリップも幅0で返るので「クリップ無し」と取り違えると、ここで画面全体へ広げてしまい、
            // 抜けるときのclearClipRect()でその後の描画まで素通しになる(dirty矩形の外のframeへ描かれ、液晶と食い違った)
            if (!had_ && PICO_GFX::render_suppressed) {
                suppressed_ = true;
                OSData::frame->setClipRect(0, 0, 0, 0);
                return;
            }
            int32_t x0 = x, y0 = y, x1 = x + w, y1 = y + h;
            if (had_) {
                if (x0 < sx_) x0 = sx_;
                if (y0 < sy_) y0 = sy_;
                if (x1 > sx_ + sw_) x1 = sx_ + sw_;
                if (y1 > sy_ + sh_) y1 = sy_ + sh_;
            }
            if (x1 < x0) x1 = x0;
            if (y1 < y0) y1 = y0;
            OSData::frame->setClipRect(x0, y0, x1 - x0, y1 - y0);
        }
        ~ScopedClip() {
            if (suppressed_) OSData::frame->setClipRect(0, 0, 0, 0);
            else if (had_) OSData::frame->setClipRect(sx_, sy_, sw_, sh_);
            else OSData::frame->clearClipRect();
        }
        ScopedClip(const ScopedClip&) = delete;
        ScopedClip& operator=(const ScopedClip&) = delete;

    private:
        int32_t sx_ = 0, sy_ = 0, sw_ = 0, sh_ = 0;
        bool had_ = false;
        bool suppressed_ = false;
};
