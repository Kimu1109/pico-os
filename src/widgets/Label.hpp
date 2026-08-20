#pragma once

#include <vector>
#include "widgets/Widget.hpp"
#include "consts.hpp"
#include "Arduino.h"
#include "functions/Font_Functions.hpp"
#include "widgets/interfaces/IFontImplementation.hpp"
#include "widgets/interfaces/IBorderColor.hpp"
#include "widgets/interfaces/ITextColor.hpp"

// 1つの書式区間（同じ太字/下線/波線設定を持つ文字列断片）
struct TextRun {
    String text;
    bool bold = false;
    bool underline = false;
    bool wavy = false;
};

// カーソル(挿入位置)候補1つ分の描画座標
// relayout()時に、文字境界ごとの「そこにカーソルを置いたときの座標」を記録しておく
struct CursorSlot {
    int line = 0;   // 対応する行番号(lines配列のインデックス)
    int x = 0;      // その行内でのX座標(rect.x からの相対値)
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

        // ---------- 背景・ボーダー関連 ----------
        bool has_background = false;                // 背景を描画するか（false = 透明）
        int border_width = 0;                        // ボーダーの太さ(px)。0 = 非表示

        // 波線装飾用のマージン
        static constexpr int kDecorationMargin = 2;

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
        void renderRun(const TextRun& run, int x, int y);
        void renderBackground();
        void renderBorder();
        void renderCursor();
        void updateCursorBlink();

    public:
        using Widget::Visible;
        using Widget::BackgroundColor;

        Label(int x, int y, String text);
        Label(String text);
        
        void render() override;
        void needsRender() override;

        // ---------- setter / getter ----------
        void Text(String text);
        String Text();

        void Placeholder(String text);
        String Placeholder();

        void PlaceholderColor(int8_t color);

        void MaxWidth(int width);
        int MaxWidth();

        void MaxHeight(int height);
        int MaxHeight();

        void DefaultHeight(int height);
        int DefaultHeight();

        void LineSpacing(int spacing);

        void SetTextColor(int8_t palette_color) override;

        WidgetTools::RenderMode GetRenderMode() const override { return this->has_background ? WidgetTools::OPAQUE : WidgetTools::CLEAR; } //has→不透明, !has→透明

        // ---------- 背景・ボーダー関連 ----------
        // 背景色を設定して有効化する（指定しない場合はデフォルトで透明）
        void BackgroundColor(int8_t palette_color) override;
        bool HasBackground();
        void NoBackground();   // 背景を透明に戻す

        // ボーダー（色・太さ）。width=0でボーダー無し
        void Border(int8_t color, int width);
        void SetBorderColor(int8_t palette_color) override {
            this->border_color = palette_color;
            this->needsRender();
        }
        void BorderWidth(int width);
        int BorderWidth();

        // ---------- カーソル(挿入位置)関連 ----------
        // index: 0 = 先頭。マークアップ記号(**, _, ~)は文字数に含まれない
        void CursorPos(int index);
        int CursorPos();

        // 現在位置からの相対移動（+1で1文字右、-1で1文字左）
        void CursorMove(int delta);

        // カーソルを文字列の末尾に移動する
        void CursorToEnd();

        // 表示ON/OFF。手動で常時表示/非表示にしたい場合に使う
        // (CursorBlink()で自動点滅を有効にしている間は、ここでの指定は
        //  次の点滅タイミングで上書きされる点に注意)
        void CursorVisible(bool visible);
        bool CursorVisible();

        // カーソルの自動点滅をLabel内部で完結させる。
        // enabled=trueで有効化すると、まず表示状態から開始しinterval_msごとに
        // render()呼び出し内で自動的に表示/非表示が切り替わる。
        // enabled=falseで無効化すると同時にカーソルは非表示になる。
        void CursorBlink(bool enabled, unsigned long interval_ms = 500);
        bool CursorBlink();

        void CursorColor(uint16_t c);

        // カーソルが取り得る最大インデックス（＝現在挿入可能な文字数）
        int TextLength();

        // 現在のカーソル位置の絶対画面座標（候補ウィンドウの位置決め等に利用可能）
        int CursorScreenX();
        int CursorScreenY();

        void SetFontSize(FontFn::FontSize size) override {
            this->f_size = size;
            this->relayout();
        }
};
