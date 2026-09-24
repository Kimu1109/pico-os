#pragma once

#include "gui/widgets/Widget.hpp"
#include <LovyanGFX.hpp>

namespace Canvas {
    // Luaからは数値(canvas_mode)で指定されるので、既存の値は動かさず末尾へ足すこと
    enum Mode {
        Line,       // 0: フリーハンド
        Rect,       // 1: 四角形(fill_shapeで塗りつぶし)
        Ellipse,    // 2: 楕円(fill_shapeで塗りつぶし)
        Arrow,      // 3: 矢印
        Straight,   // 4: 直線
        Fill        // 5: 塗りつぶし(バケツ)。触れた点と同じ色で繋がった領域を塗る
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

        //Rect/Ellipseを塗りつぶしで描くか(falseなら輪郭だけ)
        bool fill_shape = false;

        //ドラッグ中のプレビュー(Rect/Ellipse/Arrow/Straight)が前回覆った画面矩形。
        //指が動くたびにキャンバス全体ではなく「前回+今回」の範囲だけをdirtyにする
        Rect preview_rect{0, 0, 0, 0};

        //1段だけの「元に戻す」。有効な間だけスプライトと同じ大きさのバッファを持つ
        //(4bppなので w*h/2 バイト。ペイントの全面キャンバスで約23KB)。
        //描き込む操作の直前にスナップショットを取り、undo()はバッファと中身を入れ替える
        //(もう一度押すとやり直しになる)
        uint8_t* undo_buf = nullptr;
        uint32_t undo_len = 0;
        bool undo_valid = false;

        //undo_bufの確保/解放。サイズが変わったら取り直す
        void allocUndo();
        void freeUndo();
        //描き込む直前に呼ぶ。undoが無効なら何もしない
        void snapshot();

        //図形(Rect/Ellipse/Arrow/Straight)を(x0,y0)-(x1,y1)の範囲に描く。
        //(ox,oy)は描き先の座標への足し込み(スプライトなら0、frameへのプレビューなら画面位置)
        void drawShape(LGFX_Sprite* target, int ox, int oy, int x0, int y0, int x1, int y1);
        //drawShape()が塗る範囲(スプライト座標)。ブラシの太さぶん広げてある
        Rect shapeBounds(int x0, int y0, int x1, int y1);

        //(x,y)から始まる同色の連結領域をbrush_colorで塗る。塗った範囲(スプライト座標)を返す
        Rect floodFill(int16_t x, int16_t y);

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
            if(mode < Canvas::Mode::Line || mode > Canvas::Mode::Fill) return;
            this->mode = mode;
        }
        Canvas::Mode getMode(){
            return this->mode;
        }

        void setFillShape(bool fill){ this->fill_shape = fill; }
        bool getFillShape() { return this->fill_shape; }

        //「元に戻す」用のバッファを持つか。無効化するとバッファは解放される
        void setUndoEnabled(bool enabled);
        bool getUndoEnabled() { return this->undo_buf != nullptr; }
        //直前の描き込みを取り消す(もう一度呼ぶとやり直し)。戻せるものが無ければfalse
        bool undo();
        //pico.canvas_load()等、外から中身を書き換える直前に呼ぶ(undoで戻せるようにする)
        void saveUndoPoint() { this->snapshot(); }

        void drawArrow(LGFX_Sprite *canvas, int x0, int y0, int x1, int y1);

        static void DrawThickLine(LGFX_Sprite* canvas, int x0, int y0, int x1, int y1, float radius, int8_t color);
};