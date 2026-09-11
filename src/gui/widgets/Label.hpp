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

// 1つの書式区間（同じ太字/下線/波線/取り消し線設定を持つ文字列断片）
// マークアップ対応表:
//   **text**  -> bold（太字）
//   _text_ / *text* -> underline（直線下線。標準Markdownのイタリック相当だが
//                       描画コストの都合でイタリックの代わりに直線下線を採用している）
//   ~text~    -> wavy（波線下線。標準構文には存在しない独自の装飾）
//   ~~text~~  -> strikethrough（取り消し線。標準Markdownの打ち消し線に対応）
struct TextRun {
    FixedString<PICO_STR_LL> text;
    bool bold = false;
    bool underline = false;
    bool wavy = false;
    bool strikethrough = false;
    // text (bold分の+1を含まない素の文字幅)をpx単位でrelayout()時に一度だけ計算しておく。
    // computeLineOffsets()やrender()で毎回 frame->textWidth(text.c_str()) を再計算すると
    // 同じ文字列を1つのLabelにつき最大3〜4回測定することになるため、ここにキャッシュして使い回す。
    int width = 0;
};

// カーソル(挿入位置)候補1つ分の描画座標
// relayout()時に、文字境界ごとの「そこにカーソルを置いたときの座標」を記録しておく
struct CursorSlot {
    int line = 0;   // 対応する行番号(lines配列のインデックス)
    int x = 0;      // その行内でのX座標(rect.x からの相対値)
};

// テキストの水平方向の揃え位置
// 注意: max_width未指定(=0)の場合、l_rect.wは最も長い行の幅に自動フィットするため、
// 最長行に対してはCenter/Rightを指定しても見た目上の変化はない
// (他の短い行だけが最長行の幅を基準に寄せられる)。
// 単一行ラベルで視覚的な効果を出したい場合はsetMaxWidth()等で明示的に
// 描画幅を確保すること。
enum class TextAlign {
    Left,
    Center,
    Right
};

template<size_t N>
class Label : public Widget, public IFontImplementation, public IBorderColor, public ITextColor {
    private:
        FixedString<N> raw_text;                          // マークアップ込みの元テキスト
        std::vector<std::vector<TextRun>> lines;   // 解析・折返し後の行データ
        std::vector<std::vector<TextRun>> placeholder_lines;

        FixedString<N> placeholder_text = "";
        int8_t placeholder_color = PICO_LIGHTGREY;

        int max_width = 0;                         // 0 = 折り返し無効（\nのみ改行）
        int max_height = 0;                        // 0 = 高さ上限無効。超過分は切り詰めて非表示にする
        int default_height = 0;                     // 0 = 下限無効。行数由来の高さがこれより小さい場合はこちらを採用
        int line_height = 0;
        int line_spacing = 0;                      // 行間ピクセル数（デフォルト0）
        TextAlign text_align = TextAlign::Left;    // 行揃え（デフォルト左揃え）
        std::vector<int> line_offsets;             // 各行の描画開始Xオフセット（relayout時に更新）
        std::vector<int> placeholder_line_offsets; // placeholder各行のXオフセット

        bool disable_auto_text_decoration = false; // マークアップ自動装飾の無効化フラグ
        bool has_background = false;

        // ---------- カーソル（挿入位置）管理 ----------
        int cursor_index = 0;                      // 現在のカーソル位置（0 = テキスト先頭、N = N文字目の直後）
        bool cursor_visible = false;               // 現在の描画フレームでカーソルを描くか
        bool cursor_blink_enabled = false;         // 自動点滅を有効にするか
        unsigned long cursor_blink_interval_ms = 500; // 点滅間隔(ms)
        unsigned long cursor_last_blink_ms = 0;
        uint16_t cursor_color = PICO_BLACK;
        std::vector<CursorSlot> cursor_slots;      // 各カーソル位置の描画スロット（relayout時に算出）
        Rect prev_cursor_rect = {0, 0, 0, 0};      // 前回描画したカーソルの領域（差分消去用）

        static constexpr int kCursorWidth = 1;     // カーソル縦線の幅(px)
        static constexpr int kDecorationMargin = 2;// 下線・波線が文字の下にはみ出す余白(px)

        // ---------- 内部ヘルパー関数 ----------
        static int utf8CharLen(uint8_t lead);
        static std::vector<FixedString<5>> splitChars(const char* s);
        std::vector<TextRun> parseMarkup(const char* src);
        void relayout();
        void relayoutPlaceholder();
        void computeLineOffsets(const std::vector<std::vector<TextRun>>& src_lines, int box_width, std::vector<int>& out);

        void renderRun(const TextRun& run, int x, int y);
        void renderBackground();
        void renderBorder();
        void renderCursor();
        void updateCursorBlink();

        static Label<PICO_STR_LL>& utilityInstance();

    public:
        template<size_t M>
        Label(int x, int y, const FixedString<M>& text);
        Label(int x, int y, const char* text = "");
        template<size_t M>
        Label(const FixedString<M>& text);
        Label(const char* text);
        
        void render() override;
        void needsRender() override;

        WidgetType getWidgetType() const override { return WidgetType::Label; }

        template<size_t M>
        static void DrawPlain(FontFn::FontSize size, int8_t color, int x, int y, int maxWidth, const FixedString<M>& text);
        static void DrawPlain(FontFn::FontSize size, int8_t color, int x, int y, int maxWidth, const char* text);
        static int GetLineHeight(FontFn::FontSize size);

        // ---------- setter / getter ----------
        template<size_t M>
        void setText(const FixedString<M>& text);
        void setText(const char* text);
        const FixedString<N>* getText() const { return &this->raw_text; }
        FixedString<N>* getText() { return &this->raw_text; }

        template<size_t M>
        void setPlaceholder(const FixedString<M>& text);
        void setPlaceholder(const char* text);
        const FixedString<N>* getPlaceholder() const { return &this->placeholder_text; }
        FixedString<N>* getPlaceholder() { return &this->placeholder_text; }

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

        void setTextColor(int8_t c);

        void setBackgroundColor(int8_t palette_color);
        bool hasBackground();
        void setNoBackground();

        void setBorder(int8_t color, int width = 1);
        void setBorderWidth(int width);
        int getBorderWidth();

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
