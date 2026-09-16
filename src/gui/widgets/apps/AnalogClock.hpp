#pragma once

#include "gui/widgets/Widget.hpp"
#include "gui/widgets/interfaces/IBorderColor.hpp"
#include "consts.hpp"

// アナログ時計の文字盤。
//
// 子ウィジェットは持たずrender()でframeへ直接描く(AppGrid / ColorDialog と同じ方式)。
// 時刻を自分では取りに行かず、シーン側がsetTime()で流し込む
// — TimeFunctionsへの依存をウィジェットに持たせないため(テストや別用途で流用しやすい)。
//
// 針の位置が変わったフレームだけ再描画するので、毎フレームsetTime()を呼んでよい。
class AnalogClock : public Widget, public IBorderColor {
    private:
        int hour   = -1; //0-23(表示は12時間制)。-1は「まだ時刻が入っていない」
        int minute = -1;
        int second = -1;

        int8_t hand_color        = PICO_BLACK;
        int8_t second_hand_color = PICO_RED;

        // 文字盤の半径。l_rectの短辺から決まる
        int radius() const;

        // 針を「先端 + 根元」の三角形で描く。
        // 1pxの線では細すぎ、LovyanGFXのdrawWideLine()はアンチエイリアスのために
        // 4bitパレットに無い中間色を要求するので使わない。
        //   angle_deg : 12時を0°とした時計回りの角度
        //   tail      : 中心から見て針の反対側へ伸ばす長さ(軸のバランス取り)
        void drawHand(int cx, int cy, float angle_deg, int length, int half_width,
                      int tail, int8_t color);

        // 目盛り(12本)と 12/3/6/9 の数字
        void drawFace(int cx, int cy, int r);

    public:
        AnalogClock(int x, int y, int diameter){
            this->l_rect = { (int16_t)x, (int16_t)y, (int16_t)diameter, (int16_t)diameter };
        }

        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::AnalogClock; }

        // 文字盤の外は背景色で塗り潰す前提。OPAQUEにしておくと、
        // 針が動くたびのdirty矩形で下のウィジェットを描き直さずに済む
        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::OPAQUE; }

        // 値が変わったときだけ再描画を要求する
        void setTime(int h, int m, int s);

        int getHour() const   { return this->hour; }
        int getMinute() const { return this->minute; }
        int getSecond() const { return this->second; }

        void setHandColor(int8_t palette_color){
            this->hand_color = palette_color;
            this->needsRender();
        }
        void setSecondHandColor(int8_t palette_color){
            this->second_hand_color = palette_color;
            this->needsRender();
        }

        // 直径。文字盤は常に正方形に収まる
        void setDiameter(int diameter){
            this->l_rect.w = (int16_t)diameter;
            this->l_rect.h = (int16_t)diameter;
            this->needsRender();
        }
};
