#pragma once

#include "gui/widgets/Widget.hpp"
#include "functions/Notification_Functions.hpp"
#include "consts.hpp"

// 通知のトースト(画面上部に数秒だけ出る帯)。OS常駐のオーバーレイで、全てのオーバーレイの一番上に置く
// (半透明のダイアログやキーボードの上にも出る)。
//
//   [ベル] タイトル                       [×]
//          本文(1行に収まらない分は切れる)
//
// - 出す/消すのは Notification_Sources.cpp。このウィジェットは見た目と当たり判定だけを持つ
// - 消すときは PICO_GFX::MarkDirtyBelow() で下を描き直させる(半透明のダイアログの上に出していた場合、
//   普通のMarkDirty()だとトーストの跡がダイアログの下に残るため)
// - 描くのはFlushDirty()の合成の中だけ(GameBoyView等と同じ)。OPAQUEなので背景は合成側が塗る
class NotificationToast : public Widget {
    public:
        static constexpr int kMargin = 4;
        static constexpr int kH = 42;
        static constexpr int kCloseW = 30;   // 右端の「×」の当たり幅

        NotificationToast(){
            this->l_rect = { (int16_t)kMargin, (int16_t)(STATUSBAR_HEIGHT + 2),
                             (int16_t)(SCREEN_WIDTH - kMargin * 2), (int16_t)kH };
            this->background_color = PICO_WHITE;
            this->visible = false;
        }

        void show(const NotificationFunctions::Entry& e, unsigned long now_ms);
        void hide();

        bool isShowing() const { return this->visible; }
        uint32_t getSeq() const { return this->seq; }
        unsigned long getShownMs() const { return this->shown_ms; }
        // (x,y)が右端の「×」に当たるか(スクリーン座標)
        bool hitClose(int x, int y) const;
        bool contains(int x, int y) const;

        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::OPAQUE; }
        void render() override;
        WidgetType getWidgetType() const override { return WidgetType::NotificationToast; }

    private:
        uint32_t seq = 0;
        unsigned long shown_ms = 0;
        NotificationFunctions::Title title;
        NotificationFunctions::Body  body;
        NotificationFunctions::AppName app;
};
