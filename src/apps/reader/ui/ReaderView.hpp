#pragma once

#include "gui/widgets/Widget.hpp"
#include "apps/reader/Reader_Layout.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

#include <functional>

// 読書アプリの本文の1ページを描くウィジェット。ReaderScene専用。
//
// 子を持たず render() で直接描く(TextViewと同じ型)。中身は ReaderBook が組んだページ(ReaderLayout::Page)を
// 指すだけで、コピーは持たない。下の端に1行の帯(ページ番号・進み具合の線)、しおりのある
// ページは右上にしおりの印を描く。
//
// 触ったとき:
//   - 左右へ払う … 左へ払うと次のページ、右へ払うと前のページ
//   - 左の1/3を叩く … 前のページ / 右の1/3 … 次のページ / 真ん中 … メニューを出す・閉じる
// メニューの帯(上下)は setMenuBands() で背景だけ描く。帯の上のボタンはシーンが置く。
//
// 文字の幅は自分で控えておく(組むたびに LovyanGFX の textWidth() を呼ぶと遅いため)。
// 漢字(CJK統合漢字)は全部同じ幅として1回だけ測る。かな・記号などは小さな表に覚える。
class ReaderView : public Widget {
    public:
        static constexpr int kPadX = 8;
        static constexpr int kPadTop = 6;
        static constexpr int kFooterH = 12;
        static constexpr int kSwipePx = 30;

        ReaderView(int16_t x, int16_t y, int16_t w, int16_t h);

        // 文字の大きさ(false=16px / true=24px)。変えると幅の控えを捨てる
        void setLargeFont(bool large);
        bool isLargeFont() const { return large_; }
        // この大きさの枠に合わせた組み方(ルビの有無は呼び出し側が入れる)
        ReaderLayout::Params params();

        // 描くページ(呼び出し側が持ち続ける。nullptr なら message を出す)
        void setPage(const ReaderLayout::Page* page);
        void setMessage(const char* text);
        // 下の帯。left/right は半角だけ(Font0で描く)。progress は 0〜1
        void setFooter(const char* left, const char* right, float progress);
        void setBookmarked(bool on);
        // メニューの帯(上 top_h px・下 bottom_h px)の背景を描く。0,0 で消す
        void setMenuBands(int top_h, int bottom_h);

        // dir: +1 = 次のページ / -1 = 前のページ
        void setOnTurn(std::function<void(int dir)> cb){ on_turn_ = cb; }
        void setOnMenu(std::function<void()> cb){ on_menu_ = cb; }

        void causeOnPressStart() override;
        void causeOnPressEnd() override;
        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::ReaderView; }
        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::OPAQUE; }

    private:
        static constexpr int kWideSlots = 256;

        const ReaderLayout::Page* page_ = nullptr;
        FixedString<PICO_STR_L> message_;
        FixedString<PICO_STR_S> footer_left_;
        FixedString<PICO_STR_S> footer_right_;
        float progress_ = 0.0f;
        bool bookmarked_ = false;
        bool large_ = false;
        int band_top_ = 0;
        int band_bottom_ = 0;

        int start_x_ = 0;
        int start_y_ = 0;

        std::function<void(int)> on_turn_ = nullptr;
        std::function<void()> on_menu_ = nullptr;

        // 文字の幅の控え
        int8_t ascii_w_[128];
        int16_t kanji_w_ = -1;
        uint32_t wide_key_[kWideSlots];
        uint8_t wide_w_[kWideSlots];

        void clearWidthCache();
        int fontPx() const;
        int linePitch() const;
        int measure(uint32_t cp);
        static int Measure(uint32_t cp, void* ctx);
        void setFont() const;
};
