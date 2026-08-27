#include "gui/widgets/MarkdownView.hpp"
#include "OS_Data.hpp"
#include "functions/GFX_Functions.hpp"
#include "gui/icons/icon_render.h"
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
    for (int i = 0; i < kCheckboxIconPoolSize; i++) {
        checkboxIconPool[i] = new Icon(kPadding, 0, IconID::CheckboxOff, kCheckboxIconSize);
        checkboxIconPool[i]->setParent(this);
        checkboxIconPool[i]->setVisible(false);
        checkboxIconPool[i]->setDisableMarkdirty(true);
        boundCheckboxIconBlock[i] = -1;
        children_.push_back(checkboxIconPool[i]);
    }
    for (int i = 0; i < kTableCellPoolSize; i++) {
        tableCellPool[i] = new Label(kPadding, 0, "");
        tableCellPool[i]->setParent(this);
        tableCellPool[i]->setVisible(false);
        tableCellPool[i]->setDisableMarkdirty(true);
        boundTableCellBlock[i] = -1;
        boundTableCellCol[i] = -1;
        children_.push_back(tableCellPool[i]);
    }

    checkboxIconPx = IconRender::IconPixelSize(kCheckboxIconSize);

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
    for (int i = 0; i < kCheckboxIconPoolSize; i++) boundCheckboxIconBlock[i] = -1;
    for (int i = 0; i < kTableCellPoolSize; i++) { boundTableCellBlock[i] = -1; boundTableCellCol[i] = -1; }
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
// 箇条書きの内容が `[ ] `/`[x] `/`[X] ` で始まる場合はチェックボックス項目
// （GFMのタスクリスト相当）として扱う。表示はアイコン（IconID::CheckboxOn/Off）で
// 行い、イミュータブル（タップ等での状態変更は不可）とする。
bool MarkdownView::tryParseListItem(int lineStart, int lineEnd, int& indentOut, bool& orderedOut,
                                     int& numberOut, int& contentStartOut,
                                     bool& isCheckboxOut, bool& checkedOut) const {
    isCheckboxOut = false;
    checkedOut = false;

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

        // チェックボックス（イミュータブル）: "[ ] " / "[x] " / "[X] "（内容が空の "[x]" のみも許容）
        if (contentStartOut + 3 <= lineEnd &&
            doc_text[contentStartOut] == '[' &&
            (doc_text[contentStartOut + 1] == ' ' || doc_text[contentStartOut + 1] == 'x' || doc_text[contentStartOut + 1] == 'X') &&
            doc_text[contentStartOut + 2] == ']') {
            int afterBracket = contentStartOut + 3;
            if (afterBracket == lineEnd || doc_text[afterBracket] == ' ') {
                isCheckboxOut = true;
                checkedOut = (doc_text[contentStartOut + 1] == 'x' || doc_text[contentStartOut + 1] == 'X');
                contentStartOut = (afterBracket == lineEnd) ? afterBracket : afterBracket + 1;
            }
        }

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

// ---------- 水平線の判定 ----------
// CommonMark準拠: 行頭(最大3個までの半角スペースを許容)から、`*`/`-`/`_`のいずれか
// 1種類の文字と半角スペース/タブのみで構成され、かつその文字が3個以上出現する場合に
// 水平線と判定する。他の文字が1つでも混じっていれば水平線ではない。
bool MarkdownView::tryParseThematicBreak(int lineStart, int lineEnd) const {
    int i = lineStart;
    int leadingSpaces = 0;
    while (i < lineEnd && doc_text[i] == ' ' && leadingSpaces < 3) { i++; leadingSpaces++; }
    if (i >= lineEnd) return false;

    char marker = doc_text[i];
    if (marker != '*' && marker != '-' && marker != '_') return false;

    int count = 0;
    for (int j = i; j < lineEnd; j++) {
        char c = doc_text[j];
        if (c == marker) {
            count++;
        } else if (c == ' ' || c == '\t' || c == '\r') {
            // マーカー文字の間の空白は許容する
        } else {
            return false;
        }
    }
    return count >= 3;
}

// ---------- 引用行の判定 ----------
// 行頭(最大3個までの半角スペースを許容)に `>` が1個以上連続している場合を
// 引用行と判定する。`>>text` のようにスペース無しでの連結、`> > text` のように
// スペースを挟んだ連結の両方に対応する（`>`の直後の半角スペース1個だけを区切りとして消費）。
bool MarkdownView::tryParseBlockquote(int lineStart, int lineEnd, int& depthOut, int& contentStartOut) const {
    int i = lineStart;
    int leadingSpaces = 0;
    while (i < lineEnd && doc_text[i] == ' ' && leadingSpaces < 3) { i++; leadingSpaces++; }
    if (i >= lineEnd || doc_text[i] != '>') return false;

    int depth = 0;
    while (i < lineEnd && doc_text[i] == '>') {
        depth++;
        i++;
        if (i < lineEnd && doc_text[i] == ' ') i++; // `>`直後の半角スペース1個は区切りとして消費
    }
    if (depth > kMaxListLevels) depth = kMaxListLevels;

    depthOut = depth - 1; // 0始まりに変換
    contentStartOut = i;
    return true;
}

// ---------- 先頭メタデータ(フロントマター)の無視 ----------
// 文書の先頭行が正確に "---" のみである場合にYAMLフロントマターの開始とみなし、
// 以降で再び "---" または "..." のみの行(終端フェンス)が現れる位置までをスキップする。
// 終端フェンスが見つからない場合は、誤検知で本文を消してしまわないよう
// 何もスキップせずstartPosをそのまま返す。
int MarkdownView::skipFrontMatter(int startPos, int len) const {
    int firstLineEnd = doc_text.indexOf('\n', startPos);
    if (firstLineEnd == -1) firstLineEnd = len;

    String firstLine = doc_text.substring(startPos, firstLineEnd);
    firstLine.trim();
    if (firstLine != "---") return startPos;

    int searchPos = (firstLineEnd == len) ? len : firstLineEnd + 1;
    while (searchPos <= len) {
        int nl = doc_text.indexOf('\n', searchPos);
        int lineEnd = (nl == -1) ? len : nl;

        String line = doc_text.substring(searchPos, lineEnd);
        line.trim();
        if (line == "---" || line == "...") {
            return (lineEnd == len) ? len : lineEnd + 1;
        }
        if (nl == -1) break; // 終端フェンスが見つからないまま文書末尾に到達
        searchPos = nl + 1;
    }

    return startPos; // 終端が見つからなかった＝フロントマターではない可能性が高いので何もしない
}

// ---------- テーブルの判定・分割 ----------

// 区切り行（例: `| --- | :---: | ---: |`）かどうかを判定する。
// 先頭/末尾の`|`と前後の空白は無視し、残りを`|`で分割した各セルが
// 「(任意の:)(1個以上の-)(任意の:)」のみで構成されることを要求する。
// 列数が1つも取れない場合や、いずれかのセルが不正な場合はfalseを返す。
bool MarkdownView::tryParseTableDelimiterRow(int lineStart, int lineEnd, uint8_t& colCountOut,
                                              uint8_t alignOut[kMdTableMaxCols]) const {
    int s = lineStart, e = lineEnd;
    while (s < e && doc_text[s] == ' ') s++;
    if (s < e && doc_text[s] == '|') s++;
    while (e > s && (doc_text[e - 1] == ' ' || doc_text[e - 1] == '\r')) e--;
    if (e > s && doc_text[e - 1] == '|') e--;
    if (s >= e) return false;

    uint8_t col = 0;
    int cellStart = s;

    auto finalizeCell = [&](int cellEnd) -> bool {
        int cs = cellStart, ce = cellEnd;
        while (cs < ce && doc_text[cs] == ' ') cs++;
        while (ce > cs && doc_text[ce - 1] == ' ') ce--;
        if (cs >= ce) return false; // 空セルは区切り行として不正

        bool leftColon = (doc_text[cs] == ':');
        bool rightColon = (doc_text[ce - 1] == ':');
        int dashStart = cs + (leftColon ? 1 : 0);
        int dashEnd = ce - (rightColon ? 1 : 0);
        if (dashStart >= dashEnd) return false; // ダッシュが1個も無い

        for (int k = dashStart; k < dashEnd; k++) {
            if (doc_text[k] != '-') return false;
        }

        if (col < kMdTableMaxCols) {
            alignOut[col] = (leftColon && rightColon) ? 1 : (rightColon ? 2 : 0);
        }
        col++;
        return true;
    };

    for (int i = s; i <= e; i++) {
        bool atEnd = (i == e);
        if (atEnd || doc_text[i] == '|') {
            if (!finalizeCell(i)) return false;
            cellStart = i + 1;
        }
    }

    if (col == 0) return false;
    colCountOut = (col > (uint8_t)kMdTableMaxCols) ? (uint8_t)kMdTableMaxCols : col;
    return true;
}

// ヘッダ候補行の直後の行が区切り行として成立するかを判定する。
// headerEnd は呼び出し側の lineEnd と同じ意味（'\n'の位置、または末尾ならlen）。
bool MarkdownView::detectTableStart(int headerStart, int headerEnd, int len, uint8_t& colCountOut,
                                     uint8_t alignOut[kMdTableMaxCols], int& delimLineEndOut) const {
    (void)headerStart;
    if (headerEnd >= len) return false; // ヘッダ候補行が文書末尾＝区切り行が存在し得ない

    int delimStart = headerEnd + 1;
    if (delimStart > len) return false;

    int delimNl = doc_text.indexOf('\n', delimStart);
    int delimEnd = (delimNl == -1) ? len : delimNl;

    if (!tryParseTableDelimiterRow(delimStart, delimEnd, colCountOut, alignOut)) return false;

    delimLineEndOut = delimEnd;
    return true;
}

// 行を`|`区切りでセルに分割する。先頭/末尾の`|`は区切りとして扱い、`\|`は
// エスケープされたパイプ文字として分割対象から除外する（アンエスケープ自体は
// formatTableCellText()側で行う）。各セルの前後の空白はトリムする。
void MarkdownView::splitTableRow(int lineStart, int lineEnd, uint16_t cellOffsetOut[kMdTableMaxCols],
                                  uint16_t cellLengthOut[kMdTableMaxCols], uint8_t maxCols, uint8_t& cellCountOut) const {
    for (int c = 0; c < kMdTableMaxCols; c++) {
        cellOffsetOut[c] = 0;
        cellLengthOut[c] = 0;
    }
    cellCountOut = 0;

    int s = lineStart, e = lineEnd;
    while (s < e && doc_text[s] == ' ') s++;
    if (s < e && doc_text[s] == '|') s++;
    while (e > s && (doc_text[e - 1] == ' ' || doc_text[e - 1] == '\r')) e--;
    if (e > s && doc_text[e - 1] == '|') e--;
    if (s > e) s = e;

    uint8_t col = 0;
    int cellStart = s;

    for (int i = s; i <= e; i++) {
        bool atEnd = (i == e);
        bool isPipe = !atEnd && doc_text[i] == '|';
        bool isEscapedPipe = isPipe && i > cellStart && doc_text[i - 1] == '\\';
        if (atEnd || (isPipe && !isEscapedPipe)) {
            int cs = cellStart, ce = i;
            while (cs < ce && doc_text[cs] == ' ') cs++;
            while (ce > cs && doc_text[ce - 1] == ' ') ce--;

            if (col < maxCols && col < kMdTableMaxCols) {
                cellOffsetOut[col] = (uint16_t)cs;
                cellLengthOut[col] = (uint16_t)(ce - cs);
            }
            col++;
            cellStart = i + 1;
        }
    }

    cellCountOut = col;
}

// テーブルセル1つ分の表示用テキストを生成する。splitTableRow()が分割時に
// 読み飛ばした`\|`はここでアンエスケープして`|`に戻す。
String MarkdownView::formatTableCellText(int offset, int length) const {
    String raw = doc_text.substring(offset, offset + length);
    String unescaped;
    const int n = raw.length();
    for (int i = 0; i < n; i++) {
        if (raw[i] == '\\' && i + 1 < n && raw[i + 1] == '|') {
            unescaped += '|';
            i++; // '|' は消費済み
        } else {
            unescaped += raw[i];
        }
    }
    return applyInlineMarkdown(unescaped);
}

void MarkdownView::parseBlocks() {
    blocks.clear();
    blocks.reserve(kMaxBlocks);

    const int len = doc_text.length();
    int pos = skipFrontMatter(0, len);
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

        // テーブル判定用（'|'を含む行のみ、区切り行チェックに進む）
        int quickPipe = -1;
        if (!isBlank) quickPipe = doc_text.indexOf('|', pos);
        uint8_t tblColCount = 0;
        uint8_t tblAlign[kMdTableMaxCols] = {};
        int tblDelimLineEnd = -1;
        bool isTableStart = (quickPipe != -1 && quickPipe < lineEnd) &&
                             detectTableStart(pos, lineEnd, len, tblColCount, tblAlign, tblDelimLineEnd);

        if (isBlank) {
            flushParagraph(pos > 0 ? pos - 1 : pos);
            // 空行だけではリストの番号は途切れさせない（tight/looseリスト双方に対応）
        } else if (tryParseThematicBreak(pos, lineEnd)) {
            // 水平線（***, ---, ___）
            flushParagraph(pos > 0 ? pos - 1 : pos);
            resetListCounters();

            if ((int)blocks.size() < kMaxBlocks) {
                MdBlock b{};
                b.type = MdBlockType::HorizontalRule;
                b.srcOffset = pos;
                b.srcLength = 0; // テキストは持たない。render()内で直接罫線を描画する
                blocks.push_back(b);
            }
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
        } else if (isTableStart) {
            // ---- テーブル ----
            flushParagraph(pos > 0 ? pos - 1 : pos);
            resetListCounters();

            // ヘッダ行
            if ((int)blocks.size() < kMaxBlocks) {
                MdBlock b{};
                b.type = MdBlockType::TableRow;
                b.tableIsHeader = true;
                b.tableColCount = tblColCount;
                for (int c = 0; c < kMdTableMaxCols; c++) b.tableAlign[c] = tblAlign[c];
                uint8_t cellCount = 0;
                splitTableRow(pos, lineEnd, b.tableCellOffset, b.tableCellLength, tblColCount, cellCount);
                blocks.push_back(b);
            }

            // 区切り行を読み飛ばして、続くデータ行を読めるだけ読む
            // （空行、'|'を含まない行、または文書末尾でテーブル終了とみなす）
            int dataPos = (tblDelimLineEnd == len) ? len : tblDelimLineEnd + 1;
            while (dataPos < len && (int)blocks.size() < kMaxBlocks) {
                int rNl = doc_text.indexOf('\n', dataPos);
                int rLineEnd = (rNl == -1) ? len : rNl;

                bool rBlank = true;
                for (int k = dataPos; k < rLineEnd; k++) {
                    char ch = doc_text[k];
                    if (ch != ' ' && ch != '\t' && ch != '\r') { rBlank = false; break; }
                }
                if (rBlank) break;

                int rPipe = doc_text.indexOf('|', dataPos);
                if (rPipe == -1 || rPipe >= rLineEnd) break; // '|'の無い行でテーブル終了

                MdBlock rb{};
                rb.type = MdBlockType::TableRow;
                rb.tableIsHeader = false;
                rb.tableColCount = tblColCount;
                for (int c = 0; c < kMdTableMaxCols; c++) rb.tableAlign[c] = tblAlign[c];
                uint8_t rCellCount = 0;
                splitTableRow(dataPos, rLineEnd, rb.tableCellOffset, rb.tableCellLength, tblColCount, rCellCount);
                blocks.push_back(rb);

                dataPos = (rNl == -1) ? len : rNl + 1;
            }

            pos = dataPos;
            continue; // posを直接更新したので、ループ末尾の共通処理をスキップする
        } else {
            int qDepth, qContentStart;
            if (tryParseBlockquote(pos, lineEnd, qDepth, qContentStart)) {
                flushParagraph(pos > 0 ? pos - 1 : pos);
                resetListCounters();

                if ((int)blocks.size() < kMaxBlocks) {
                    MdBlock b{};
                    b.type = MdBlockType::Quote;
                    b.srcOffset = qContentStart;
                    b.srcLength = lineEnd - qContentStart;
                    b.quoteDepth = (uint8_t)qDepth;

                    uint16_t linkOff, linkLen;
                    if (findFirstInlineLink(b.srcOffset, b.srcOffset + b.srcLength, linkOff, linkLen)) {
                        b.urlOffset = linkOff;
                        b.urlLength = linkLen;
                    }

                    blocks.push_back(b);
                }
            } else {
                int indent, number, contentStart;
                bool ordered, isCheckbox, checked;
                if (tryParseListItem(pos, lineEnd, indent, ordered, number, contentStart, isCheckbox, checked)) {
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
                        b.listIsCheckbox = isCheckbox;
                        b.listChecked = checked;

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
            if (b.listIsCheckbox) {
                // マーカーはアイコン(IconID::CheckboxOn/Off)側で表現するため、
                // テキスト側には付与しない
                return applyInlineMarkdown(content);
            }
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
        case MdBlockType::Quote: {
            String content = doc_text.substring(b.srcOffset, b.srcOffset + b.srcLength);
            return applyInlineMarkdown(content);
        }
        case MdBlockType::HorizontalRule:
        case MdBlockType::TableRow:
            return ""; // どちらもLabel1個には対応しない要素。専用のバインド処理で個別に描画する
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
        else if (b.type == MdBlockType::HorizontalRule) {
            // Labelを介さない固定高さの罫線
            b.height = kHrHeight;
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
            int extra = b.listIsCheckbox ? (checkboxIconPx + kCheckboxIconGap) : 0;
            int w = viewport_w - indentPx - extra;
            if (w < 20) w = 20; // 極端なネストでも最低限の幅を確保
            measure_label->setMaxWidth(w);
            measure_label->setFontSize(fontSizeForBlock(b.type));
            measure_label->setText(formatBlockText(b));
            int textH = measure_label->getH();
            // チェックボックス項目はアイコンの縦幅もブロック高さに含める
            b.height = b.listIsCheckbox ? (uint16_t)std::max<int>(textH, checkboxIconPx) : textH;
        }
        else if (b.type == MdBlockType::Quote) {
            // (quoteDepth+1)段分のインデント幅を、縦バーの描画スペースも兼ねて確保する
            int indentPx = (b.quoteDepth + 1) * kQuoteIndentWidth;
            int w = viewport_w - indentPx;
            if (w < 20) w = 20;
            measure_label->setMaxWidth(w);
            measure_label->setFontSize(fontSizeForBlock(b.type));
            measure_label->setText(formatBlockText(b));
            b.height = measure_label->getH();
        }
        else if (b.type == MdBlockType::TableRow) {
            int colCount = std::max<int>(1, (int)b.tableColCount);
            int colWidth = viewport_w / colCount;
            int cellW = colWidth - kTableCellPadding * 2;
            if (cellW < 10) cellW = 10;

            int maxH = kTableMinRowHeight;
            for (int c = 0; c < b.tableColCount && c < kMdTableMaxCols; c++) {
                measure_label->setMaxWidth(cellW);
                measure_label->setFontSize(fontSizeForBlock(b.type));
                String cellText = formatTableCellText(b.tableCellOffset[c], b.tableCellLength[c]);
                measure_label->setText(b.tableIsHeader ? ("**" + cellText + "**") : cellText);
                int h = measure_label->getH();
                if (h > maxH) maxH = h;
            }
            b.height = maxH;
        }
        else {
            measure_label->setMaxWidth(viewport_w);
            measure_label->setFontSize(fontSizeForBlock(b.type));
            measure_label->setText(formatBlockText(b));
            b.height = measure_label->getH();
        }

        b.y = y;

        // 連続する同種の要素同士は間隔を詰めて、まとまりを見やすくする
        int spacing = kBlockSpacing;
        if (b.type == MdBlockType::ListItem &&
            idx + 1 < blocks.size() && blocks[idx + 1].type == MdBlockType::ListItem) {
            spacing = kBlockSpacing / 2;
        }
        if (b.type == MdBlockType::Quote &&
            idx + 1 < blocks.size() && blocks[idx + 1].type == MdBlockType::Quote &&
            blocks[idx + 1].quoteDepth == b.quoteDepth) {
            spacing = kBlockSpacing / 2;
        }
        if (b.type == MdBlockType::TableRow &&
            idx + 1 < blocks.size() && blocks[idx + 1].type == MdBlockType::TableRow) {
            spacing = 0; // 行同士を隙間なく詰めて、罫線が格子状に繋がるようにする
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
        case MdBlockType::Quote:    return FontFn::FontSize::Small;
        case MdBlockType::TableRow: return FontFn::FontSize::Small;
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

    int labelSlot = 0, imageSlot = 0, checkboxSlot = 0, tableCellSlot = 0;

    for (int i = lo; i < (int)blocks.size() && blocks[i].y < viewport_bottom; i++) {
        const MdBlock& b = blocks[i];

        if (b.type == MdBlockType::Image) {
            if (imageSlot >= kImagePoolSize) continue; // プール枯渇。TODO: 画像密度が高い文書向けにプール拡張を検討
            bindImageSlot(imageSlot++, i, force);
        } else if (b.type == MdBlockType::HorizontalRule) {
            continue; // Labelを持たない要素。renderDecorations()内で直接描画する
        } else if (b.type == MdBlockType::TableRow) {
            for (int c = 0; c < b.tableColCount && c < kMdTableMaxCols; c++) {
                if (tableCellSlot >= kTableCellPoolSize) break; // プール枯渇。TODO: 大きな表向けにプール拡張を検討
                bindTableCellSlot(tableCellSlot++, i, c, force);
            }
        } else {
            if (labelSlot >= kLabelPoolSize) continue;
            bindLabelSlot(labelSlot++, i, force);

            if (b.type == MdBlockType::ListItem && b.listIsCheckbox && checkboxSlot < kCheckboxIconPoolSize) {
                bindCheckboxIconSlot(checkboxSlot++, i, force);
            }
        }
    }

    for (; labelSlot < kLabelPoolSize; labelSlot++) hideLabelSlot(labelSlot);
    for (; imageSlot < kImagePoolSize; imageSlot++) hideImageSlot(imageSlot);
    for (; checkboxSlot < kCheckboxIconPoolSize; checkboxSlot++) hideCheckboxIconSlot(checkboxSlot);
    for (; tableCellSlot < kTableCellPoolSize; tableCellSlot++) hideTableCellSlot(tableCellSlot);
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
        int extra = b.listIsCheckbox ? (checkboxIconPx + kCheckboxIconGap) : 0;
        int w = this->l_rect.w - SCROLL_L - kPadding * 2 - indentPx - extra;
        if (w < 20) w = 20;
        lbl->setMaxWidth(w);
        lbl->setFontSize(fontSizeForBlock(b.type));
        lbl->setText(formatBlockText(b));
        lbl->setX(kPadding + indentPx + extra);
    } else if (b.type == MdBlockType::Quote) {
        if(lbl->getDisableAutoTextDecoration())
            lbl->setDisableAutoTextDecoration(false);
        int indentPx = (b.quoteDepth + 1) * kQuoteIndentWidth;
        int w = this->l_rect.w - SCROLL_L - kPadding * 2 - indentPx;
        if (w < 20) w = 20;
        lbl->setMaxWidth(w);
        lbl->setTextColor(kQuoteTextColor); // 引用であることを視覚的に区別するため、やや薄い色にする
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

// チェックボックス項目のマーカー用アイコンをバインドする。イミュータブル表示専用
// （タップでの状態変更は行わない）。テキスト側(bindLabelSlot)とは独立して、
// 同じブロックの左側に重ねて配置する。
void MarkdownView::bindCheckboxIconSlot(int slot, int blockIdx, bool force) {
    if (!force && boundCheckboxIconBlock[slot] == blockIdx) {
        checkboxIconPool[slot]->setVisible(true);
        return;
    }
    const MdBlock& b = blocks[blockIdx];
    Icon* icon = checkboxIconPool[slot];

    icon->setIconId(b.listChecked ? IconID::CheckboxOn : IconID::CheckboxOff);

    int indentPx = b.listIndent * kListIndentWidth;
    icon->setX(kPadding + indentPx);
    icon->setY(b.y);
    icon->setVisible(true);

    boundCheckboxIconBlock[slot] = blockIdx;
}

// テーブルのセル1つ分をLabelにバインドする。列幅はビューポート幅を列数で均等割りし、
// 前後にkTableCellPadding分の余白を設ける。文字寄せ(tableAlign)はLabel側が非対応のため
// 現状は常に左寄せとなる。
void MarkdownView::bindTableCellSlot(int slot, int blockIdx, int col, bool force) {
    if (!force && boundTableCellBlock[slot] == blockIdx && boundTableCellCol[slot] == col) {
        tableCellPool[slot]->setVisible(true);
        return;
    }
    const MdBlock& b = blocks[blockIdx];
    Label* lbl = tableCellPool[slot];

    const int viewport_w = this->l_rect.w - SCROLL_L - kPadding * 2;
    const int colCount = std::max<int>(1, (int)b.tableColCount);
    const int colWidth = viewport_w / colCount;

    int w = colWidth - kTableCellPadding * 2;
    if (w < 10) w = 10;

    lbl->setNoBackground();
    lbl->setBorder(PICO_BLACK, 0);
    lbl->setDisableAutoTextDecoration(false);
    lbl->setTextColor(PICO_BLACK);
    if (b.tableIsHeader) {
        lbl->setBackgroundColor(PICO_LIGHTGREY); // ヘッダ行は背景色で強調
    }
    lbl->setMaxWidth(w);
    lbl->setFontSize(fontSizeForBlock(b.type));

    String cellText = formatTableCellText(b.tableCellOffset[col], b.tableCellLength[col]);
    lbl->setText(b.tableIsHeader ? ("**" + cellText + "**") : cellText);

    lbl->setX(kPadding + col * colWidth + kTableCellPadding);
    lbl->setY(b.y);
    lbl->setVisible(true);

    boundTableCellBlock[slot] = blockIdx;
    boundTableCellCol[slot] = col;
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

void MarkdownView::hideCheckboxIconSlot(int slot) {
    if (boundCheckboxIconBlock[slot] == -1) return;
    checkboxIconPool[slot]->setVisible(false);
    boundCheckboxIconBlock[slot] = -1;
}

void MarkdownView::hideTableCellSlot(int slot) {
    if (boundTableCellBlock[slot] == -1) return;
    tableCellPool[slot]->setVisible(false);
    boundTableCellBlock[slot] = -1;
    boundTableCellCol[slot] = -1;
}

// ---------- 装飾の直接描画（水平線の罫線・引用の縦バー・テーブルの罫線） ----------
// Labelプールに乗らない/乗せる必要のない視覚要素を、bindVisibleBlocks()と同じ
// ビューポート走査ロジックでビューポート内のみ対象に直接描画する。
// render()の最後（子ウィジェット描画の前）に呼び出す想定。
void MarkdownView::renderDecorations() {
    const int viewport_top = scroll_y;
    const int viewport_bottom = scroll_y + this->l_rect.h;

    int lo = 0, hi = (int)blocks.size();
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (blocks[mid].y + (int)blocks[mid].height < viewport_top) lo = mid + 1;
        else hi = mid;
    }

    const Rect g_rect = this->getScreenRect();
    const int contentTop = g_rect.y - scroll_y; // scroll_y=0のときの「文書y=0」が画面上で来る位置
    const int viewport_w = this->l_rect.w - SCROLL_L - kPadding * 2;

    // 枠線(1px)を潰さないよう、描画可能な縦範囲を1pxずつ内側に絞る
    const int clipTop = g_rect.y + 1;
    const int clipBottom = g_rect.y + g_rect.h - 1;

    for (int i = lo; i < (int)blocks.size() && blocks[i].y < viewport_bottom; i++) {
        const MdBlock& b = blocks[i];

        if (b.type == MdBlockType::HorizontalRule) {
            int lineY = contentTop + (int)b.y + (int)b.height / 2;
            if (lineY < clipTop || lineY >= clipBottom) continue;
            OSData::frame->fillRect(g_rect.x + kPadding, lineY, viewport_w, 1, kHrColor);
        } else if (b.type == MdBlockType::Quote) {
            int barTop = contentTop + (int)b.y;
            int barBottom = barTop + (int)b.height;
            if (barTop < clipTop) barTop = clipTop;
            if (barBottom > clipBottom) barBottom = clipBottom;
            if (barBottom <= barTop) continue;

            // ネスト段数分だけ縦バーを重ねて描画する（GitHub等の表示に準拠）
            for (int lvl = 0; lvl <= (int)b.quoteDepth; lvl++) {
                int barX = g_rect.x + kPadding + lvl * kQuoteIndentWidth;
                OSData::frame->fillRect(barX, barTop, kQuoteBarWidth, barBottom - barTop, kQuoteBarColor);
            }
        } else if (b.type == MdBlockType::TableRow) {
            int rowTop = contentTop + (int)b.y;
            int rowBottom = rowTop + (int)b.height;

            int drawTop = rowTop, drawBottom = rowBottom;
            if (drawTop < clipTop) drawTop = clipTop;
            if (drawBottom > clipBottom) drawBottom = clipBottom;

            int colCount = std::max<int>(1, (int)b.tableColCount);
            int colWidth = viewport_w / colCount;

            // 上辺（先頭行なら表全体の上端、以降の行では前の行との境界線を兼ねる）
            if (rowTop >= clipTop && rowTop < clipBottom) {
                OSData::frame->fillRect(g_rect.x + kPadding, rowTop, viewport_w, 1, kTableLineColor);
            }
            // 下辺（このテーブルの最後の行のときだけ描画し、二重線にならないようにする）
            bool isLastRow = (i + 1 >= (int)blocks.size()) || (blocks[i + 1].type != MdBlockType::TableRow);
            if (isLastRow && rowBottom > clipTop && rowBottom <= clipBottom) {
                OSData::frame->fillRect(g_rect.x + kPadding, rowBottom - 1, viewport_w, 1, kTableLineColor);
            }
            // 縦罫線（列の左右境界。右端は割り切れない余りを吸収して確実に内側に収める）
            if (drawBottom > drawTop) {
                for (int c = 0; c <= colCount; c++) {
                    int lineX = (c == colCount)
                        ? (g_rect.x + kPadding + viewport_w - 1)
                        : (g_rect.x + kPadding + c * colWidth);
                    OSData::frame->fillRect(lineX, drawTop, 1, drawBottom - drawTop, kTableLineColor);
                }
            }
        }
    }
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
    scroll_y = constrain(new_y, 0, max_scroll_y);

    unsigned long now = millis();
    if (now - last_scroll_render_ms < 33) return;
    last_scroll_render_ms = now;

    bindVisibleBlocks(false);
    this->needsRender();
    for (int i = 0; i < kLabelPoolSize; i++) labelPool[i]->needsRender();
    for (int i = 0; i < kImagePoolSize; i++) imagePool[i]->needsRender();
    for (int i = 0; i < kCheckboxIconPoolSize; i++) checkboxIconPool[i]->needsRender();
    for (int i = 0; i < kTableCellPoolSize; i++) tableCellPool[i]->needsRender();
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
        // Link型の行だけでなく、段落/見出し/リスト項目/引用にインラインリンクが
        // 見つかっている場合(urlLength > 0)もタップで開けるようにする。
        // 1ブロックに複数リンクがある場合は最初の1件のみが対象になる点に注意。
        if (idx >= 0 && blocks[idx].urlLength > 0 && on_link_tap) {
            String url = doc_text.substring(blocks[idx].urlOffset, blocks[idx].urlOffset + blocks[idx].urlLength);
            on_link_tap(url);
        }
    }

    is_scrolling = false;
}

// ---------- 描画（枠・スクロールバー・装飾のみ。背景/子はコンポジタが処理） ----------

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

    renderDecorations();

    markdirty(g_rect);
    prev_l_rect.copy(l_rect);
}
