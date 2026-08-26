#include "widgets/MarkdownView.hpp"
#include "OS_Data.hpp"
#include "functions/GFX_Functions.hpp"
#include "icons/icon_render.h"
#include "SdFat.h"

MarkdownView::MarkdownView(int16_t x, int16_t y, int16_t w, int16_t h) {
    this->l_rect = {x, y, w, h};

    for (int i = 0; i < kLabelPoolSize; i++) {
        labelPool[i] = new Label(kPadding, 0, "");
        labelPool[i]->setParent(this);
        labelPool[i]->setVisible(false);
        labelPool[i]->setDisableMarkdirty(true);
        boundLabelBlock[i] = -1;
        children_.push_back(labelPool[i]);
    }
    for (int i = 0; i < kImagePoolSize; i++) {
        imagePool[i] = new Image("", kPadding, 0, true);
        imagePool[i]->setParent(this);
        imagePool[i]->setVisible(false);
        imagePool[i]->setDisableMarkdirty(true);
        boundImageBlock[i] = -1;
        children_.push_back(imagePool[i]);
    }

    measure_label = new Label(0, 0, ""); // レンダリングツリーには含めない（getChildren()に入れない）
}

// ---------- ロード & パース ----------

bool MarkdownView::load(const String& path) {
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

// ---------- インライン要素（コード/リンク）認識 ----------

// src中の `code` を波線(~code~)、[text](url) を下線(_text_) へ変換する。
// LabelのparseMarkup()は**/_/~ をトグル式のインライン装飾として解釈できるため、
// ここで変換すれば追加のLabel改修なしに「同じ行の中に」コード風/リンク風の
// 装飾を混在させて描画できる。
// 注意点:
//   - コードスパン・リンクとも改行をまたぐ場合は解釈せず、記号をそのまま出力する
//   - コード内容に *, _, ~ が含まれる場合、Label側のトグルに影響してしまう
//     （Label側がバックスラッシュエスケープに対応していないための既知の制約）
//   - 1ブロックに複数リンクがある場合、装飾自体は全リンクに適用されるが、
//     タップで開けるのは findFirstInlineLink() が拾う最初の1件のみ
String MarkdownView::applyInlineMarkdown(const String& src) const {
    String out;
    const int n = src.length();

    for (int i = 0; i < n; i++) {
        char c = src[i];

        // インラインコード `code`
        if (c == '`') {
            int close = src.indexOf('`', i + 1);
            int nl = src.indexOf('\n', i + 1);
            if (close != -1 && (nl == -1 || close < nl)) {
                out += "~";
                out += src.substring(i + 1, close);
                out += "~";
                i = close;
                continue;
            }
            out += c;
            continue;
        }

        // インラインリンク [text](url)
        if (c == '[') {
            int closeBracket = src.indexOf(']', i + 1);
            int nl1 = src.indexOf('\n', i + 1);
            if (closeBracket != -1 && (nl1 == -1 || closeBracket < nl1) &&
                closeBracket + 1 < n && src[closeBracket + 1] == '(') {
                int closeParen = src.indexOf(')', closeBracket + 2);
                int nl2 = src.indexOf('\n', closeBracket + 2);
                if (closeParen != -1 && (nl2 == -1 || closeParen < nl2)) {
                    out += "_";
                    out += src.substring(i + 1, closeBracket);
                    out += "_";
                    i = closeParen;
                    continue;
                }
            }
            out += c;
            continue;
        }

        out += c;
    }
    return out;
}

// doc_text の [start, end) 範囲内で最初に見つかる [text](url) のURL部分を探す。
// 改行をまたぐ組は無視する。
bool MarkdownView::findFirstInlineLink(int start, int end, uint16_t& urlOffOut, uint16_t& urlLenOut) const {
    for (int i = start; i < end; i++) {
        if (doc_text[i] != '[') continue;

        int closeBracket = doc_text.indexOf(']', i + 1);
        if (closeBracket == -1 || closeBracket >= end) continue;

        int nl1 = doc_text.indexOf('\n', i + 1);
        if (nl1 != -1 && nl1 < closeBracket) continue;

        if (closeBracket + 1 >= end || doc_text[closeBracket + 1] != '(') continue;

        int closeParen = doc_text.indexOf(')', closeBracket + 2);
        if (closeParen == -1 || closeParen >= end) continue;

        int nl2 = doc_text.indexOf('\n', closeBracket + 2);
        if (nl2 != -1 && nl2 < closeParen) continue;

        urlOffOut = closeBracket + 2;
        urlLenOut = closeParen - (closeBracket + 2);
        return true;
    }
    return false;
}

// ---------- リスト行の判定 ----------
// 先頭スペース2個につき1段のネストとして扱う。
// 「- 」「* 」「+ 」= 箇条書き、「数字. 」= 番号付き。
bool MarkdownView::tryParseListItem(int lineStart, int lineEnd, int& indentOut, bool& orderedOut,
                                     int& numberOut, int& contentStartOut) const {
    int i = lineStart;
    int spaces = 0;
    while (i < lineEnd && doc_text[i] == ' ') { spaces++; i++; }
    if (i >= lineEnd) return false;

    indentOut = spaces / 2;
    if (indentOut >= kMaxListLevels) indentOut = kMaxListLevels - 1;

    char c = doc_text[i];

    // 箇条書き: -, *, + の直後に半角スペース
    // (「**太字**」等は連続した記号なのでこの条件には合致しない)
    if ((c == '-' || c == '*' || c == '+') && i + 1 < lineEnd && doc_text[i + 1] == ' ') {
        orderedOut = false;
        numberOut = 0;
        contentStartOut = i + 1;
        while (contentStartOut < lineEnd && doc_text[contentStartOut] == ' ') contentStartOut++;
        return true;
    }

    // 番号付き: 数字が1つ以上続いた後に '.' + 半角スペース
    int j = i;
    int num = 0;
    bool hasDigit = false;
    while (j < lineEnd && doc_text[j] >= '0' && doc_text[j] <= '9') {
        num = num * 10 + (doc_text[j] - '0');
        j++;
        hasDigit = true;
    }
    if (hasDigit && j < lineEnd && doc_text[j] == '.' && j + 1 < lineEnd && doc_text[j + 1] == ' ') {
        orderedOut = true;
        numberOut = num;
        contentStartOut = j + 1;
        while (contentStartOut < lineEnd && doc_text[contentStartOut] == ' ') contentStartOut++;
        return true;
    }

    return false;
}

void MarkdownView::parseBlocks() {
    blocks.clear();
    blocks.reserve(kMaxBlocks);

    const int len = doc_text.length();
    int pos = 0;
    int paraStart = -1;

    // 番号付きリストのネスト段ごとの現在の番号。
    // 段落・見出し等の非リスト行が来たら全段リセットする。
    int orderedCounter[kMaxListLevels];
    for (int i = 0; i < kMaxListLevels; i++) orderedCounter[i] = 0;

    auto resetListCounters = [&]() {
        for (int i = 0; i < kMaxListLevels; i++) orderedCounter[i] = 0;
    };

    auto flushParagraph = [&](int endPos) {
        if (paraStart >= 0 && endPos > paraStart && (int)blocks.size() < kMaxBlocks) {
            MdBlock b{};
            b.type = MdBlockType::Paragraph;
            b.srcOffset = paraStart;
            b.srcLength = endPos - paraStart;

            uint16_t linkOff, linkLen;
            if (findFirstInlineLink(b.srcOffset, b.srcOffset + b.srcLength, linkOff, linkLen)) {
                b.urlOffset = linkOff;
                b.urlLength = linkLen;
            }

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
            resetListCounters();

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
            // 空行だけではリストの番号は途切れさせない（tight/looseリスト双方に対応）
        } else if (lineLen >= 2 && doc_text[pos] == '#') {
            flushParagraph(pos > 0 ? pos - 1 : pos);
            resetListCounters();
            int level = 0, i = pos;
            while (i < lineEnd && doc_text[i] == '#' && level < 3) { level++; i++; }
            while (i < lineEnd && doc_text[i] == ' ') i++;

            MdBlock b{};
            b.type = (level == 1) ? MdBlockType::H1 : (level == 2 ? MdBlockType::H2 : MdBlockType::H3);
            b.srcOffset = i;
            b.srcLength = lineEnd - i;

            uint16_t linkOff, linkLen;
            if (findFirstInlineLink(b.srcOffset, b.srcOffset + b.srcLength, linkOff, linkLen)) {
                b.urlOffset = linkOff;
                b.urlLength = linkLen;
            }

            blocks.push_back(b);
        } else if (lineLen >= 2 && doc_text[pos] == '!' && doc_text[pos + 1] == '[') {
            flushParagraph(pos > 0 ? pos - 1 : pos);
            resetListCounters();
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
            int indent, number, contentStart;
            bool ordered;
            if (tryParseListItem(pos, lineEnd, indent, ordered, number, contentStart)) {
                flushParagraph(pos > 0 ? pos - 1 : pos);

                if ((int)blocks.size() < kMaxBlocks) {
                    // このリスト項目より深いネストのカウンタは新しいサブリストの
                    // 開始として扱うためリセットする
                    for (int lvl = indent + 1; lvl < kMaxListLevels; lvl++) orderedCounter[lvl] = 0;

                    MdBlock b{};
                    b.type = MdBlockType::ListItem;
                    b.srcOffset = contentStart;
                    b.srcLength = lineEnd - contentStart;
                    b.listIndent = (uint8_t)indent;
                    b.listOrdered = ordered;

                    if (ordered) {
                        orderedCounter[indent]++;
                        b.listNumber = orderedCounter[indent];
                    } else {
                        orderedCounter[indent] = 0;
                        b.listNumber = 0;
                    }

                    uint16_t linkOff, linkLen;
                    if (findFirstInlineLink(b.srcOffset, b.srcOffset + b.srcLength, linkOff, linkLen)) {
                        b.urlOffset = linkOff;
                        b.urlLength = linkLen;
                    }

                    blocks.push_back(b);
                }
            } else {
                MdBlock linkBlock{};
                if (tryParseWholeLineLink(pos, lineEnd, linkBlock)) {
                    flushParagraph(pos > 0 ? pos - 1 : pos);
                    resetListCounters();
                    if ((int)blocks.size() < kMaxBlocks) blocks.push_back(linkBlock);
                } else if (paraStart < 0) {
                    paraStart = pos;
                    resetListCounters(); // 通常の段落が始まったのでリストは終了
                }
            }
        }

        pos = (nl == -1) ? len : nl + 1;
    }
    flushParagraph(len);
}

// ---------- レイアウト（高さ事前計算） ----------

String MarkdownView::formatBlockText(const MdBlock& b) const {
    switch (b.type) {
        case MdBlockType::H1:
        case MdBlockType::H2:
        case MdBlockType::H3: {
            String raw = doc_text.substring(b.srcOffset, b.srcOffset + b.srcLength);
            return "**" + applyInlineMarkdown(raw) + "**";
        }
        case MdBlockType::Link: {
            String text = doc_text.substring(b.srcOffset, b.srcOffset + b.srcLength);
            return "_" + text + "_"; // 下線で視覚的に示す
        }
        case MdBlockType::ListItem: {
            String content = doc_text.substring(b.srcOffset, b.srcOffset + b.srcLength);
            String marker;
            if (b.listOrdered) {
                marker = String(b.listNumber) + ".";
            } else {
                // 記号自体はネスト段によらず統一（フォントの文字種カバレッジに配慮し、
                // 絵文字的な行頭記号は使わずASCIIのみを使用）。段の深さはインデント幅で表現する。
                marker = "-";
            }
            return marker + " " + applyInlineMarkdown(content);
        }
        case MdBlockType::CodeBlock:
            return doc_text.substring(b.srcOffset, b.srcOffset + b.srcLength);
        default: // Paragraph
            return applyInlineMarkdown(doc_text.substring(b.srcOffset, b.srcOffset + b.srcLength));
    }
}

void MarkdownView::layoutBlocks() {
    const int viewport_w = this->l_rect.w - SCROLL_L - kPadding * 2;
    int32_t y = kPadding;

    for (size_t idx = 0; idx < blocks.size(); idx++) {
        MdBlock& b = blocks[idx];

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
            measure_label->setMaxWidth(viewport_w - kPadding * 2); // 内側に余白を持たせる
            measure_label->setFontSize(FontFn::FontSize::Small);
            measure_label->setText(formatBlockText(b));
            b.height = measure_label->getH() + kPadding * 2; // 背景ボックス分の余白
        }
        else if (b.type == MdBlockType::Link) {
            measure_label->setMaxWidth(viewport_w);
            measure_label->setFontSize(FontFn::FontSize::Small);
            measure_label->setText(formatBlockText(b));
            b.height = measure_label->getH();
        }
        else if (b.type == MdBlockType::ListItem) {
            int indentPx = b.listIndent * kListIndentWidth;
            int w = viewport_w - indentPx;
            if (w < 20) w = 20; // 極端なネストでも最低限の幅を確保
            measure_label->setMaxWidth(w);
            measure_label->setFontSize(fontSizeForBlock(b.type));
            measure_label->setText(formatBlockText(b));
            b.height = measure_label->getH();
        }
        else {
            measure_label->setMaxWidth(viewport_w);
            measure_label->setFontSize(fontSizeForBlock(b.type));
            measure_label->setText(formatBlockText(b));
            b.height = measure_label->getH();
        }

        b.y = y;

        // 連続するリスト項目同士は間隔を詰めて、リストのまとまりを見やすくする
        int spacing = kBlockSpacing;
        if (b.type == MdBlockType::ListItem &&
            idx + 1 < blocks.size() && blocks[idx + 1].type == MdBlockType::ListItem) {
            spacing = kBlockSpacing / 2;
        }

        y += b.height + spacing;
    }

    total_height = y;
    max_scroll_y = std::max(0, (int)total_height - (int)this->l_rect.h);
}

