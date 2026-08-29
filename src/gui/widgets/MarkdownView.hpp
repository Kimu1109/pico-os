#pragma once

#include <vector>
#include "gui/widgets/Widget.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/Image.hpp"
#include "gui/widgets/Icon.hpp"
#include "gui/icons/icons_data.h"
#include "Arduino.h"

// テーブルの最大列数。240px幅の画面で可読性を保てる範囲として4に制限しており、
// これを超える列は末尾から切り捨てる（MdBlockの固定長配列サイズにも使用するため
// クラス外に定義する）。
static constexpr int kMdTableMaxCols = 4;

enum class MdBlockType : uint8_t {
    H1, H2, H3,
    Paragraph,
    Image,
    Link,           // 行全体が [text](url) のみのブロック
    CodeBlock,      // ```で囲まれたコードブロック
    ListItem,       // リスト項目（箇条書き / 番号付き / チェックボックス、ネスト対応）
    HorizontalRule, // 水平線（***, ---, ___ のいずれか3文字以上）
    Quote,          // 引用（`>`、ネスト対応）
    TableRow,       // テーブルの1行分（ヘッダ行 or データ行）
};

struct MdBlock {
    MdBlockType type;
    uint16_t srcOffset;
    uint16_t srcLength;
    uint16_t urlOffset = 0;   // Link用、および段落/見出し/リスト項目/引用内に
                              // インラインリンクが見つかった場合の1件目用
    uint16_t urlLength = 0;   // 0 = リンク無し
    uint8_t  listIndent = 0;  // ListItem用：ネストの深さ（0始まり）
    bool     listOrdered = false;    // ListItem用：番号付きリストかどうか
    uint16_t listNumber = 0;         // ListItem用：番号付きの場合の表示番号
    bool     listIsCheckbox = false; // ListItem用：チェックボックス項目かどうか（イミュータブル、表示のみ）
    bool     listChecked = false;    // ListItem用：チェック済みかどうか（listIsCheckbox時のみ有効）
    uint8_t  quoteDepth = 0;         // Quote用：ネストの深さ（0始まり。`>`の重ね数-1）

    // ---------- TableRow用 ----------
    uint8_t  tableColCount = 0;      // 実際の列数（kMdTableMaxCols以下）
    bool     tableIsHeader = false;  // ヘッダ行かどうか
    // 列ごとの寄せ(0=left,1=center,2=right)。テーブルはLabelを介さずframeへ直接描画する
    // ため、renderDecorations()内でこの値をもとに実際に寄せて描画する。
    uint8_t  tableAlign[kMdTableMaxCols] = {};
    uint16_t tableCellOffset[kMdTableMaxCols] = {};
    uint16_t tableCellLength[kMdTableMaxCols] = {};

    int32_t  y;
    uint16_t height;
};

class MarkdownView : public Widget {
    private:
        static constexpr int kLabelPoolSize = 16;
        static constexpr int kImagePoolSize = 2;
        static constexpr int kMaxBlocks     = 128;
        static constexpr int kMaxSourceBytes = 16384;
        static constexpr int kPadding       = 4;
        static constexpr int kBlockSpacing  = 6;
        static constexpr int SCROLL_L       = 15; // ScrollContainerと同じ見た目に揃える

        // ---------- リスト関連 ----------
        static constexpr int kMaxListLevels   = 6;  // ネストの最大段数（番号カウンタ配列のサイズ。引用の深さもこれで丸める）
        static constexpr int kListIndentWidth = 12; // 1段あたりのインデント幅(px)

        // ---------- チェックボックス関連 ----------
        // イミュータブル表示専用。タップでの状態変更は一切行わない。
        static constexpr int kCheckboxIconPoolSize = kLabelPoolSize; // 表示中のチェックボックス項目数はLabelスロット数を超えない
        static constexpr IconSize kCheckboxIconSize = IconSize::Px16;
        static constexpr int kCheckboxIconGap = 4; // アイコンとテキストの間隔(px)

        // ---------- 引用関連 ----------
        static constexpr int kQuoteIndentWidth = 10; // 1段あたりのインデント幅(px)。バーの描画スペースも含む
        static constexpr int kQuoteBarWidth    = 2;  // 縦バーの太さ(px)
        static constexpr int8_t kQuoteBarColor  = PICO_DARKGREY;
        static constexpr int8_t kQuoteTextColor = PICO_DARKGREY;

