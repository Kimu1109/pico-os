#pragma once

#include <vector>
#include "gui/widgets/Widget.hpp"
#include "consts.hpp"
#include "Arduino.h"
#include "functions/Font_Functions.hpp"
#include "gui/widgets/interfaces/IFontImplementation.hpp"
#include "gui/widgets/interfaces/IBorderColor.hpp"
#include "gui/widgets/interfaces/ITextColor.hpp"

// 1つの書式区間（同じ太字/下線/波線/取り消し線設定を持つ文字列断片）
// マークアップ対応表:
//   **text**  -> bold（太字）
//   _text_ / *text* -> underline（直線下線。標準Markdownのイタリック相当だが
//                       描画コストの都合でイタリックの代わりに直線下線を採用している）
//   ~text~    -> wavy（波線下線。標準構文には存在しない独自の装飾）
//   ~~text~~  -> strikethrough（取り消し線。標準Markdownの打ち消し線に対応）
struct TextRun {
    String text;
    bool bold = false;
    bool underline = false;
    bool wavy = false;
    bool strikethrough = false;
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

class Label : public Widget, public IFontImplementation, public IBorderColor, public ITextColor {
    private:
        String raw_text;                          // マークアップ込みの元テキスト
        std::vector<std::vector<TextRun>> lines;   // 解析・折返し後の行データ
        std::vector<std::vector<TextRun>> placeholder_lines;

        String placeholder_text = "";
        int8_t placeholder_color = PICO_LIGHTGREY;

        int max_width = 0;                         // 0 = 折り返し無効（\nのみ改行）
        int max_height = 0;                        // 0 = 高さ上限無効。超過分は切り詰めて非表示にする
        int default_height = 0;                     // 0 = 下限無効。行数由来の高さがこれより小さい場合はこちらを採用
        int line_height = 0;
        int line_spacing = 2;                      // 行間(px)

        TextAlign text_align = TextAlign::Left;
        // relayout()/relayoutPlaceholder()内で行ごとに事前計算される、
        // 各行の描画開始X座標オフセット(rect.x からの相対値)。
        // lines / placeholder_lines と同じインデックスで対応する。
        std::vector<int> line_offsets;
        std::vector<int> placeholder_line_offsets;

        // ---------- 背景・ボーダー関連 ----------
        bool has_background = false;                // 背景を描画するか（false = 透明）
        int border_width = 0;                        // ボーダーの太さ(px)。0 = 非表示

        // 波線装飾用のマージン
        static constexpr int kDecorationMargin = 2;

        // 装飾の有無
        bool disable_auto_text_decoration = false;

        // ---------- カーソル(挿入位置)関連 ----------
        std::vector<CursorSlot> cursor_slots;      // 文字境界(0文字目の手前〜末尾)ごとの座標一覧
        int cursor_index = 0;                      // 現在のカーソル位置(cursor_slotsのインデックス)
        bool cursor_visible = false;                // カーソルを表示するか（点滅制御は呼び出し側で行う）
        uint16_t cursor_color = PICO_BLACK;         // カーソルの色
        int cursor_width = 1;                       // カーソルの太さ(px)
        Rect prev_cursor_rect;                      // 前回描画したカーソル位置（消去用）

        bool cursor_blink_enabled = false;          // 点滅を有効にするか
        unsigned long cursor_blink_interval_ms = 500; // 点滅間隔(ms)
        unsigned long cursor_last_blink_ms = 0;       // 最後に表示状態を切り替えた時刻(millis())

        // ---------- 内部ヘルパー関数 ----------
        static int utf8CharLen(uint8_t lead);
        static std::vector<String> splitChars(const String& s);
        std::vector<TextRun> parseMarkup(const String& src);
        void relayout();
        void relayoutPlaceholder();
        // linesの各行の実測幅からtext_alignに応じたオフセットを算出し、outに書き込む。
        // box_widthは揃えの基準となる幅(通常はthis->l_rect.w)。
        void computeLineOffsets(const std::vector<std::vector<TextRun>>& src_lines, int box_width, std::vector<int>& out);
        void renderRun(const TextRun& run, int x, int y);
        void renderBackground();
        void renderBorder();
        void renderCursor();
        void updateCursorBlink();

