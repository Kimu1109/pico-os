#include "widgets/Label.hpp"
#include "functions/GFX_Functions.hpp"
#include "OS_Data.hpp"

void Label::needsRender(){
    this->needs_redraw = true;
    PICO_GFX::markDirty(this->rect);
}

// ---------- UTF-8 ----------
int Label::utf8CharLen(uint8_t lead) {
    if ((lead & 0x80) == 0x00) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 1; // 不正バイト列へのフォールバック
}

std::vector<String> Label::splitChars(const String& s) {
    std::vector<String> out;
    size_t i = 0, n = s.length();
    while (i < n) {
        int len = utf8CharLen((uint8_t)s[i]);
        String c;
        for (int k = 0; k < len && i < n; k++, i++) c += s[i];
        out.push_back(c);
    }
    return out;
}

// ---------- マークアップ解析 ----------
// **太字** / _下線_ / ~波線~ （入れ子非対応・単純トグル方式）
std::vector<TextRun> Label::parseMarkup(const String& src) {
    std::vector<TextRun> runs;
    TextRun cur;
    size_t i = 0, n = src.length();

    auto flush = [&]() {
        if (cur.text.length() > 0) {
            runs.push_back(cur);
            cur.text = "";
        }
    };

    while (i < n) {
        if (src[i] == '*' && i + 1 < n && src[i + 1] == '*') {
            flush();
            cur.bold = !cur.bold;
            i += 2;
            continue;
        }
        if (src[i] == '_') {
            flush();
            cur.underline = !cur.underline;
            i += 1;
            continue;
        }
        if (src[i] == '~') {
            flush();
            cur.wavy = !cur.wavy;
            i += 1;
            continue;
        }
        int len = utf8CharLen((uint8_t)src[i]);
        for (int k = 0; k < len && i < n; k++, i++) cur.text += src[i];
    }
    flush();
    return runs;
}

// ---------- 折返し込みレイアウト計算 ----------
void Label::relayout() {
    lines.clear();
    line_height = OSData::frame->fontHeight();

    // \n で段落分割
    std::vector<String> paragraphs;
    {
        String buf;
        for (size_t i = 0; i < raw_text.length(); i++) {
            if (raw_text[i] == '\n') { paragraphs.push_back(buf); buf = ""; }
            else buf += raw_text[i];
        }
        paragraphs.push_back(buf);
    }

    for (auto& para : paragraphs) {
        std::vector<TextRun> runs = parseMarkup(para);

        std::vector<TextRun> curLine;
        int curWidth = 0;
        TextRun piece;

        for (auto& run : runs) {
            piece.text = "";
            piece.bold = run.bold;
            piece.underline = run.underline;
            piece.wavy = run.wavy;

            for (auto& ch : splitChars(run.text)) {
                int cw = OSData::frame->textWidth(ch);
                if (run.bold) cw += 1; // 疑似太字の分の余白

                if (max_width > 0 && curWidth > 0 && curWidth + cw > max_width) {
                    if (piece.text.length() > 0) { curLine.push_back(piece); piece.text = ""; }
                    lines.push_back(curLine);
                    curLine.clear();
                    curWidth = 0;
                }
                piece.text += ch;
                curWidth += cw;
            }
            if (piece.text.length() > 0) { curLine.push_back(piece); piece.text = ""; }
        }
        lines.push_back(curLine);
    }

    // 全体サイズの再計算
    int maxLineWidth = 0;
    for (auto& line : lines) {
        int lw = 0;
        for (auto& run : line) {
            int rw = OSData::frame->textWidth(run.text);
            if (run.bold) rw += 1;
            lw += rw;
        }
        if (lw > maxLineWidth) maxLineWidth = lw;
    }

    this->rect.w = (max_width > 0) ? max_width : maxLineWidth;
    this->rect.h = lines.empty() ? 0
            : (int)lines.size() * (line_height + line_spacing) - line_spacing + kDecorationMargin;
    this->needsRender();
}

// ---------- 1つのRunを描画 ----------
void Label::renderRun(const TextRun& run, int x, int y) {
    OSData::frame->setTextColor(this->color);
    OSData::frame->setCursor(x, y);
    OSData::frame->print(run.text);

    int rw = OSData::frame->textWidth(run.text);

    if (run.bold) {
        // 疑似太字: 1px右にずらして重ね描き
        OSData::frame->setCursor(x + 1, y);
        OSData::frame->print(run.text);
        rw += 1;
    }

    int baseline = y + line_height - 1;

    if (run.underline) {
        OSData::frame->drawFastHLine(x, baseline, rw, this->color);
    }

    if (run.wavy) {
        // 波線: step間隔でジグザグに線をつなぐ
        int step = 3;
        int amp = 1;
        int wy = baseline + 1;
        int px = x, py = wy;
        bool up = false;
        for (int dx = step; dx <= rw - step; dx += step) {
            int nx = x + dx;
            int ny = wy + (up ? -amp : amp);
            OSData::frame->drawLine(px, py, nx, ny, this->color);
            px = nx; py = ny;
            up = !up;
        }
    }
}

// ---------- コンストラクタ ----------
Label::Label(int x, int y, String text) {
    this->rect.x = x;
    this->rect.y = y;
    this->Text(text);
    this->needs_redraw = true;
}

Label::Label(String text) {
    this->Text(text);
    this->needs_redraw = true;
}

// ---------- render ----------
void Label::render() {
    if (!this->needs_redraw) return;
    if (!this->visible) return;

    // 前回の描画内容を消去
    PICO_GFX::markDirty(this->prev_rect);

    // print()側の自動折り返しを無効化
    OSData::frame->setTextWrap(false, false);

    // 新しく描画
    int cy = this->rect.y;
    for (auto& line : lines) {
        int cx = this->rect.x;
        for (auto& run : line) {
            renderRun(run, cx, cy);
            int rw = OSData::frame->textWidth(run.text);
            if (run.bold) rw += 1;
            cx += rw;
        }
        cy += line_height + line_spacing;
    }
    PICO_GFX::markDirty(this->rect);

    this->prev_rect.copy(this->rect);

    this->needs_redraw = false;
}

// ---------- setter / getter ----------
void Label::Text(String text) {
    this->raw_text = text;
    relayout();
}

String Label::Text() {
    return this->raw_text;
}

void Label::MaxWidth(int width) {
    this->max_width = width;
    relayout();
}

int Label::MaxWidth() {
    return this->max_width;
}

void Label::LineSpacing(int spacing) {
    this->line_spacing = spacing;
    relayout();
}

void Label::Color(uint16_t c) {
    this->color = c;
    this->needsRender();
}

void Label::X(int x) {
    this->rect.x = x;
    this->needsRender();
}

int Label::X() {
    return this->rect.x;
}

void Label::Y(int y) {
    this->rect.y = y;
    this->needsRender();
}

int Label::Y() {
    return this->rect.y;
}

int Label::W() {
    return this->rect.w;
}

int Label::H() {
    return this->rect.h;
}

void Label::Visible(bool visible){
    this->visible = visible;
    this->needsRender();
}