FontFn::FontSize MarkdownView::fontSizeForBlock(MdBlockType type) const {
    // ※ Font_Functions.hpp の実際のenum値に合わせて要調整（プレースホルダー）
    switch (type) {
        case MdBlockType::H1:       return FontFn::FontSize::Bigger;
        case MdBlockType::H2:       return FontFn::FontSize::Big;
        case MdBlockType::H3:       return FontFn::FontSize::Normal;
        case MdBlockType::ListItem: return FontFn::FontSize::Small;
        default:                    return FontFn::FontSize::Small;
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
        labelPool[slot]->setVisible(true);
        return;
    }
    const MdBlock& b = blocks[blockIdx];
    Label* lbl = labelPool[slot];

    lbl->setNoBackground();
    lbl->setBorder(PICO_BLACK, 0);
    lbl->setTextColor(PICO_BLACK);

    if (b.type == MdBlockType::CodeBlock) {
        lbl->setDisableAutoTextDecoration(true);
        lbl->setMaxWidth(this->l_rect.w - SCROLL_L - kPadding * 4);
        lbl->setBackgroundColor(PICO_LIGHTGREY);
        lbl->setBorder(PICO_DARKGREY, 1);
        lbl->setFontSize(FontFn::FontSize::Small);
        lbl->setText(formatBlockText(b));
        lbl->setX(kPadding);
    } else if (b.type == MdBlockType::Link) {
        if(lbl->getDisableAutoTextDecoration())
            lbl->setDisableAutoTextDecoration(false);
        lbl->setMaxWidth(this->l_rect.w - SCROLL_L - kPadding * 2);
        lbl->setTextColor(PICO_BLUE);
        lbl->setFontSize(FontFn::FontSize::Small);
        lbl->setText(formatBlockText(b));
        lbl->setX(kPadding);
    } else if (b.type == MdBlockType::ListItem) {
        if(lbl->getDisableAutoTextDecoration())
            lbl->setDisableAutoTextDecoration(false);
        int indentPx = b.listIndent * kListIndentWidth;
        int w = this->l_rect.w - SCROLL_L - kPadding * 2 - indentPx;
        if (w < 20) w = 20;
        lbl->setMaxWidth(w);
        lbl->setFontSize(fontSizeForBlock(b.type));
        lbl->setText(formatBlockText(b));
        lbl->setX(kPadding + indentPx);
    } else {
        if(lbl->getDisableAutoTextDecoration())
            lbl->setDisableAutoTextDecoration(false);
        lbl->setMaxWidth(this->l_rect.w - SCROLL_L - kPadding * 2);
        lbl->setFontSize(fontSizeForBlock(b.type));
        lbl->setText(formatBlockText(b));
        lbl->setX(kPadding);
    }

    lbl->setY(b.y);
    lbl->setVisible(true);
    boundLabelBlock[slot] = blockIdx;
}

