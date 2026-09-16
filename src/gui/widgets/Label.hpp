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
    // 文字列を持たず、元テキスト(Labelのraw_text / placeholder_text)上の範囲を参照する。
    //
    // 以前は FixedString<PICO_STR_LL> を埋め込んでいたため、中身が1文字でも
    // 1ランあたり208Bを占めていた。ランは行数×ラン数ぶん作られるので、
    // 折り返しの多いLabelではここが確保量の大半になっていた。
    // 参照方式にできるのは、マークアップ記号はラン境界にしか現れず、
    // 1つのランは必ず元テキストの連続した範囲になるため。
    uint16_t srcOffset = 0; // 元テキスト先頭からのバイト位置
    uint16_t srcLength = 0; // バイト数
    uint16_t line = 0;      // このランが属する行番号

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
    uint16_t line = 0;      // 対応する行番号(line_startsのインデックス)
    int16_t x = 0;          // その行内でのX座標(rect.x からの相対値)
    // このスロットが指す挿入位置の、元テキスト(raw_text)先頭からのバイト位置。
    //
    // スロットの並び順(cursor_index)と元テキストの文字位置は一致しない。
    // マークアップ記号(**や~)はparseMarkup()が読み飛ばすためスロットを持たず、
    // 「元テキストのn文字目」と「n番目のスロット」がずれるため。
    // 入力欄のように元テキスト側の位置でカーソルを扱いたい呼び出し元のために、
    // 変換の基準をここへ持たせてある(setCursorToByteOffset/getCursorByteOffset)。
    uint16_t src_offset = 0;
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
        // 解析・折返し後の行データ。
        //
        // 以前は std::vector<std::vector<TextRun>> で行ごとにvectorを持っていたため、
        // 行数ぶん確保が走っていた。全ランを1本に連結し、行番号はTextRun側に持たせる。
        // ランは行番号の昇順に並ぶので、描画は先頭から舐めながら行が変わったら改行すればよい。
        // 空行(空段落)はランを持たないため、行数は別に数える必要がある。
        std::vector<TextRun> runs_flat;
        uint16_t line_count = 0;
        std::vector<TextRun> placeholder_runs_flat;
        uint16_t placeholder_line_count = 0;

        FixedString<N> placeholder_text;
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
        int border_width = 0;                       // ボーダーの太さ(px)。0 = 非表示

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
        // srcはNUL終端でなくてよい(raw_textの部分文字列をコピーせず渡すため、長さを明示する)。
        // base_offsetは src が元テキストの何バイト目を指しているか。
        // TextRunのsrcOffsetは元テキスト先頭からの絶対位置で持つ
        std::vector<TextRun> parseMarkup(const char* src, size_t n, uint16_t base_offset);
        void relayout();
        void relayoutPlaceholder();

        // ---------- レイアウトの遅延解決 ----------
        // レイアウトに影響するsetterはこのフラグを立てるだけにして、実際のrelayout()は
        // レイアウト結果が必要になった時点(ensureLayout)で1回だけ走らせる。
        //
        // 以前はsetterごとに毎回relayout()していたため、例えばMarkdownView::bindLabelSlot()は
        // setMaxWidth -> setFontSize -> setText と呼ぶだけで3回フルレイアウトが走り、
        // しかも最初の2回は更新前のテキストに対する計算なので全部捨てられていた。
        // relayout()は1文字ごとにtextWidth()を呼ぶので、回数はそのままCPUに効く。
        mutable bool needs_relayout = true;

        // カーソル位置テーブル(cursor_slots)を作るかどうか。
        // 1文字につき1エントリ積むため日本語120字で約2KBかかるが、必要なのは
        // 入力欄として使われるLabelだけ。MarkdownViewのラベルプールやStatusbarの
        // ラベルはカーソルを一生使わないので、その分の確保と計算をまるごと省く。
        // カーソル系のAPIが呼ばれた時点で自動的に有効化される
        bool cursor_tracking = false;

        // レイアウトが古ければ計算し直す。const getterからも呼ぶためconstにしてある
        void ensureLayout() const;

        // レイアウト結果を無効化する(レイアウトに影響するsetterから呼ぶ)
        void invalidateLayout();

        // ensureLayout()を通さずに、再計算前の(=いま画面に出ている)スクリーン矩形を返す。
        // invalidateLayout()が「消すべき古い領域」をdirty登録するために使う。
        // ここでgetScreenRect()を使うとensureLayout()が走ってしまい遅延の意味が無くなる
        Rect staleScreenRect() const;
        void computeLineOffsets(const std::vector<TextRun>& src_runs, int src_line_count,
                                 int box_width, std::vector<int>& out);

        // src_bufはrunが参照している元テキストのバッファ(raw_text または placeholder_text)。
        // 非constなのは、print()がNUL終端を要求するため描画の間だけ終端を差し替えるから
        void renderRun(char* src_buf, const TextRun& run, int x, int y);
        void renderBackground();
        void renderBorder();
        void renderCursor();
        void updateCursorBlink();

        static Label<PICO_STR_LL>& utilityInstance();

    protected:
        // カーソル位置テーブルの構築を有効にする。
        // 入力欄として使うことが分かっている場合(Textbox)はコンストラクタで呼んでおくと、
        // 描画中に有効化されてレイアウトがやり直しになるのを避けられる
        void enableCursorTracking();

    public:
        // 注意: メンバテンプレート(template<size_t M>)はLabel.cpp側で個別インスタンス化していないため、
        // クラス本体内でインライン定義しておく(呼び出し側で使われた組み合わせごとに暗黙インスタンス化させる)。
        // Label.cpp末尾の`template class Label<N>;`はメンバテンプレートまでは実体化しない点に注意。
        template<size_t M>
        Label(int x, int y, const FixedString<M>& text) {
            this->l_rect.x = x;
            this->l_rect.y = y;
            this->setText(text);
            this->needs_redraw = true;
        }
        Label(int x, int y, const char* text = "");
        template<size_t M>
        Label(const FixedString<M>& text) : Label(0, 0, text) {}
        Label(const char* text);

        void render() override;
        void needsRender() override;

        // レイアウト結果(l_rect.w/h)を読む経路。遅延した再計算をここで解決する。
        // getScreenRect()/hitTest()も基底経由でgetLocalRect()を呼ぶのでまとめて効く
        Rect getLocalRect() const override;
        int getW() override;
        int getH() override;

        WidgetType getWidgetType() const override { return WidgetType::Label; }

        template<size_t M>
        static void DrawPlain(FontFn::FontSize size, int8_t color, int x, int y, int maxWidth, const FixedString<M>& text) {
            DrawPlain(size, color, x, y, maxWidth, text.c_str());
        }
        static void DrawPlain(FontFn::FontSize size, int8_t color, int x, int y, int maxWidth, const char* text);
        static int GetLineHeight(FontFn::FontSize size);

        // ---------- setter / getter ----------
        template<size_t M>
        void setText(const FixedString<M>& text) {
            //const char*版と同じ理由で、変化が無ければ再レイアウトしない
            if(this->raw_text == text) return;

            this->raw_text.assign(text);
            invalidateLayout();
        }
        void setText(const char* text);
        const FixedString<N>* getText() const { return &this->raw_text; }
        FixedString<N>* getText() { return &this->raw_text; }

        template<size_t M>
        void setPlaceholder(const FixedString<M>& text) {
            this->placeholder_text.assign(text);
            invalidateLayout();
        }
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

        void setBorderColor(int8_t palette_color) override;
        void setBorder(int8_t color, int width = 1);
        void setBorderWidth(int width);
        int getBorderWidth();

        void setCursorPos(int index);
        int getCursorPos();

        // 元テキスト(getText())上のバイト位置でカーソルを置く / 読み出す。
        // マークアップ記号は表示されずスロットを持たないので、指定位置に
        // 対応するスロットが無い場合は「その位置を超えない最後のスロット」に寄せる
        // (記号自体は描画されないため、見た目の位置は一致する)。
        void setCursorToByteOffset(size_t byte_offset);
        size_t getCursorByteOffset();
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
            this->invalidateLayout();
        }

        void setDisableAutoTextDecoration(bool value){
            this->disable_auto_text_decoration = value;
            this->invalidateLayout();
        }
        bool getDisableAutoTextDecoration(){
            return this->disable_auto_text_decoration;
        }
};
