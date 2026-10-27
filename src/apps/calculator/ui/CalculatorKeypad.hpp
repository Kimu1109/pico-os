#pragma once

#include "gui/widgets/Widget.hpp"

#include "util/InplaceFunction.hpp"

// 電卓のキーパッド。配列(Layout)は3種類:
//   Scientific … 関数電卓(7行6列)。電卓ページとグラフの式
//   Programmer … プログラマ電卓(7行6列)。16進の数字A-F・ビット演算。今の基数で使えない数字は灰色で押せない
//   Converter  … 単位/通貨の変換(4行4列)。数字と ± と[入替]だけ
//
// AppGrid/ColorDialog/KeyboardNumと同じく、キー1つごとにButtonをnewせず、
// 固定長のキー配列+タップ位置からの逆算で処理する(ヒープを使わない、子を持たないので
// hit_transparentの問題とも無縁)。
//
// SHIFT(逆関数等の裏の機能)とHYP(双曲線関数)はキーパッドの中だけで完結する1回きりの切り替えで、
// 次のキーを押すと戻る。押されたキーは「式へ足す文字列」(例: "sin(")か、次の命令のどれかで
// setOnKey()へ渡る:
//   "AC"(全消去) "DEL"(1つ消す) "="(計算/グラフでは描画) "DRG"(角度の単位を切り替える)
//   "NEG"(符号を反転。変換) "SWAP"(変換元と変換先を入れ替える。変換)
class CalculatorKeypad : public Widget {
    public:
        // キーの種類。Insertは式へ足す文字列、Digitは基数で使えるかが変わる数字(進数)、それ以外は命令
        enum class Kind : uint8_t { Insert, Command, Shift, Hyp, Trig, Digit };

        enum class Layout : uint8_t { Scientific, Programmer, Converter };

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
        const Key* keys = nullptr;
        int key_count = 0;
        int rows = 7;
        int cols = 6;
        int fn_rows = 3;      // 上から何行を関数の段(薄い灰色の地)にするか
        int digit_base = 16;  // Digitのキーのうち、値がこれ以上のものは押せない

        InplaceFunction<void(const char* key)> on_key = nullptr;

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
        CalculatorKeypad(int x, int y, int w, int h, Layout layout = Layout::Scientific);

        void setOnKey(InplaceFunction<void(const char* key)> callback){
            this->on_key = callback;
        }

        void setGraphMode(bool on){ if(this->graph_mode != on){ this->graph_mode = on; this->needsRender(); } }
        void setAngleLabel(const char* label){ this->angle_label = label; this->needsRender(); }

        // 進数: 今の基数(2/8/10/16)。使えない数字のキーを灰色にして押せなくする
        void setDigitBase(int base){ if(this->digit_base != base){ this->digit_base = base; this->needsRender(); } }
        int getDigitBase() const { return this->digit_base; }

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
