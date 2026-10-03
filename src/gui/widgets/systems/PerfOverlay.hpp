#pragma once

#include "gui/widgets/Widget.hpp"
#include "consts.hpp"

// フレーム時間のオンスクリーン表示(/sys/debug.cfg の perf-overlay、設定アプリの「その他」)。
// 画面の右下に小さく出す常駐オーバーレイ。中身は ProfilerFunctions::Last() と History()。
//
//   16.7ms 60.0fps          … 平均フレーム時間とfps
//   max 24.1 lua 2.3        … 窓の中の最大フレーム時間・1フレームあたりのLuaの実行時間
//   upd 3.1 drw 9.2 etc 0.4 … 画面更新(onUpdate/Luaのloop)・描画(合成+転送)・それ以外
//   [グラフ]                 … 直近60フレームのフレーム時間(緑≦16.7ms、黄≦33.3ms、赤それ以上)
//
// 書き換えるのは集計の窓が閉じたとき(0.5秒ごと)だけ。毎フレーム描き直すと、表示そのものが
// 測っている時間を押し上げてしまうため。タップは下の画面へ素通りさせる(hit_transparent)。
class PerfOverlay : public Widget {
    public:
        static constexpr int kWidth = 132;
        static constexpr int kHeight = 50;

        PerfOverlay(){
            this->l_rect = { (int16_t)(SCREEN_WIDTH - kWidth), (int16_t)(SCREEN_HEIGHT - kHeight), kWidth, kHeight };
            this->hit_transparent = true;
            this->visible = false;
        }

        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::OPAQUE; }
        int8_t getBackgroundColor() override { return PICO_BLACK; }
        void render() override;
        WidgetType getWidgetType() const override { return WidgetType::PerfOverlay; }

    private:
        uint32_t shown_serial = 0xFFFFFFFF;
};
