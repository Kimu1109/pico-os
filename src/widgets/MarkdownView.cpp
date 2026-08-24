#include "widgets/MarkdownView.hpp"
#include "OS_Data.hpp"
#include "functions/GFX_Functions.hpp"
#include "icons/icon_render.h"
#include "SdFat.h"

MarkdownView::MarkdownView(int16_t x, int16_t y, int16_t w, int16_t h) {
    this->l_rect = {x, y, w, h};

    for (int i = 0; i < kLabelPoolSize; i++) {
        labelPool[i] = new Label(kPadding, 0, "");
        labelPool[i]->SetParent(this);
        labelPool[i]->Visible(false);
        boundLabelBlock[i] = -1;
        children_.push_back(labelPool[i]);
    }
    for (int i = 0; i < kImagePoolSize; i++) {
        imagePool[i] = new Image("", kPadding, 0, false);
        imagePool[i]->SetParent(this);
        imagePool[i]->Visible(false);
        boundImageBlock[i] = -1;
        children_.push_back(imagePool[i]);
    }

    measure_label = new Label(0, 0, ""); // レンダリングツリーには含めない（getChildren()に入れない）
}

// ---------- ロード & パース ----------

bool MarkdownView::Load(const String& path) {
    FsFile f = OSData::SD.open(path);
    if (!f) return false;

    size_t size = f.fileSize();
    if (size > kMaxSourceBytes) size = kMaxSourceBytes;

    char* buf = new char[size + 1];
    f.read((uint8_t*)buf, size);
    buf[size] = '\0';
    f.close();

    doc_text = String(buf);
    delete[] buf;

    parseBlocks();
    layoutBlocks();

    scroll_y = 0;
    for (int i = 0; i < kLabelPoolSize; i++) boundLabelBlock[i] = -1;
    for (int i = 0; i < kImagePoolSize; i++) boundImageBlock[i] = -1;
    bindVisibleBlocks(true);

    this->needsRender();
    return true;
}