        // ---------- 水平線関連 ----------
        static constexpr int kHrHeight = 16; // 罫線自体が占める行の高さ（上下の余白込み）
        static constexpr int8_t kHrColor = PICO_DARKGREY;

        // ---------- テーブル関連 ----------
        // ラベルウィジェットは使わず、罫線と同様にrender()内でframeへ直接描画する
        // （マークアップ解釈や折返しといった装飾は行わない、1行のみの軽量描画）。
        static constexpr int kTableCellPadding  = 3;   // セルの上下左右の余白(px)
        static constexpr int kTableMinRowHeight = 16;  // 行の最低高さ(px)
        static constexpr int8_t kTableLineColor = PICO_BLACK;      // 罫線の色
        static constexpr int8_t kTableHeaderBgColor = PICO_LIGHTGREY; // ヘッダ行の背景色
        static constexpr int8_t kTableTextColor = PICO_BLACK;      // セル文字色

        String doc_text;
        std::vector<MdBlock> blocks;
        int32_t total_height = 0;

        // ウィジェットプール（固定長・起動時に一度だけ確保）
        Label* labelPool[kLabelPoolSize];
        Image* imagePool[kImagePoolSize];
        Icon*  checkboxIconPool[kCheckboxIconPoolSize];

        int boundLabelBlock[kLabelPoolSize];   // 現在そのスロットが表示しているblockのindex(-1=未使用)
        int boundImageBlock[kImagePoolSize];
        int boundCheckboxIconBlock[kCheckboxIconPoolSize];

        int checkboxIconPx = 0; // チェックボックスアイコン1辺のピクセルサイズ（起動時にキャッシュ）

        Label* measure_label; // レイアウト計算専用（レンダリングツリーには含めない）

        std::vector<Widget*> children_; // getChildren()用（プール全部への参照）

        // スクロール状態
        int sy = 0;
        int s_scroll_y = 0;
        int scroll_y = 0;
        int max_scroll_y = 0;
        bool is_scrolling = false;
        unsigned long last_scroll_render_ms = 0;

        void parseBlocks();
        void layoutBlocks();
        void bindVisibleBlocks(bool force);
        void bindLabelSlot(int slot, int blockIdx, bool force);
        void bindImageSlot(int slot, int blockIdx, bool force);
        void bindCheckboxIconSlot(int slot, int blockIdx, bool force);
        void hideLabelSlot(int slot);
        void hideImageSlot(int slot);
        void hideCheckboxIconSlot(int slot);
        FontFn::FontSize fontSizeForBlock(MdBlockType type) const;

        // ---------- インライン要素（コード/リンク）認識 ----------
        // src中の `code` を Labelの波線(~)装飾へ、[text](url) を下線(_)装飾へ変換した
        // 表示用テキストを生成する。改行をまたぐ組は無効として素通りさせる。
        String applyInlineMarkdown(const String& src) const;
        // doc_text の [start, end) 範囲内で最初に見つかった [text](url) の
        // URL部分のオフセット/長さ(doc_text基準)を取得する。見つからなければfalse。
        bool findFirstInlineLink(int start, int end, uint16_t& urlOffOut, uint16_t& urlLenOut) const;

        // ---------- リスト行の判定 ----------
        // 行がリスト項目(箇条書き/番号付き)かどうかを判定し、
        // ネスト段数・順序付きかどうか・番号・内容の開始位置・
        // チェックボックス項目かどうか(イミュータブル表示)を返す。
        bool tryParseListItem(int lineStart, int lineEnd, int& indentOut, bool& orderedOut,
                               int& numberOut, int& contentStartOut,
                               bool& isCheckboxOut, bool& checkedOut) const;

        // ---------- 水平線の判定 ----------
        // 行が `*`/`-`/`_` のいずれか1種類の文字（空白を挟んでも良い）のみから成り、
        // かつその文字が3個以上含まれる場合に水平線と判定する（CommonMark準拠）。
        bool tryParseThematicBreak(int lineStart, int lineEnd) const;

