#pragma once

#include "gui/widgets/Widget.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

class PvidPlayer;

// .pvid の今のフレームを等倍で描くウィジェット。VideoScene専用。
//
// 自分の矩形(黒)の真ん中に動画を置く。描き方は GameBoyView と同じ:
//   - 描くのはFlushDirty()の合成の中(PICO_GFX::isDirtyDeactivates中)だけ。UpdateAll()から来るrender()はdirtyを積むだけ
//   - OSData::frame の4bppのバッファへ行ごとに memcpy する(今のクリップ=dirty矩形の内側だけ)
//   - onFrame() が、前回から変わったタイルの行の帯だけをdirtyにする(止まった画面は液晶へ何も送らない)
// 動画が無い間は真ん中に文を出す。
class VideoView : public Widget {
    private:
        PvidPlayer* player = nullptr;
        // 自分の中での動画の位置(画面座標ではない)
        int16_t vx = 0, vy = 0, vw = 0, vh = 0;
        FixedString<PICO_STR_L> message;

        void drawMessage(const Rect& g);

    public:
        VideoView(int x, int y, int w, int h);

        // 開いた/閉じたときに呼ぶ(動画の置き場所を決め直して全体を描き直す)
        void setPlayer(PvidPlayer* p);
        // 1フレーム以上解いた後に呼ぶ。変わった所だけdirtyにする
        void onFrame();
        void setMessage(const char* text);
        // 動画の画面上の矩形(無ければ大きさ0)
        Rect videoRect() const;

        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::VideoView; }
        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::OPAQUE; }
};
