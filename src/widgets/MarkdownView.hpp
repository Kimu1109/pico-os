#pragma once

#include <vector>
#include "widgets/Widget.hpp"
#include "widgets/Label.hpp"
#include "widgets/Image.hpp"
#include "Arduino.h"

enum class MdBlockType : uint8_t {
    H1, H2, H3,
    Paragraph,
    Image,
    Link,       // 行全体が [text](url) のみのブロック
    CodeBlock,  // ```で囲まれたコードブロック
    ListItem,   // 追加：リスト項目（箇条書き / 番号付き、ネスト対応）
};

struct MdBlock {
    MdBlockType type;
    uint16_t srcOffset;
    uint16_t srcLength;
    uint16_t urlOffset = 0;   // Link用、および段落/見出し/リスト項目内に
                              // インラインリンクが見つかった場合の1件目用
    uint16_t urlLength = 0;   // 0 = リンク無し
    uint8_t  listIndent = 0;  // ListItem用：ネストの深さ（0始まり）
    bool     listOrdered = false; // ListItem用：番号付きリストかどうか
    uint16_t listNumber = 0;      // ListItem用：番号付きの場合の表示番号
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
        static constexpr int kMaxListLevels   = 6;  // ネストの最大段数（番号カウンタ配列のサイズ）
        static constexpr int kListIndentWidth = 12; // 1段あたりのインデント幅(px)

        String doc_text;
        std::vector<MdBlock> blocks;
        int32_t total_height = 0;

        // ウィジェットプール（固定長・起動時に一度だけ確保）
        Label* labelPool[kLabelPoolSize];
        Image* imagePool[kImagePoolSize];
        int boundLabelBlock[kLabelPoolSize];   // 現在そのスロットが表示しているblockのindex(-1=未使用)
        int boundImageBlock[kImagePoolSize];

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
        void hideLabelSlot(int slot);
        void hideImageSlot(int slot);
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
        // ネスト段数・順序付きかどうか・番号・内容の開始位置を返す。
        bool tryParseListItem(int lineStart, int lineEnd, int& indentOut, bool& orderedOut,
                               int& numberOut, int& contentStartOut) const;

        std::function<void(String)> on_link_tap = nullptr;

        // タップ判定用（is_scrollingがfalseのケースのドラッグ距離を見る）
        int press_start_x = 0;
        int press_start_y = 0;
        bool moved_beyond_threshold = false;
        static constexpr int kTapThreshold = 6; // px

        String formatBlockText(const MdBlock& b) const;
        int findBlockAtScreenY(int screenY) const;       // タップ位置→ブロック特定

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