void MarkdownView::bindImageSlot(int slot, int blockIdx, bool force) {
    if (!force && boundImageBlock[slot] == blockIdx) {
        imagePool[slot]->setVisible(true);
        return;
    }
    const MdBlock& b = blocks[blockIdx];
    Image* img = imagePool[slot];

    img->setPath(doc_text.substring(b.srcOffset, b.srcOffset + b.srcLength));
    img->setX(kPadding);
    img->setY(b.y);
    img->setVisible(true);

    boundImageBlock[slot] = blockIdx;
}

void MarkdownView::hideLabelSlot(int slot) {
    if (boundLabelBlock[slot] == -1) return;
    labelPool[slot]->setVisible(false);
    boundLabelBlock[slot] = -1;
}

void MarkdownView::hideImageSlot(int slot) {
    if (boundImageBlock[slot] == -1) return;
    imagePool[slot]->setVisible(false);
    boundImageBlock[slot] = -1;
}

// ---------- タッチ（スクロールバー領域のみでドラッグ、ScrollContainerと同じ流儀） ----------

void MarkdownView::causeOnPressStart() {
    Widget::causeOnPressStart();

    press_start_x = OSData::touchX - getScreenX();
    press_start_y = OSData::touchY - getScreenY();
    moved_beyond_threshold = false;

    is_scrolling = (press_start_x >= this->l_rect.w - SCROLL_L);
    if (is_scrolling) {
        sy = press_start_y;
        s_scroll_y = scroll_y;
    }
}

