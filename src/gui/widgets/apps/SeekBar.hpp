#pragma once

#include "gui/widgets/Widget.hpp"
#include "consts.hpp"

#include "util/InplaceFunction.hpp"

// 再生位置のシークバー(ファイルビューワーの音・動画、動画アプリ)。
//
// 線 + つまみを自分で描く。押した/ドラッグした位置へつまみを動かして見せ、指を離したときに
// 1回だけ on_seek(割合 0〜1)を呼ぶ(動かすたびにSDを読み直さないため)。
// 再生位置は持たず、シーンが setRatio() で流し込む(ドラッグ中は流し込みを無視する)。
// seekable=false(MMLなど飛べないもの)のときはつまみを出さず、タッチにも反応しない。
class SeekBar : public Widget {
    private:
        float ratio = 0.0f;       // 流し込まれた位置
        float drag_ratio = 0.0f;  // ドラッグ中の位置
        bool dragging = false;
        bool seekable = true;
        int shown_px = -1;
        InplaceFunction<void(float)> on_seek = nullptr;

        float ratioAt(int screen_x) const;
        int pxOf(float r) const;

    public:
        static constexpr int kKnobR = 5;

        SeekBar(int16_t x, int16_t y, int16_t w, int16_t h){
            this->l_rect = {x, y, w, h};
        }

        void setOnSeek(InplaceFunction<void(float)> cb){ this->on_seek = cb; }
        // 毎フレーム呼んでよい(画素が変わったときだけ描き直す)
        void setRatio(float r);
        void setSeekable(bool on);
        bool isDragging() const { return dragging; }

        void render() override;
        void causeOnPressStart() override;
        void causeOnPressMove() override;
        void causeOnPressEnd() override;
        void causeOnPressOut() override;

        WidgetType getWidgetType() const override { return WidgetType::SeekBar; }
        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::OPAQUE; }
        bool wantsFrameUpdate() const override { return false; }
};