void MarkdownView::parseBlocks() {
    blocks.clear();
    blocks.reserve(kMaxBlocks);

    const int len = doc_text.length();
    int pos = 0;
    int paraStart = -1;

    auto flushParagraph = [&](int endPos) {
        if (paraStart >= 0 && endPos > paraStart && (int)blocks.size() < kMaxBlocks) {
            MdBlock b{};
            b.type = MdBlockType::Paragraph;
            b.srcOffset = paraStart;
            b.srcLength = endPos - paraStart;
            blocks.push_back(b);
        }
        paraStart = -1;
    };

    // 行全体が [text](url) だけかどうかを判定するヘルパー
    auto tryParseWholeLineLink = [&](int lineStart, int lineEnd, MdBlock& outBlock) -> bool {
        if (lineEnd - lineStart < 4) return false;
        if (doc_text[lineStart] != '[') return false;
        int closeBracket = doc_text.indexOf(']', lineStart);
        if (closeBracket == -1 || closeBracket >= lineEnd) return false;
        if (closeBracket + 1 >= lineEnd || doc_text[closeBracket + 1] != '(') return false;
        int closeParen = doc_text.indexOf(')', closeBracket);
        if (closeParen == -1 || closeParen >= lineEnd) return false;
        // 末尾までがリンク構文のみ（前後の余分なテキストが無い）
        if (closeParen != lineEnd - 1) return false;

        outBlock.type = MdBlockType::Link;
        outBlock.srcOffset = lineStart + 1;
        outBlock.srcLength = closeBracket - (lineStart + 1);
        outBlock.urlOffset = closeBracket + 2;
        outBlock.urlLength = closeParen - (closeBracket + 2);
        return true;
    };

    while (pos < len && (int)blocks.size() < kMaxBlocks) {
        int nl = doc_text.indexOf('\n', pos);
        int lineEnd = (nl == -1) ? len : nl;
        int lineLen = lineEnd - pos;

        bool isBlank = true;
        for (int i = pos; i < lineEnd; i++) {
            char c = doc_text[i];
            if (c != ' ' && c != '\t' && c != '\r') { isBlank = false; break; }
        }

        // フェンス付きコードブロック開始
        if (!isBlank && lineLen >= 3 && doc_text[pos] == '`' && doc_text[pos+1] == '`' && doc_text[pos+2] == '`') {
            flushParagraph(pos > 0 ? pos - 1 : pos);

            int contentStart = (nl == -1) ? len : nl + 1;
            int searchPos = contentStart;
            int fenceEndLineStart = -1, fenceEndLineEnd = -1;

            while (searchPos <= len) {
                int innerNl = doc_text.indexOf('\n', searchPos);
                int innerLineEnd = (innerNl == -1) ? len : innerNl;
                if (innerLineEnd - searchPos >= 3 &&
                    doc_text[searchPos] == '`' && doc_text[searchPos+1] == '`' && doc_text[searchPos+2] == '`') {
                    fenceEndLineStart = searchPos;
                    fenceEndLineEnd = innerLineEnd;
                    break;
                }
                if (innerNl == -1) break;
                searchPos = innerNl + 1;
            }

            int contentEnd = (fenceEndLineStart != -1) ? (fenceEndLineStart > contentStart ? fenceEndLineStart - 1 : contentStart) : len;

            if ((int)blocks.size() < kMaxBlocks) {
                MdBlock b{};
                b.type = MdBlockType::CodeBlock;
                b.srcOffset = contentStart;
                b.srcLength = contentEnd - contentStart;
                blocks.push_back(b);
            }

            pos = (fenceEndLineEnd == -1) ? len : ((fenceEndLineEnd == len) ? len : fenceEndLineEnd + 1);
            continue;
        }

        if (isBlank) {
            flushParagraph(pos > 0 ? pos - 1 : pos);
        } else if (lineLen >= 2 && doc_text[pos] == '#') {
            flushParagraph(pos > 0 ? pos - 1 : pos);
            int level = 0, i = pos;
            while (i < lineEnd && doc_text[i] == '#' && level < 3) { level++; i++; }
            while (i < lineEnd && doc_text[i] == ' ') i++;

            MdBlock b{};
            b.type = (level == 1) ? MdBlockType::H1 : (level == 2 ? MdBlockType::H2 : MdBlockType::H3);
            b.srcOffset = i;
            b.srcLength = lineEnd - i;
            blocks.push_back(b);
        } else if (lineLen >= 2 && doc_text[pos] == '!' && doc_text[pos + 1] == '[') {
            flushParagraph(pos > 0 ? pos - 1 : pos);
            int pOpen = doc_text.indexOf('(', pos);
            int pClose = doc_text.indexOf(')', pos);
            if (pOpen != -1 && pClose != -1 && pOpen < lineEnd && pClose <= lineEnd) {
                MdBlock b{};
                b.type = MdBlockType::Image;
                b.srcOffset = pOpen + 1;
                b.srcLength = pClose - (pOpen + 1);
                blocks.push_back(b);
            }
        } else {
            MdBlock linkBlock{};
            if (tryParseWholeLineLink(pos, lineEnd, linkBlock)) {
                flushParagraph(pos > 0 ? pos - 1 : pos);
                if ((int)blocks.size() < kMaxBlocks) blocks.push_back(linkBlock);
            } else if (paraStart < 0) {
                paraStart = pos;
            }
        }

        pos = (nl == -1) ? len : nl + 1;
    }
    flushParagraph(len);
}

// ---------- レイアウト（高さ事前計算） ----------

String MarkdownView::escapeCodeText(const String& raw) const {
    String out;
    out.reserve(raw.length() + 16);
    for (size_t i = 0; i < raw.length(); i++) {
        char c = raw[i];
        out += c;
        if (c == '*' || c == '_' || c == '~') {
            out += "\xE2\x80\x8B"; // U+200B ゼロ幅スペース（UTF-8）
        }
    }
    return out;
}

String MarkdownView::formatBlockText(const MdBlock& b) const {
    switch (b.type) {
        case MdBlockType::H1:
        case MdBlockType::H2:
        case MdBlockType::H3: {
            String raw = doc_text.substring(b.srcOffset, b.srcOffset + b.srcLength);
            return "**" + raw + "**";
        }
        case MdBlockType::Link: {
            String text = doc_text.substring(b.srcOffset, b.srcOffset + b.srcLength);
            return "_" + text + "_"; // 下線で視覚的に示す
        }
        case MdBlockType::CodeBlock: {
            String raw = doc_text.substring(b.srcOffset, b.srcOffset + b.srcLength);
            return escapeCodeText(raw);
        }
        default:
            return doc_text.substring(b.srcOffset, b.srcOffset + b.srcLength);
    }
}

