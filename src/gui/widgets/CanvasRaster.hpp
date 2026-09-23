#pragma once

#include "gui/widgets/Widget.hpp"
#include <LovyanGFX.hpp>

namespace Canvas {
    enum Mode {
        Line,
        Rect,
        Ellipse,
        Arrow
    };
}

class CanvasRaster : public Widget {
    private:
        Canvas::Mode mode = Canvas::Mode::Line;

        LGFX_Sprite* sp;

        int16_t sx;
        int16_t sy;

        //移動検出用の前回の絶対座標(グローバル座標)矩形
        Rect prev_screen_rect{0, 0, 0, 0};

        int8_t brush_color = PICO_BLACK;
        float brush_radius = 1.5;

        //(sx,sy)から(x,y)まで現在のブラシで線を焼き込み、描いた範囲だけをdirtyにする。
        //終わると(sx,sy)は(x,y)へ進む
        void strokeTo(int16_t x, int16_t y);

        int16_t relX(int16_t x){
            return x - this->getScreenX();
        }
        int16_t relY(int16_t y){
            return y - this->getScreenY();
        }
        int16_t absX(int16_t x){
            return x + this->getScreenX();
        }
        int16_t absY(int16_t y){
            return y + this->getScreenY();
        }

    public:

        CanvasRaster(int16_t x, int16_t y, int16_t w, int16_t h);


        // spはコンストラクタでnewし、createSprite()でピクセルバッファも確保している。

        // 解放しないとCanvasRasterを破棄するたびに画面1枚分のバッファがリークする

        ~CanvasRaster() override;

        void render() override;

        // pico.image_save/loadが直接スプライトへ触れられるようにする生の口。
        // 中身を書き換える側(IconRender::EncodePimg/DecodePimgBody)は
        // このクラスの外に置いてあるので、非constの生ポインタで貸す
        LGFX_Sprite* getSprite() { return sp; }

        // w/hを変更する。LGFX_Sprite::createSprite()を呼び直す都合上、
        // 既存の描画内容は消える(白紙に戻る)。生成直後(pico.create()の直後)に
        // 一度だけ呼ぶ使い方を想定しており、描き始めた後に呼ぶ用途ではない。
        // pico.canvas_load()が画像サイズへ合わせる際にも使う
        void resize(int16_t w, int16_t h);
        void setW(int16_t w){ this->resize(w, this->l_rect.h); }
        void setH(int16_t h){ this->resize(this->l_rect.w, h); }

        WidgetType getWidgetType() const override { return WidgetType::CanvasRaster; }

        // 自分の矩形をスプライトで隙間なく覆うのでOPAQUE。CLEARのままだと
        // FlushDirty()が毎回、背景の白塗り+下に重なるウィジェット(スクラッチパッドの
        // 枠線Rect等)の再描画をしてから上書きすることになる
        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::OPAQUE; }
        
        void causeOnPressStart() override;
        void causeOnPressMove() override;
        void causeOnPressEnd() override;

        void canvasClear();

        void setBrush(int8_t brush_color, float brush_radius){
            this->brush_color = brush_color;
            this->brush_radius = brush_radius;
        }

        void setBrushColor(int8_t brush_color){
            this->brush_color = brush_color;
        }
        int8_t getBrushColor() { return this->brush_color; }

        void setBrushRadius(float radius){
            this->brush_radius = radius;
        }
        float getBrushRadius() { return this->brush_radius; }

        void setMode(Canvas::Mode mode){
            this->mode = mode;
        }
        Canvas::Mode getMode(){
            return this->mode;
        }

        void drawArrow(LGFX_Sprite *canvas, int x0, int y0, int x1, int y1);

        static void DrawThickLine(LGFX_Sprite* canvas, int x0, int y0, int x1, int y1, float radius, int8_t color);
};