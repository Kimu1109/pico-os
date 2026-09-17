#pragma once

#include "gui/widgets/Widget.hpp"

#include <functional>

// 電卓の主なキー配置(数字・四則演算・括弧・√・π・AC・⌫・=)。
//
// AppGrid/ColorDialog/KeyboardNumと同じく、キー1つごとにButtonをnewせず、
// 固定長のキー配列+タップ位置からの逆算で処理する(ヒープを使わない、子を持たないので
// hit_transparentの問題とも無縁)。電卓アプリ専用の部品なのでwidgets/apps/に置く。
class CalculatorKeypad : public Widget {
    public:
        static constexpr int kRows = 6;
        static constexpr int kCols = 4;

    private:
        struct Key {
            const char* label; // 表示文字列でありsetOnKey()へそのまま渡す値でもある
            int row;
            int col_start;
            int col_span; // 1マスを超えて横に広げる場合(例: "0")
        };

        static const Key kKeys[];
        static const int kKeyCount;

        std::function<void(const char* key)> on_key = nullptr;

        // 列は必ず240pxをkCols(4)で割り切れる前提だが、行の高さは呼び出し側の
        // 表示領域次第で割り切れないことがあるため、余りは最後の行/列へ足す
        // (TabBar::tabX/tabWと同じ流儀)
        int colX(int col) const;
        int colW(int col) const;
        int colSpanW(int col_start, int col_span) const;
        int rowY(int row) const;
        int rowH(int row) const;

        const Key* hitKey(int local_x, int local_y) const;

    public:
        CalculatorKeypad(int x, int y, int w, int h){
            this->l_rect = { (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h };
        }

        void setOnKey(std::function<void(const char* key)> callback){
            this->on_key = callback;
        }

        void causeOnPressStart() override;
        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::CalculatorKeypad; }
        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::OPAQUE; }

        void setW(int w){ this->l_rect.w = (int16_t)w; this->needsRender(); }
        void setH(int h){ this->l_rect.h = (int16_t)h; this->needsRender(); }
};