void MarkdownView::layoutBlocks() {
    const int viewport_w = this->l_rect.w - SCROLL_L - kPadding * 2;
    int32_t y = kPadding;

    for (auto& b : blocks) {
        if (b.type == MdBlockType::Image) {
            String path = doc_text.substring(b.srcOffset, b.srcOffset + b.srcLength);
            uint16_t h = kPadding;
            FsFile f = OSData::SD.open(path);
            if (f) {
                IconRender::PimgHeader head;
                if (IconRender::ReadPimgHeader(f, head)) h = head.height;
                f.close();
            }
            b.height = h;
        }
        else if (b.type == MdBlockType::CodeBlock) {
            measure_label->MaxWidth(viewport_w - kPadding * 2); // 内側に余白を持たせる
            measure_label->SetFontSize(FontFn::FontSize::Small);
            measure_label->Text(formatBlockText(b));
            b.height = measure_label->H() + kPadding * 2; // 背景ボックス分の余白
        }
        else if (b.type == MdBlockType::Link) {
            measure_label->MaxWidth(viewport_w);
            measure_label->SetFontSize(FontFn::FontSize::Small);
            measure_label->Text(formatBlockText(b));
            b.height = measure_label->H();
        }
        else {
            measure_label->MaxWidth(viewport_w);
            measure_label->SetFontSize(fontSizeForBlock(b.type));
            measure_label->Text(formatBlockText(b));
            b.height = measure_label->H();
        }

        b.y = y;
        y += b.height + kBlockSpacing;
    }

    total_height = y;
    max_scroll_y = std::max(0, (int)total_height - (int)this->l_rect.h);
}

FontFn::FontSize MarkdownView::fontSizeForBlock(MdBlockType type) const {
    // ※ Font_Functions.hpp の実際のenum値に合わせて要調整（プレースホルダー）
    switch (type) {
        case MdBlockType::H1: return FontFn::FontSize::Bigger;
        case MdBlockType::H2: return FontFn::FontSize::Big;
        case MdBlockType::H3: return FontFn::FontSize::Normal;
        default:              return FontFn::FontSize::Small;
    }
}

// ---------- ウィジェットプールの再バインド（仮想化の核） ----------

void MarkdownView::bindVisibleBlocks(bool force) {
    const int viewport_top = scroll_y;
    const int viewport_bottom = scroll_y + this->l_rect.h;

    int lo = 0, hi = (int)blocks.size();
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (blocks[mid].y + (int)blocks[mid].height < viewport_top) lo = mid + 1;
        else hi = mid;
    }

    int labelSlot = 0, imageSlot = 0;

    for (int i = lo; i < (int)blocks.size() && blocks[i].y < viewport_bottom; i++) {
        if (blocks[i].type == MdBlockType::Image) {
            if (imageSlot >= kImagePoolSize) continue; // プール枯渇。TODO: 画像密度が高い文書向けにプール拡張を検討
            bindImageSlot(imageSlot++, i, force);
        } else {
            if (labelSlot >= kLabelPoolSize) continue;
            bindLabelSlot(labelSlot++, i, force);
        }
    }

    for (; labelSlot < kLabelPoolSize; labelSlot++) hideLabelSlot(labelSlot);
    for (; imageSlot < kImagePoolSize; imageSlot++) hideImageSlot(imageSlot);
}

void MarkdownView::bindLabelSlot(int slot, int blockIdx, bool force) {
    if (!force && boundLabelBlock[slot] == blockIdx) {
        labelPool[slot]->Visible(true);
        return;
    }
    const MdBlock& b = blocks[blockIdx];
    Label* lbl = labelPool[slot];

    lbl->NoBackground();
    lbl->Border(PICO_BLACK, 0);
    lbl->SetTextColor(PICO_BLACK);

    if (b.type == MdBlockType::CodeBlock) {
        lbl->MaxWidth(this->l_rect.w - SCROLL_L - kPadding * 4);
        lbl->BackgroundColor(PICO_LIGHTGREY);
        lbl->Border(PICO_DARKGREY, 1);
        lbl->SetFontSize(FontFn::FontSize::Small);
        lbl->Text(formatBlockText(b));
        lbl->X(kPadding);
    } else if (b.type == MdBlockType::Link) {
        lbl->MaxWidth(this->l_rect.w - SCROLL_L - kPadding * 2);
        lbl->SetTextColor(PICO_BLUE);
        lbl->SetFontSize(FontFn::FontSize::Small);
        lbl->Text(formatBlockText(b));
        lbl->X(kPadding);
    } else {
        lbl->MaxWidth(this->l_rect.w - SCROLL_L - kPadding * 2);
        lbl->SetFontSize(fontSizeForBlock(b.type));
        lbl->Text(formatBlockText(b));
        lbl->X(kPadding);
    }

    lbl->Y(b.y);
    lbl->Visible(true);
    boundLabelBlock[slot] = blockIdx;
}

