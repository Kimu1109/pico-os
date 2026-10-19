#pragma once

#include "gui/widgets/Widget.hpp"

#include <functional>

// 関数電卓のキーパッド(7行6列)。
//
// AppGrid/ColorDialog/KeyboardNumと同じく、キー1つごとにButtonをnewせず、
// 固定長のキー配列+タップ位置からの逆算で処理する(ヒープを使わない、子を持たないので
// hit_transparentの問題とも無縁)。電卓アプリ専用の部品なのでwidgets/apps/に置く。
//
// SHIFT(逆関数等の裏の機能)とHYP(双曲線関数)はキーパッドの中だけで完結する1回きりの切り替えで、
// 次のキーを押すと戻る。押されたキーは「式へ足す文字列」(例: "sin(")か、次の命令のどれかで
// setOnKey()へ渡る:
//   "AC"(全消去) "DEL"(1つ消す) "="(計算/グラフでは描画) "DRG"(角度の単位を切り替える)
class CalculatorKeypad : public Widget {
    public:
        static constexpr int kRows = 7;
        static constexpr int kCols = 6;

        // キーの種類。Insertは式へ足す文字列、それ以外はキーパッド/シーンへの命令
        enum class Kind : uint8_t { Insert, Command, Shift, Hyp, Trig };

        struct Key {
            const char* label;       // 表
            const char* insert;      // 表のときに足す文字列(Commandなら命令名)
            const char* shift_label; // SHIFT中の表示(nullptrなら表と同じ)
            const char* shift_insert;
            int row;
            int col_start;
            int col_span;
            Kind kind;
        };

    private:
        static const Key kKeys[];
        static const int kKeyCount;

        std::function<void(const char* key)> on_key = nullptr;

        bool shift = false;
        bool hyp = false;
        bool graph_mode = false;     // trueなら"mod"の位置に変数"x"、"="は"描画"
        const char* angle_label = "DEG";

        // 三角関数の文字列(SHIFT/HYPの組み合わせで"asinh("等を作る)
        mutable char trig_buf[3][12];

        int colX(int col) const;
        int colW(int col) const;
        int colSpanW(int col_start, int col_span) const;
        int rowY(int row) const;
        int rowH(int row) const;

        const Key* hitKey(int local_x, int local_y) const;

        // 今の状態(SHIFT/HYP/グラフ)で見せる文字と、押したときに渡す文字列
        const char* labelOf(const Key& key) const;
        const char* insertOf(const Key& key) const;

    public:
        CalculatorKeypad(int x, int y, int w, int h){
            this->l_rect = { (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h };
        }

        void setOnKey(std::function<void(const char* key)> callback){
            this->on_key = callback;
        }

        void setGraphMode(bool on){ if(this->graph_mode != on){ this->graph_mode = on; this->needsRender(); } }
        void setAngleLabel(const char* label){ this->angle_label = label; this->needsRender(); }

        bool isShift() const { return this->shift; }
        bool isHyp() const { return this->hyp; }

        // テスト用: 今の状態でそのラベルを持つキーの中心(画面座標)。無ければfalse
        bool keyCenter(const char* label, int& x, int& y) const;

        void causeOnPressStart() override;
        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::CalculatorKeypad; }
        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::OPAQUE; }

        void setW(int w){ this->l_rect.w = (int16_t)w; this->needsRender(); }
        void setH(int h){ this->l_rect.h = (int16_t)h; this->needsRender(); }
};