        // ---------- 引用行の判定 ----------
        // 行頭（最大3個までの半角スペースの後）に `>` が1個以上連続している場合に
        // 引用行と判定し、ネスト段数と内容の開始位置を返す。`>` の直後の半角スペース
        // 1個は区切りとして消費する（`>>text` のようにスペース無しでも許容）。
        bool tryParseBlockquote(int lineStart, int lineEnd, int& depthOut, int& contentStartOut) const;

        // ---------- 先頭メタデータ(フロントマター)の無視 ----------
        // Hugo/md-to-pdf等が使う「先頭行が `---` のみ、以降 `---` または `...` のみの
        // 行が現れるまでがYAMLメタデータ」という規約に従い、該当範囲をスキップする。
        // 先頭行が `---` のみでない場合や、終端フェンスが見つからない場合は
        // 何もスキップせずstartPosをそのまま返す（安全側）。
        int skipFrontMatter(int startPos, int len) const;

        // ---------- テーブルの判定・分割 ----------
        // 区切り行（例: `| --- | :---: | ---: |`）かどうかを判定する。各セルが
        // 「(空白)(任意の:)(1個以上の-)(任意の:)(空白)」のみで構成されることを要求し、
        // 列数(kMdTableMaxCols超は切り捨て)と、列ごとの寄せ(0=left,1=center,2=right)を返す。
        bool tryParseTableDelimiterRow(int lineStart, int lineEnd, uint8_t& colCountOut,
                                        uint8_t alignOut[kMdTableMaxCols]) const;
        // ヘッダ候補行(headerStart, headerEnd)の直後の行が区切り行として成立するかを判定する。
        // 成立する場合、列数・寄せ・区切り行の終端位置(delimLineEndOut)を返す。
        bool detectTableStart(int headerStart, int headerEnd, int len, uint8_t& colCountOut,
                               uint8_t alignOut[kMdTableMaxCols], int& delimLineEndOut) const;
        // 行を`|`区切りでセルに分割する。先頭/末尾の`|`は区切りとして扱い、`\|`は
        // エスケープされたパイプ文字として分割対象から除外する。各セルの前後の空白は
        // トリムする。maxColsを超えるセルは切り捨てる。見つかったセル数をcellCountOutに返す
        // （maxColsに満たない分はoffset/length共に0のままとなり、空セルとして扱われる）。
        void splitTableRow(int lineStart, int lineEnd, uint16_t cellOffsetOut[kMdTableMaxCols],
                            uint16_t cellLengthOut[kMdTableMaxCols], uint8_t maxCols, uint8_t& cellCountOut) const;
        // テーブルセル1つ分の表示用テキストを生成する。`\|`のアンエスケープのみを行い、
        // `code`や[text](url)等のインライン装飾はそのまま素通しする（テーブルはLabelを介さず
        // frameへ直接print()するため、マークアップは解釈されない）。
        String formatTableCellText(int offset, int length) const;

        String formatBlockText(const MdBlock& b) const;
        int findBlockAtScreenY(int screenY) const;       // タップ位置→ブロック特定

        // ---------- 装飾の直接描画 ----------
        // Labelプールに載せない要素（水平線の罫線、引用の縦バー、テーブルの罫線）を
        // render()内でビューポート走査して直接描画する。
        void renderDecorations();

        // タップ判定用（is_scrollingがfalseのケースのドラッグ距離を見る）
        int press_start_x = 0;
        int press_start_y = 0;
        bool moved_beyond_threshold = false;
        static constexpr int kTapThreshold = 6; // px

        std::function<void(String)> on_link_tap = nullptr;

    public:

        MarkdownView(int16_t x, int16_t y, int16_t w, int16_t h);

        bool load(const String& path);

        void render() override;

        void causeOnPressStart() override;
        void causeOnPressMove() override;
        void causeOnPressEnd() override;

        void setOnLinkTap(std::function<void(String)> callback) {
            this->on_link_tap = callback;
        }
        void clearOnLinkTap(){
            this->on_link_tap = nullptr;
        }

        const std::vector<Widget*>& getChildren() const override {
            return children_;
        }

        Rect getScreenClipRect() const override {
            Rect dst = this->getScreenRect();
            dst.w -= SCROLL_L;
            if (parent) {
                return parent->getScreenClipRect().intersection(dst);
            }
            return dst;
        }

        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::OPAQUE; }

        int getScrollOffsetY() const override { return scroll_y; }
};
