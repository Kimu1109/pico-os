#pragma once

#include "gui/widgets/Widget.hpp"
#include "consts.hpp"

// 図形ウィジェットの1つ。3点(x1,y1)/(x2,y2)/(x3,y3)を頂点とする三角形を描く。
// 座標の扱いはLineShapeと同じ考え方: x1..y3は親から見たローカル座標(正の情報源)、
// l_rectはそれ+太さぶんの余白を包む外接矩形として毎回再計算する。
// x/y(Widget::getX/setX)は外接矩形の左上を指し、setX/setYは3点をまとめて
// 平行移動する(LayoutContainer/GridContainerがsetX/setYで配置する前提を崩さないため)
class TriangleShape : public Widget {
    private:
        int16_t x1, y1, x2, y2, x3, y3;

        int8_t color = PICO_BLACK;
        bool filled = true;
        // filled==falseのときの輪郭の太さ(単位px)
        int thickness = 1;

        void recomputeBounds();

    public:
        TriangleShape(int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t x3, int16_t y3)
            : x1(x1), y1(y1), x2(x2), y2(y2), x3(x3), y3(y3) {
            this->recomputeBounds();
            this->prev_l_rect.copy(this->l_rect);
        }

        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::TriangleShape; }

        void setX(int x) override {
            const int16_t delta = (int16_t)x - this->l_rect.x;
            this->x1 += delta; this->x2 += delta; this->x3 += delta;
            this->l_rect.x = (int16_t)x;
            this->needsRender();
        }
        void setY(int y) override {
            const int16_t delta = (int16_t)y - this->l_rect.y;
            this->y1 += delta; this->y2 += delta; this->y3 += delta;
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
        int getX3() const { return this->x3; }
        void setX3(int x3) { this->x3 = (int16_t)x3; this->recomputeBounds(); this->needsRender(); }
        int getY3() const { return this->y3; }
        void setY3(int y3) { this->y3 = (int16_t)y3; this->recomputeBounds(); this->needsRender(); }

        int8_t getColor() const { return this->color; }
        void setColor(int8_t color) { this->color = color; this->needsRender(); }

        bool getFilled() const { return this->filled; }
        void setFilled(bool filled) { this->filled = filled; this->needsRender(); }

        int getThickness() const { return this->thickness; }
        void setThickness(int thickness) {
            this->thickness = thickness < 1 ? 1 : thickness;
            this->recomputeBounds();
            this->needsRender();
        }
};