        // DrawPlain()/GetLineHeight()が使い回す、遅延初期化された専用Labelインスタンス。
        // 静的ローカル変数として実装することで、PICO_GFX::Setup()より前に
        // グラフィック関連オブジェクトが構築されてしまう問題を避ける
        // （「静的初期化順序」の既知の注意点に合わせた遅延初期化）。
        static Label& utilityInstance();

    public:
        Label(int x, int y, String text);
        Label(String text);
        
        void render() override;
        void needsRender() override;

        // ---------- 軽量な直接描画ユーティリティ ----------
        // テーブルセルのような、マークアップ解釈・ワードラップ・カーソル/プレースホルダー等の
        // 状態を一切持たない軽量描画が必要な用途向け。ウィジェットツリーには参加せず、
        // 内部で使い回す専用のLabelインスタンス(utilityInstance())を介して
        // fontApply()/fontDefault()/textColorApply()/textColorDefault()を呼び出すことで、
        // 通常のLabelと全く同じフォント・色の適用ロジックを再利用しつつ、
        // 太字/下線/波線などの装飾やマークアップ解釈、複数行折返しは一切行わない
        // （1行分をそのままframeへ描画するのみ）。
        // maxWidthを1以上指定すると、その幅でclipRectを設定してから描画し、
        // 超過分を切り詰める（0以下でクリップ無効）。
        static void DrawPlain(FontFn::FontSize size, int8_t color, int x, int y, int maxWidth, const String& text);
        // 指定フォントサイズの行の高さ(px)を取得する（実際にfontApply()した状態でfontHeight()を取得する）。
        static int GetLineHeight(FontFn::FontSize size);

        // ---------- setter / getter ----------
        void setText(String text);
        String getText();

        void setPlaceholder(String text);
        String getPlaceholder();

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

        WidgetTools::RenderMode getRenderMode() const override { return this->has_background ? WidgetTools::OPAQUE : WidgetTools::CLEAR; } //has→不透明, !has→透明

        // ---------- 背景・ボーダー関連 ----------
        // 背景色を設定して有効化する（指定しない場合はデフォルトで透明）
        void setBackgroundColor(int8_t palette_color) override;
        bool hasBackground();
        void setNoBackground();   // 背景を透明に戻す

        // ボーダー（色・太さ）。width=0でボーダー無し
        void setBorder(int8_t color, int width);
        void setBorderColor(int8_t palette_color) override {
            this->border_color = palette_color;
            this->needsRender();
        }
        void setBorderWidth(int width);
        int getBorderWidth();

        // ---------- カーソル(挿入位置)関連 ----------
        // index: 0 = 先頭。マークアップ記号(**, _, *, ~, ~~)は文字数に含まれない
        void setCursorPos(int index);
        int getCursorPos();

        // 現在位置からの相対移動（+1で1文字右、-1で1文字左）
        void setCursorMove(int delta);

        // カーソルを文字列の末尾に移動する
        void setCursorToEnd();

        // 表示ON/OFF。手動で常時表示/非表示にしたい場合に使う
        // (CursorBlink()で自動点滅を有効にしている間は、ここでの指定は
        //  次の点滅タイミングで上書きされる点に注意)
        void setCursorVisible(bool visible);
        bool getCursorVisible();

        // カーソルの自動点滅をLabel内部で完結させる。
        // enabled=trueで有効化すると、まず表示状態から開始しinterval_msごとに
        // render()呼び出し内で自動的に表示/非表示が切り替わる。
        // enabled=falseで無効化すると同時にカーソルは非表示になる。
        void setCursorBlink(bool enabled, unsigned long interval_ms = 500);
        bool getCursorBlink();

        void setCursorColor(uint16_t c);

        // カーソルが取り得る最大インデックス（＝現在挿入可能な文字数）
        int getTextLength();

        // 現在のカーソル位置の絶対画面座標（候補ウィンドウの位置決め等に利用可能）
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
