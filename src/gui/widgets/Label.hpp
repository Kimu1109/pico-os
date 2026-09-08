#pragma once

#include <vector>
#include "gui/widgets/Widget.hpp"
#include "consts.hpp"
#include "Arduino.h"
#include "functions/Font_Functions.hpp"
#include "gui/widgets/interfaces/IFontImplementation.hpp"
#include "gui/widgets/interfaces/IBorderColor.hpp"
#include "gui/widgets/interfaces/ITextColor.hpp"
#include "util/FixedString.hpp"

// 1 つの書式区間（同じ太字/下線/波線/取り消し線設定を持つ文字列断片）
// マークアップ対応表:
//   **text**  -> bold（太字）
//   _text_ / *text* -> underline（直線下線。標準 Markdown のイタリック相当だが
//                       描画コストの都合でイタリックの代わりに直線下線を採用している）
//   ~text~    -> wavy（波線下線。標準構文には存在しない独自の装飾）
//   ~~text~~  -> strikethrough（取り消し線。標準 Markdown の打ち消し線に対応）
template<size_t N = PICO_STR_LL>
struct TextRun {
    FixedString<N> text;
    bool bold = false;
    bool underline = false;
    bool wavy = false;
    bool strikethrough = false;
};

// カーソル (挿入位置) 候補 1 つ分の描画座標
// relayout() 時に、文字境界ごとの「そこにカーソルを置いたときの座標」を記録しておく
struct CursorSlot {
    int line = 0;   // 対応する行番号 (lines 配列のインデックス)
    int x = 0;      // その行内での X 座標 (rect.x からの相対値)
};

// テキストの水平方向の揃え位置
enum class TextAlign {
    Left,
    Center,
    Right
};

class Label : public Widget, public IFontImplementation, public IBorderColor, public ITextColor {
    private:
        FixedString<PICO_STR_LL> raw_text;                          // マークアップ込みの元テキスト
        std::vector<std::vector<TextRun<>>> lines;   // 解析・折返し後の行データ
        std::vector<std::vector<TextRun<>>> placeholder_lines;

        FixedString<PICO_STR_LL> placeholder_text = "";
        int8_t placeholder_color = PICO_LIGHTGREY;

        int max_width = 0;                         // 0 = 折り返し無効（\nのみ改行）
        int max_height = 0;                        // 0 = 高さ上限無効。超過分は切り詰めて非表示にする
        int default_height = 0;                     // 0 = 下限無効。行数由来の高さがこれより小さい場合はこちらを採用
        int line_height = 0;
        int line_spacing = 2;                      // 行間 (px)

        TextAlign text_align = TextAlign::Left;
        std::vector<int> line_offsets;
        std::vector<int> placeholder_line_offsets;

        // ---------- 背景・ボーダー関連 ----------
        bool has_background = false;                // 背景を描画するか（false = 透明）
        int border_width = 0;                        // ボーダーの太さ (px)。0 = 非表示

        static constexpr int kDecorationMargin = 2;
        bool disable_auto_text_decoration = false;

        // ---------- カーソル (挿入位置) 関連 ----------
        std::vector<CursorSlot> cursor_slots;
        int cursor_index = 0;
        bool cursor_visible = false;
        uint16_t cursor_color = PICO_BLACK;
        int cursor_width = 1;
        Rect prev_cursor_rect;

        bool cursor_blink_enabled = false;
        unsigned long cursor_blink_interval_ms = 500;
        unsigned long cursor_last_blink_ms = 0;

        // ---------- 内部ヘルパー関数 ----------
        static int utf8CharLen(uint8_t lead);
        static std::vector<FixedString<5>> splitChars(const FixedString<PICO_STR_LL>& s);
        std::vector<TextRun<>> parseMarkup(const FixedString<PICO_STR_LL>& src);
        void relayout();
        void relayoutPlaceholder();
        void computeLineOffsets(const std::vector<std::vector<TextRun<>>>& src_lines, int box_width, std::vector<int>& out);
        void renderRun(const TextRun<>& run, int x, int y);
        void renderBackground();
        void renderBorder();
        void renderCursor();
        void updateCursorBlink();

        static Label& utilityInstance();

    public:
        // コンストラクタ：任意の文字数の FixedString を受け付ける
        template<size_t N = PICO_STR_LL>
        Label(int x, int y, const FixedString<N>& text) {
            this->l_rect.x = x;
            this->l_rect.y = y;
            this->setText(text);
            this->needs_redraw = true;
        }
        
        template<size_t N = PICO_STR_LL>
        Label(const FixedString<N>& text) {
            this->setText(text);
            this->needs_redraw = true;
        }
        
        void render() override;
        void needsRender() override;

        // 軽量な直接描画ユーティリティ
        template<size_t N = PICO_STR_LL>
        static void DrawPlain(FontFn::FontSize size, int8_t color, int x, int y, int maxWidth, const FixedString<N>& text);
        static int GetLineHeight(FontFn::FontSize size);

        // setter / getter
        template<size_t N = PICO_STR_LL>
        void setText(const FixedString<N>& text);
        FixedString<PICO_STR_LL> getText();

        template<size_t N = PICO_STR_LL>
        void setPlaceholder(const FixedString<N>& text);
        FixedString<PICO_STR_LL> getPlaceholder();

        void setPlaceholderColor(int8_t color);
        void setMaxWidth(int width);
        int getMaxWidth();
        void setMaxHeight(int height);
        int getMaxHeight();
        void setDefaultHeight(int height);
        int getDefaultHeight();
        void setLineSpacing(int spacing);
        void setTextAlign(TextAlign align);
        TextAlign getTextAlign();
        void setTextColor(int8_t palette_color) override;

        WidgetTools::RenderMode getRenderMode() const override { return this->has_background ? WidgetTools::OPAQUE : WidgetTools::CLEAR; }

        // 背景・ボーダー関連
        void setBackgroundColor(int8_t palette_color) override;
        bool hasBackground();
        void setNoBackground();
        void setBorder(int8_t color, int width);
        void setBorderColor(int8_t palette_color) override {
            this->border_color = palette_color;
            this->needsRender();
        }
        void setBorderWidth(int width);
        int getBorderWidth();

        // カーソル関連
        void setCursorPos(int index);
        int getCursorPos();
        void setCursorMove(int delta);
        void setCursorToEnd();
        void setCursorVisible(bool visible);
        bool getCursorVisible();
        void setCursorBlink(bool enabled, unsigned long interval_ms = 500);
        bool getCursorBlink();
        void setCursorColor(uint16_t c);
        int getTextLength();
        int getCursorScreenX();
        int getCursorScreenY();

        void setFontSize(FontFn::FontSize size) override {
            this->f_size = size;
            this->relayout();
        }

        void setDisableAutoTextDecoration(bool value){
            this->disable_auto_text_decoration = value;
            this->relayout();
        }
        bool getDisableAutoTextDecoration(){
            return this->disable_auto_text_decoration;
        }
};
