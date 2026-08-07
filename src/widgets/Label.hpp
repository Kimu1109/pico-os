#pragma once

#include <vector>
#include "widgets/Widget.hpp"
#include "consts.hpp"
#include "Arduino.h"

// 1つの書式区間（同じ太字/下線/波線設定を持つ文字列断片）
struct TextRun {
    String text;
    bool bold = false;
    bool underline = false;
    bool wavy = false;
};

class Label : public Widget {
    private:
        String raw_text;                          // マークアップ込みの元テキスト
        std::vector<std::vector<TextRun>> lines;   // 解析・折返し後の行データ
        int max_width = 0;                         // 0 = 折り返し無効（\nのみ改行）
        int line_height = 0;
        int line_spacing = 2;                      // 行間(px)
        uint16_t color = PICO_BLACK;                // 文字色（下線・波線にも使用）

        // 波線装飾用のマージン
        static constexpr int kDecorationMargin = 2;

        // ---------- 内部ヘルパー関数 ----------
        static int utf8CharLen(uint8_t lead);
        static std::vector<String> splitChars(const String& s);
        std::vector<TextRun> parseMarkup(const String& src);
        void relayout();
        void renderRun(const TextRun& run, int x, int y);

    public:
        using Widget::Visible;

        Label(int x, int y, String text);
        Label(String text);
        
        void render() override;
        void needsRender();

        // ---------- setter / getter ----------
        void Text(String text);
        String Text();

        void MaxWidth(int width);
        int MaxWidth();

        void LineSpacing(int spacing);

        void Color(uint16_t c);

        void X(int x);
        int X();

        void Y(int y);
        int Y();

        int W();
        int H();

        void Visible(bool visible) override;

        bool isOpaque() const override { return false; }
};

