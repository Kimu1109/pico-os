#pragma once

#include "gui/widgets/Widget.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

#include <cstdint>
#include "util/InplaceFunction.hpp"

// プログラマ電卓の「同じ値を4つの基数で並べる」表示(電卓アプリ専用)。
//
//   [HEX]            FF
//   [DEC]           255
//   [OCT]           377
//   [BIN] 0000 0000 0000 0000 0000 0000 1111 1111   (64bitのときは上位32bitと下位32bitの2行)
//
// 行をタップすると、その基数で入力する(setOnSelectBase)。選んでいる基数の見出しは反転表示。
// 値が無いとき(式が途中)は空欄、誤りのときは赤字の文言だけを出す。
// 2進はどの語長でも語長ぶんの桁を0埋めで出す(ビットの位置を数えやすいよう4桁ごとに隙間)。
// 子を持たず、行ごとの文字はrender()で直接描く(CalculatorKeypadと同じ)。
class BaseView : public Widget {
    public:
        static constexpr int kRowCount = 4; // HEX DEC OCT BIN

        // 1行の高さと2進の行の高さ(画面の割り付けに使う)
        static int RowHeight();
        static int BinLineHeight();
        // 全部の行を出すのに要る高さ
        static int FittingHeight();

        // 行の番号と基数
        static int BaseOfRow(int row);
        static int RowOfBase(int base);

    private:
        int64_t value = 0;
        int bits = 64;
        int base = 10;
        bool has_value = false;
        FixedString<PICO_STR_M> error;

        InplaceFunction<void(int base)> on_select_base = nullptr;

        int rowY(int row) const;
        int rowH(int row) const;

    public:
        BaseView(int x, int y, int w, int h){
            this->l_rect = { (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h };
        }

        void setValue(int64_t v, int word_bits);
        void clearValue();
        void setError(const char* message);
        void setBase(int b){ if(this->base != b){ this->base = b; this->needsRender(); } }
        int getBase() const { return this->base; }
        bool hasValue() const { return this->has_value; }
        int64_t getValue() const { return this->value; }
        const char* getError() const { return this->error.c_str(); }

        // 行の文字(テスト用)。行の番号は0=HEX〜3=BIN。2進は隙間なしで語長ぶんの桁
        void rowText(int row, char* out, size_t cap) const;
        // テスト用: 行の中心(画面座標)
        void rowCenter(int row, int& x, int& y) const;

        void setOnSelectBase(InplaceFunction<void(int base)> callback){ this->on_select_base = callback; }

        void causeOnPressStart() override;
        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::BaseView; }
        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::OPAQUE; }
};
