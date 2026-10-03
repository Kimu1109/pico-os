#pragma once

#include "gui/widgets/Widget.hpp"
#include "util/Calc_Eval.hpp"
#include "consts.hpp"

#include <cstdint>

// 電卓アプリのグラフ(y = f(x) を最大3本)。
//
// 子を持たずrender()で直接描く型(MonthGrid/AppGridと同じ)。式の文字列はシーンが持ち、
// ここは指すだけ(setFunction())。式は CalcEval::Evaluate() でx(画素の列)ごとに評価する。
//
//   - 指でドラッグ … 表示範囲を動かす
//   - タップ        … その位置のxで各式の値を読む(トレース。縦線と点、左上に値)
//   - 拡大/縮小/初期化はシーンのボタンから zoom() / resetView()
//
// 評価した値は列ごとに覚えておき(samples)、式や表示範囲が変わったときだけ計算し直す。
// FlushDirty()がdirty矩形ごとにrender()を呼ぶので、毎回計算すると何度も同じ評価をするため。
// 描くのはGameBoyViewと同じくFlushDirty()の合成の中だけ(UpdateAll()からはdirtyを積むだけ)。
class GraphView : public Widget {
    public:
        static constexpr int kMaxFunctions = 3;
        static constexpr int kMaxColumns = SCREEN_WIDTH;
        static const int8_t kColors[kMaxFunctions];

    private:
        const char* exprs[kMaxFunctions] = { nullptr, nullptr, nullptr };

        // 表示範囲(数学の座標)
        double x_min = -10.0, x_max = 10.0;
        double y_min = -10.0, y_max = 10.0;

        CalcEval::Context ctx;

        float samples[kMaxFunctions][kMaxColumns];
        bool samples_valid = false;

        // トレース
        bool trace_on = false;
        double trace_x = 0.0;

        // ドラッグ
        int16_t press_x = 0, press_y = 0;
        int16_t last_x = 0, last_y = 0;
        bool dragged = false;

        double pixelToX(double px) const;
        double pixelToY(double py) const;
        double xToPixel(double x) const;
        double yToPixel(double y) const;

        void computeSamples();
        void drawGrid(const Rect& g);
        void drawCurves(const Rect& g);
        void drawTrace(const Rect& g);

        void changed(){ this->samples_valid = false; this->needsRender(); }

    public:
        GraphView(int x, int y, int w, int h);

        // i番目の式(nullptrか空ならその式は描かない)。文字列は呼び出し側が持ち続けること
        void setFunction(int i, const char* expr);
        // 式の中身だけが変わったとき(ポインタは同じ)
        void functionsChanged(){ this->changed(); }

        void setAngleMode(CalcEval::AngleMode mode){ this->ctx.angle = mode; this->changed(); }
        void setAns(double ans){ this->ctx.ans = ans; this->changed(); }

        // 中心を変えずに factor 倍の範囲を見せる(0.5 = 2倍に拡大)
        void zoom(double factor);
        void resetView();
        // 表示範囲の幅/高さに対する割合だけ動かす(物理キーボードの矢印)
        void pan(double fx, double fy);
        void setView(double xmin, double xmax, double ymin, double ymax);

        double getXMin() const { return this->x_min; }
        double getXMax() const { return this->x_max; }
        double getYMin() const { return this->y_min; }
        double getYMax() const { return this->y_max; }

        bool isTracing() const { return this->trace_on; }
        double getTraceX() const { return this->trace_x; }
        void clearTrace(){ if(this->trace_on){ this->trace_on = false; this->needsRender(); } }

        // テスト用: 列colでのi番目の式の値(計算できなければNaN)
        float sampleAt(int i, int col);

        void causeOnPressStart() override;
        void causeOnPressMove() override;
        void causeOnPressEnd() override;
        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::GraphView; }
        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::OPAQUE; }
};