void MarkdownView::causeOnPressMove() {
    Widget::causeOnPressMove();

    int rx = OSData::touchX - getScreenX();
    int ry = OSData::touchY - getScreenY();
    if (abs(rx - press_start_x) > kTapThreshold || abs(ry - press_start_y) > kTapThreshold) {
        moved_beyond_threshold = true;
    }

    if (!is_scrolling) return;

    int new_y = s_scroll_y + ((float)(ry - sy) / (float)this->l_rect.h) * max_scroll_y;
    int old_scroll_y = scroll_y;
    scroll_y = constrain(new_y, 0, max_scroll_y);

    unsigned long now = millis();
    if (now - last_scroll_render_ms < 33) return;
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

void MarkdownView::causeOnPressEnd() {
    Widget::causeOnPressEnd();

    if (!is_scrolling && !moved_beyond_threshold) {
        int ry = OSData::touchY - getScreenY();
        int idx = findBlockAtScreenY(ry);
        // Link型の行だけでなく、段落/見出し/リスト項目にインラインリンクが
        // 見つかっている場合(urlLength > 0)もタップで開けるようにする。
        // 1ブロックに複数リンクがある場合は最初の1件のみが対象になる点に注意。
        if (idx >= 0 && blocks[idx].urlLength > 0 && on_link_tap) {
            String url = doc_text.substring(blocks[idx].urlOffset, blocks[idx].urlOffset + blocks[idx].urlLength);
            on_link_tap(url);
        }
    }

    is_scrolling = false;
}

// ---------- 描画（枠・スクロールバーのみ。背景/子はコンポジタが処理） ----------

void MarkdownView::render() {
    if (prev_l_rect != l_rect) {
        markdirty(this->getScreenPrevRect());
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

    markdirty(g_rect);
    prev_l_rect.copy(l_rect);
}