void MarkdownView::bindImageSlot(int slot, int blockIdx, bool force) {
    if (!force && boundImageBlock[slot] == blockIdx) {
        imagePool[slot]->Visible(true);
        return;
    }
    const MdBlock& b = blocks[blockIdx];
    Image* img = imagePool[slot];

    img->Path(doc_text.substring(b.srcOffset, b.srcOffset + b.srcLength));
    img->X(kPadding);
    img->Y(b.y);
    img->Visible(true);

    boundImageBlock[slot] = blockIdx;
}

void MarkdownView::hideLabelSlot(int slot) {
    if (boundLabelBlock[slot] == -1) return;
    labelPool[slot]->Visible(false);
    boundLabelBlock[slot] = -1;
}

void MarkdownView::hideImageSlot(int slot) {
    if (boundImageBlock[slot] == -1) return;
    imagePool[slot]->Visible(false);
    boundImageBlock[slot] = -1;
}

// ---------- タッチ（スクロールバー領域のみでドラッグ、ScrollContainerと同じ流儀） ----------

void MarkdownView::onPressStart() {
    Widget::onPressStart();

    press_start_x = OSData::touchX - getScreenX();
    press_start_y = OSData::touchY - getScreenY();
    moved_beyond_threshold = false;

    is_scrolling = (press_start_x >= this->l_rect.w - SCROLL_L);
    if (is_scrolling) {
        sy = press_start_y;
        s_scroll_y = scroll_y;
    }
}

void MarkdownView::onPressMove() {
    Widget::onPressMove();

    int rx = OSData::touchX - getScreenX();
    int ry = OSData::touchY - getScreenY();
    if (abs(rx - press_start_x) > kTapThreshold || abs(ry - press_start_y) > kTapThreshold) {
        moved_beyond_threshold = true;
    }

    if (!is_scrolling) return;

    int new_y = s_scroll_y + (int)((ry - sy) * PICO_SCROLL_EX);
    int old_scroll_y = scroll_y;
    scroll_y = constrain(new_y, 0, max_scroll_y);

    unsigned long now = millis();
    if (now - last_scroll_render_ms < 50) return;
    last_scroll_render_ms = now;

    bindVisibleBlocks(false);
    this->needsRender();
    for (int i = 0; i < kLabelPoolSize; i++) labelPool[i]->needsRender();
    for (int i = 0; i < kImagePoolSize; i++) imagePool[i]->needsRender();
}
int MarkdownView::findBlockAtScreenY(int screenY) const {
    int docY = screenY + scroll_y;
    int lo = 0, hi = (int)blocks.size();
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (blocks[mid].y + (int)blocks[mid].height < docY) lo = mid + 1;
        else hi = mid;
    }
    if (lo < (int)blocks.size() && docY >= blocks[lo].y && docY < blocks[lo].y + (int)blocks[lo].height) {
        return lo;
    }
    return -1;
}

void MarkdownView::onPressEnd() {
    Widget::onPressEnd();

    if (!is_scrolling && !moved_beyond_threshold) {
        int ry = OSData::touchY - getScreenY();
        int idx = findBlockAtScreenY(ry);
        if (idx >= 0 && blocks[idx].type == MdBlockType::Link && on_link_tap) {
            String url = doc_text.substring(blocks[idx].urlOffset, blocks[idx].urlOffset + blocks[idx].urlLength);
            on_link_tap(url);
        }
    }

    is_scrolling = false;
}

// ---------- 描画（枠・スクロールバーのみ。背景/子はコンポジタが処理） ----------

void MarkdownView::render() {
    if (prev_l_rect != l_rect) {
        PICO_GFX::markDirty(this->getScreenPrevRect());
    }

    const Rect g_rect = this->getScreenRect();

    OSData::frame->drawRect(g_rect.x, g_rect.y, g_rect.w, g_rect.h, PICO_BLACK);

    const int bar_x = g_rect.x + g_rect.w - SCROLL_L;
    const int bar_y = g_rect.y;
    const int bar_w = SCROLL_L;
    const int bar_h = g_rect.h;

    OSData::frame->drawRect(bar_x, bar_y, bar_w, bar_h, PICO_BLACK);

    if (max_scroll_y > 0) {
        const int total_h = max_scroll_y + bar_h;
        int thumb_h = std::max(10, (bar_h * bar_h) / total_h);
        if (thumb_h > bar_h) thumb_h = bar_h;
        const int thumb_y = bar_y + (scroll_y * (bar_h - thumb_h)) / max_scroll_y;

        OSData::frame->fillRect(bar_x + 2, thumb_y + 2, bar_w - 4, std::max(1, thumb_h - 4), PICO_BLACK);
    }

    PICO_GFX::markDirty(g_rect);
    prev_l_rect.copy(l_rect);
}
