#pragma once

#include "gui/widgets/Widget.hpp"
#include "consts.hpp"

// 図形ウィジェットの1つ。2点(x1,y1)-(x2,y2)を結ぶ線分を描く。
//
// x1/y1/x2/y2は「親から見たローカル座標」で持つ(l_rectとは独立した正の情報源)。
// l_rect(x,y,w,h)はこの2点+太さぶんの余白を包む外接矩形として毎回再計算し、
// hitTest/markdirty/renderの基準に使う(Widget共通の仕組みへそのまま乗せるため)。
//
// 共通プロパティのx/y(Widget::getX/setX)は「外接矩形の左上」を指し、setX/setYは
// 2点をまとめて平行移動する(LayoutContainer/GridContainerがsetX/setYで配置する
// 前提を崩さないため)。線の形そのもの(向き・長さ)を変えたい場合はx1/y1/x2/y2を
// 個別に設定すること
class LineShape : public Widget {
    private:
        int16_t x1, y1, x2, y2;

        int8_t color = PICO_BLACK;
        int thickness = 1;

        // x1/y1/x2/y2/thicknessから外接矩形(l_rect)を計算し直す
        void recomputeBounds();

    public:
        LineShape(int16_t x1, int16_t y1, int16_t x2, int16_t y2)
            : x1(x1), y1(y1), x2(x2), y2(y2) {
            this->recomputeBounds();
            this->prev_l_rect.copy(this->l_rect);
        }

        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::LineShape; }

        // Widget::setX/setYは通常l_rect.xだけを書き換えるが、線ではそれだと
        // 外接矩形の片端だけ動いて形が歪む。2点をまとめてずらす平行移動にする
        void setX(int x) override {
            const int16_t delta = (int16_t)x - this->l_rect.x;
            this->x1 += delta;
            this->x2 += delta;
            this->l_rect.x = (int16_t)x;
            this->needsRender();
        }
        void setY(int y) override {
            const int16_t delta = (int16_t)y - this->l_rect.y;
            this->y1 += delta;
            this->y2 += delta;
            this->l_rect.y = (int16_t)y;
            this->needsRender();
        }

        int getX1() const { return this->x1; }
        void setX1(int x1) { this->x1 = (int16_t)x1; this->recomputeBounds(); this->needsRender(); }
        int getY1() const { return this->y1; }
        void setY1(int y1) { this->y1 = (int16_t)y1; this->recomputeBounds(); this->needsRender(); }
        int getX2() const { return this->x2; }
        void setX2(int x2) { this->x2 = (int16_t)x2; this->recomputeBounds(); this->needsRender(); }
        int getY2() const { return this->y2; }
        void setY2(int y2) { this->y2 = (int16_t)y2; this->recomputeBounds(); this->needsRender(); }

        int8_t getColor() const { return this->color; }
        void setColor(int8_t color) { this->color = color; this->needsRender(); }

        int getThickness() const { return this->thickness; }
        void setThickness(int thickness) {
            this->thickness = thickness < 1 ? 1 : thickness;
            this->recomputeBounds();
            this->needsRender();
        }
};
