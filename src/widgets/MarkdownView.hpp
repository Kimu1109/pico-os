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
    Link,       // 追加：行全体がリンクのみのブロック
    CodeBlock,  // 追加：```で囲まれたコードブロック
};

struct MdBlock {
    MdBlockType type;
    uint16_t srcOffset;
    uint16_t srcLength;
    uint16_t urlOffset = 0;   // Link用：doc_text内のURL部分オフセット
    uint16_t urlLength = 0;   // Link用：URL部分の長さ
    int32_t  y;
    uint16_t height;
};

class MarkdownView : public Widget {
    private:
        static constexpr int kLabelPoolSize = 8;
        static constexpr int kImagePoolSize = 2;
        static constexpr int kMaxBlocks     = 128;
        static constexpr int kMaxSourceBytes = 16384;
        static constexpr int kPadding       = 4;
        static constexpr int kBlockSpacing  = 6;
        static constexpr int SCROLL_L       = 15; // ScrollContainerと同じ見た目に揃える

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
        FontFn::FontSize fontSizeForBlock(MdBlockType type) const; // ※要調整: 実際のenum値に合わせて

        std::function<void(String)> on_link_tap = nullptr;

        // タップ判定用（is_scrollingがfalseのケースのドラッグ距離を見る）
        int press_start_x = 0;
        int press_start_y = 0;
        bool moved_beyond_threshold = false;
        static constexpr int kTapThreshold = 6; // px

        String formatBlockText(const MdBlock& b) const;
        String escapeCodeText(const String& raw) const; // 追加：マークアップ文字をエスケープ
        int findBlockAtScreenY(int screenY) const;       // 追加：タップ位置→ブロック特定

    public:
        using Widget::onPressStart;
        using Widget::onPressMove;

        MarkdownView(int16_t x, int16_t y, int16_t w, int16_t h);

        bool Load(const String& path);

        void render() override;

        void onPressStart() override;
        void onPressMove() override;
        void onPressEnd() override;

        void OnLinkTap(std::function<void(String)> callback) {
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

        WidgetTools::RenderMode GetRenderMode() const override { return WidgetTools::OPAQUE; }

        int getScrollOffsetY() const override { return scroll_y; }
};
