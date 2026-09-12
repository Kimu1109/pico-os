
// -------------------------------------------------------------------
// 実装
// -------------------------------------------------------------------

#include "gui/widgets/Label.hpp"
#include <utility>
#include "functions/GFX_Functions.hpp"
#include "OS_Data.hpp"

template<size_t N>
void Label<N>::needsRender() {
    this->needs_redraw = true;
    markdirty(this->getScreenRect());
    markdirty(this->prev_cursor_rect);
}

template<size_t N>
void Label<N>::ensureLayout() const {
    if (!this->needs_relayout) return;
    //relayout()はレイアウト結果(可視状態ではない)を更新するだけなので、
    //const getterから呼んでも観測できる振る舞いは変わらない
    const_cast<Label<N>*>(this)->relayout();
}

template<size_t N>
Rect Label<N>::staleScreenRect() const {
    const int sx = (parent ? parent->getScreenX() - parent->getScrollOffsetX() : 0) + l_rect.x;
    const int sy = (parent ? parent->getScreenY() - parent->getScrollOffsetY() : 0) + l_rect.y;
    return { (int16_t)sx, (int16_t)sy, l_rect.w, l_rect.h };
}

template<size_t N>
void Label<N>::invalidateLayout() {
    //ここでdirty登録するのは「消すべき古い領域」なので、再計算前の矩形を使う。
    //needsRender()はgetScreenRect()経由でensureLayout()を呼んでしまい、
    //せっかく遅延させた再計算がその場で走ってしまう
    this->needs_redraw = true;
    markdirty(this->staleScreenRect());
    markdirty(this->prev_cursor_rect);

    this->needs_relayout = true;
}

template<size_t N>
Rect Label<N>::getLocalRect() const {
    this->ensureLayout();
    return l_rect;
}

template<size_t N>
int Label<N>::getW() {
    this->ensureLayout();
    return this->l_rect.w;
}

template<size_t N>
int Label<N>::getH() {
    this->ensureLayout();
    return this->l_rect.h;
}

// 文字列中の位置iから1文字ぶんを取り出してNUL終端でoutへ書き、次の位置を返す。
// (バイト数判定は util/Utf8Byte.hpp の Utf8CharBytesFromLeadByte() を使う。
//  以前はLabel内にutf8CharLen()という同一実装の重複があったので削除した)
//
// 以前は splitChars() が「1文字につきFixedString<5>を1要素」のvectorを組み立てていたが、
// 呼び出し側は1文字ずつしか使わないので、その分の確保(日本語120字で8回4080B)は丸ごと無駄だった。
// その場でUTF-8境界を進めることで確保はゼロになる。
static inline size_t nextUtf8Char(const char* s, size_t n, size_t i, char out[5], int& outLen) {
    int len = Utf8CharBytesFromLeadByte((uint8_t)s[i]);
    if (i + (size_t)len > n) len = (int)(n - i);
    memcpy(out, s + i, (size_t)len);
    out[len] = '\0';
    outLen = len;
    return i + (size_t)len;
}

template<size_t N>
std::vector<TextRun> Label<N>::parseMarkup(const char* src, size_t n) {
    std::vector<TextRun> runs;
    TextRun cur;
    if (!src) return runs;
    size_t i = 0;

    auto flush = [&]() {
        if (cur.text.length() > 0) {
            runs.push_back(cur);
            cur.text.clear();
        }
    };

    if (disable_auto_text_decoration) {
        cur.text.assign(src);
        flush();
    } else {
        while (i < n) {
            if (src[i] == '*' && i + 1 < n && src[i + 1] == '*') {
                flush();
                cur.bold = !cur.bold;
                i += 2;
                continue;
            }
            if (src[i] == '~' && i + 1 < n && src[i + 1] == '~') {
                flush();
                cur.strikethrough = !cur.strikethrough;
                i += 2;
                continue;
            }
            if (src[i] == '_' || src[i] == '*') {
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
            int len = Utf8CharBytesFromLeadByte((uint8_t)src[i]);
            if (i + len > n) len = n - i;
            cur.text.appendUtf8Char(&src[i], len);
            i += len;
        }
    }
    flush();
    return runs;
}

template<size_t N>
void Label<N>::computeLineOffsets(const std::vector<std::vector<TextRun>>& src_lines, int box_width, std::vector<int>& out) {
    out.assign(src_lines.size(), 0);
    if (this->text_align == TextAlign::Left) return;

    for (size_t li = 0; li < src_lines.size(); li++) {
        int lw = 0;
        for (auto& run : src_lines[li]) {
            int rw = run.width; // relayout()側で計算済みの幅を再利用(再計測しない)
            if (run.bold) rw += 1;
            lw += rw;
        }

        int avail = box_width - lw;
        if (avail < 0) avail = 0;

        if (this->text_align == TextAlign::Center) {
            out[li] = avail / 2;
        } else {
            out[li] = avail;
        }
    }
}

template<size_t N>
void Label<N>::relayout() {
    //先に下ろしておく。この関数の末尾のneedsRender()がgetScreenRect()経由で
    //ensureLayout()を呼び返すため、ここを立てたままだと再入する
    this->needs_relayout = false;

    this->fontApply();

    lines.clear();
    cursor_slots.clear();
    line_height = OSData::frame->fontHeight();

    // 段落(\n区切り)はraw_textの部分文字列なので、コピーせず(offset,length)で参照する。
    //
    // 以前は std::vector<FixedString<N>> へ丸ごとコピーしていた。この要素サイズは
    // テキストの長さではなくテンプレート引数Nに比例するため、7文字のLabel<1KiB>でも
    // 1段落あたり1032B、3段落なら3640Bをrelayout()のたびに確保していた。
    const char* s = raw_text.c_str();
    const size_t total = raw_text.length();

    {
        CursorSlot head;
        head.line = 0;
        head.x = 0;
        cursor_slots.push_back(head);
    }

    size_t para_start = 0;
    bool last_paragraph = false;
    while (!last_paragraph) {
        size_t para_end = para_start;
        while (para_end < total && s[para_end] != '\n') para_end++;
        last_paragraph = (para_end >= total);

        std::vector<TextRun> runs = parseMarkup(s + para_start, para_end - para_start);

        std::vector<TextRun> curLine;
        int curWidth = 0;
        TextRun piece;

        for (auto& run : runs) {
            piece.text.clear();
            piece.width = 0;
            piece.bold = run.bold;
            piece.underline = run.underline;
            piece.wavy = run.wavy;
            piece.strikethrough = run.strikethrough;

            const char* rp = run.text.c_str();
            const size_t rn = run.text.length();
            char ch[5];
            int chLen = 0;
            for (size_t ci = 0; ci < rn; ) {
                ci = nextUtf8Char(rp, rn, ci, ch, chLen);

                // chWは太字加算を含まない素の文字幅。piece.widthにはこちらを積算し、
                // computeLineOffsets()/render()側でtextWidth(run.text.c_str())を
                // 呼んだ場合と同じ値になるようにする(太字分の+1はそれらの呼び出し側で
                // 1回だけ加算される想定のため、ここで重ねて加算しない)。
                int chW = OSData::frame->textWidth(ch);
                int cw = chW + (run.bold ? 1 : 0); // 折り返し判定用(太字は従来通り1文字ごとに+1)

                if (max_width > 0 && curWidth > 0 && curWidth + cw > max_width) {
                    if (piece.text.length() > 0) { curLine.push_back(piece); piece.text.clear(); piece.width = 0; }
                    //moveでバッファごと渡す(以前はTextRun(1個208B)を行ごとにコピーしていた)
                    lines.push_back(std::move(curLine));
                    curLine.clear();
                    curWidth = 0;
                }
                piece.text.appendUtf8Char(ch, chLen);
                piece.width += chW;
                curWidth += cw;

                CursorSlot slot;
                slot.line = (int)lines.size();
                slot.x = curWidth;
                cursor_slots.push_back(slot);
            }
            if (piece.text.length() > 0) { curLine.push_back(piece); piece.text.clear(); piece.width = 0; }
        }
        lines.push_back(std::move(curLine));

        if (!last_paragraph) {
            CursorSlot slot;
            slot.line = (int)lines.size();
            slot.x = 0;
            cursor_slots.push_back(slot);
        }

        para_start = para_end + 1;
    }

    int maxLineWidth = 0;
    for (auto& line : lines) {
        int lw = 0;
        for (auto& run : line) {
            int rw = run.width; // 上のループで積算済みの幅を再利用(再計測しない)
            if (run.bold) rw += 1;
            lw += rw;
        }
        if (lw > maxLineWidth) maxLineWidth = lw;
    }

    this->l_rect.w = (max_width > 0) ? max_width : maxLineWidth;
    this->l_rect.h = lines.empty() ? 0
            : (int)lines.size() * (line_height + line_spacing) - line_spacing + kDecorationMargin;

    if (this->default_height > 0 && this->l_rect.h < this->default_height) {
        this->l_rect.h = this->default_height;
    }

    if (this->max_height > 0 && this->l_rect.h > this->max_height) {
        this->l_rect.h = this->max_height;
    }

    if (this->cursor_index >= (int)cursor_slots.size()) this->cursor_index = (int)cursor_slots.size() - 1;
    if (this->cursor_index < 0) this->cursor_index = 0;

    computeLineOffsets(this->lines, this->l_rect.w, this->line_offsets);

    relayoutPlaceholder();

    this->fontDefault();
    this->needsRender();
}

template<size_t N>
void Label<N>::relayoutPlaceholder() {
    placeholder_lines.clear();
    placeholder_line_offsets.clear();
    if (placeholder_text.length() == 0) return;

    std::vector<TextRun> runs = parseMarkup(placeholder_text.c_str(), placeholder_text.length());
    std::vector<TextRun> curLine;
    int curWidth = 0;
    TextRun piece;

    for (auto& run : runs) {
        piece.text.clear();
        piece.width = 0;
        piece.bold = run.bold;
        piece.underline = run.underline;
        piece.wavy = run.wavy;
        piece.strikethrough = run.strikethrough;

        const char* rp = run.text.c_str();
        const size_t rn = run.text.length();
        char ch[5];
        int chLen = 0;
        for (size_t ci = 0; ci < rn; ) {
            ci = nextUtf8Char(rp, rn, ci, ch, chLen);

            int chW = OSData::frame->textWidth(ch);
            int cw = chW + (run.bold ? 1 : 0);

            if (max_width > 0 && curWidth > 0 && curWidth + cw > max_width) {
                if (piece.text.length() > 0) { curLine.push_back(piece); piece.text.clear(); piece.width = 0; }
                placeholder_lines.push_back(std::move(curLine));
                curLine.clear();
                curWidth = 0;
            }
            piece.text.appendUtf8Char(ch, chLen);
            piece.width += chW;
            curWidth += cw;
        }
        if (piece.text.length() > 0) { curLine.push_back(piece); piece.text.clear(); piece.width = 0; }
    }
    placeholder_lines.push_back(std::move(curLine));

    computeLineOffsets(this->placeholder_lines, this->l_rect.w, this->placeholder_line_offsets);
}

template<size_t N>
void Label<N>::renderRun(const TextRun& run, int x, int y) {
    if (run.text.length() == 0) return;

    OSData::frame->setCursor(x, y);
    OSData::frame->print(run.text.c_str());

    int w = run.width; // relayout()側で計算済みの幅を再利用(再計測しない)

    if (run.bold) {
        OSData::frame->setCursor(x + 1, y);
        OSData::frame->print(run.text.c_str());
        w += 1;
    }

    if (run.strikethrough) {
        int strikeY = y + line_height / 2;
        OSData::frame->drawFastHLine(x, strikeY, w, this->text_color);
    }

    if (run.underline) {
        int underY = y + line_height;
        OSData::frame->drawFastHLine(x, underY, w, this->text_color);
    }

    if (run.wavy) {
        int waveY = y + line_height;
        const int period = 4;
        const int amp = 1;
        for (int px = 0; px < w; px++) {
            int phase = px % period;
            int dy = 0;
            if (phase == 0) dy = 0;
            else if (phase == 1) dy = amp;
            else if (phase == 2) dy = 0;
            else dy = -amp;
            OSData::frame->drawPixel(x + px, waveY + dy, this->text_color);
        }
    }
}

template<size_t N>
void Label<N>::renderBackground() {
    if (!this->has_background) return;
    const Rect g_rect = getScreenRect();
    OSData::frame->fillRect(g_rect.x, g_rect.y, g_rect.w, g_rect.h, this->background_color);
}

template<size_t N>
void Label<N>::renderBorder() {
    if (this->border_width <= 0) return;
    const Rect g_rect = getScreenRect();
    for (int i = 0; i < this->border_width; i++) {
        OSData::frame->drawRect(
            g_rect.x + i,
            g_rect.y + i,
            g_rect.w - i * 2,
            g_rect.h - i * 2,
            this->border_color
        );
    }
}

template<size_t N>
void Label<N>::renderCursor() {
    if (!this->cursor_visible) {
        this->prev_cursor_rect = {0, 0, 0, 0};
        return;
    }

    int cx = getCursorScreenX();
    int cy = getCursorScreenY();

    Rect cur_rect = {
        (int16_t)cx,
        (int16_t)cy,
        (int16_t)kCursorWidth,
        (int16_t)line_height
    };

    OSData::frame->fillRect(cur_rect.x, cur_rect.y, cur_rect.w, cur_rect.h, this->cursor_color);

    markdirty(cur_rect);
    this->prev_cursor_rect = cur_rect;
}

template<size_t N>
void Label<N>::updateCursorBlink() {
    if (!this->cursor_blink_enabled) return;

    unsigned long now = millis();
    if (now - this->cursor_last_blink_ms >= this->cursor_blink_interval_ms) {
        this->cursor_last_blink_ms = now;
        this->cursor_visible = !this->cursor_visible;
        this->needsRender();
    }
}

// コンストラクタ
// FixedString<M>版はメンバテンプレートのためLabel.hpp内にインライン定義済み。
template<size_t N>
Label<N>::Label(int x, int y, const char* text) {
    this->l_rect.x = x;
    this->l_rect.y = y;
    this->setText(text);
    this->needs_redraw = true;
}

template<size_t N>
Label<N>::Label(const char* text) : Label(0, 0, text) {}

template<size_t N>
void Label<N>::render() {
    if (!this->visible) return;

    //描画は最新のレイアウトを前提にするので、遅延していればここで解決する
    this->ensureLayout();

    this->updateCursorBlink();

    if (!this->needs_redraw) return;

    if (prev_l_rect != l_rect)
        markdirty(getScreenPrevRect());

    this->renderBackground();
    this->renderBorder();

    this->fontApply();

    const Rect g_rect = getScreenRect();
    int cy = g_rect.y;

    bool show_placeholder = this->raw_text.length() == 0 && this->placeholder_text.length() > 0;
    auto& render_lines = show_placeholder ? this->placeholder_lines : this->lines;
    auto& render_offsets = show_placeholder ? this->placeholder_line_offsets : this->line_offsets;

    int8_t saved_text_color = this->text_color;
    if (show_placeholder) this->text_color = this->placeholder_color;
    this->textColorApply();

    size_t line_idx = 0;
    for (auto& line : render_lines) {
        if (this->max_height > 0 && cy >= g_rect.y + g_rect.h) break;

        int offset = (line_idx < render_offsets.size()) ? render_offsets[line_idx] : 0;
        int cx = g_rect.x + offset;
        for (auto& run : line) {
            renderRun(run, cx, cy);
            int rw = run.width; // relayout()側で計算済みの幅を再利用(再計測しない)
            if (run.bold) rw += 1;
            cx += rw;
        }
        cy += line_height + line_spacing;
        line_idx++;
    }

    if (show_placeholder) this->text_color = saved_text_color;
    this->textColorDefault();
    this->fontDefault();

    this->renderCursor();

    markdirty(g_rect);

    this->prev_l_rect.copy(this->l_rect);

    this->needs_redraw = false;
}

template<size_t N>
Label<PICO_STR_LL>& Label<N>::utilityInstance() {
    static Label<PICO_STR_LL> instance(0, 0, "");
    return instance;
}

// FixedString<M>版はメンバテンプレートのためLabel.hpp内にインライン定義済み。
template<size_t N>
void Label<N>::DrawPlain(FontFn::FontSize size, int8_t color, int x, int y, int maxWidth, const char* text) {
    Label<PICO_STR_LL>& helper = utilityInstance();
    helper.f_size = size;
    helper.text_color = color;

    helper.fontApply();
    helper.textColorApply();

    if (maxWidth > 0) {
        OSData::frame->setClipRect(x, y, maxWidth, OSData::frame->fontHeight());
    }
    OSData::frame->setCursor(x, y);
    if (text) OSData::frame->print(text);
    if (maxWidth > 0) {
        OSData::frame->clearClipRect();
    }

    helper.textColorDefault();
    helper.fontDefault();
}

template<size_t N>
int Label<N>::GetLineHeight(FontFn::FontSize size) {
    Label<PICO_STR_LL>& helper = utilityInstance();
    helper.f_size = size;

    helper.fontApply();
    int h = OSData::frame->fontHeight();
    helper.fontDefault();

    return h;
}

// FixedString<M>版はメンバテンプレートのためLabel.hpp内にインライン定義済み。
template<size_t N>
void Label<N>::setText(const char* text) {
    this->raw_text.assign(text);
    invalidateLayout();
}

template<size_t N>
void Label<N>::setPlaceholder(const char* text) {
    this->placeholder_text.assign(text);
    invalidateLayout();
}

template<size_t N>
void Label<N>::setPlaceholderColor(int8_t color) {
    this->placeholder_color = color;
    this->needsRender();
}

template<size_t N>
void Label<N>::setMaxWidth(int width) {
    this->max_width = width;
    invalidateLayout();
}

template<size_t N>
int Label<N>::getMaxWidth() {
    return this->max_width;
}

template<size_t N>
void Label<N>::setMaxHeight(int height) {
    this->max_height = height;
    invalidateLayout();
}

template<size_t N>
int Label<N>::getMaxHeight() {
    return this->max_height;
}

template<size_t N>
void Label<N>::setDefaultHeight(int height) {
    this->default_height = height;
    invalidateLayout();
}

template<size_t N>
int Label<N>::getDefaultHeight() {
    return this->default_height;
}

template<size_t N>
void Label<N>::setLineSpacing(int spacing) {
    this->line_spacing = spacing;
    invalidateLayout();
}

template<size_t N>
void Label<N>::setTextAlign(TextAlign align) {
    this->text_align = align;
    invalidateLayout();
}

template<size_t N>
TextAlign Label<N>::getTextAlign() {
    return this->text_align;
}

template<size_t N>
void Label<N>::setTextColor(int8_t c) {
    this->text_color = c;
    this->needsRender();
}

template<size_t N>
void Label<N>::setBackgroundColor(int8_t palette_color) {
    this->background_color = palette_color;
    this->has_background = true;
    this->needsRender();
}

template<size_t N>
bool Label<N>::hasBackground() {
    return this->has_background;
}

template<size_t N>
void Label<N>::setNoBackground() {
    this->has_background = false;
    this->needsRender();
}

template<size_t N>
void Label<N>::setBorderColor(int8_t palette_color) {
    this->border_color = palette_color;
    this->needsRender();
}

template<size_t N>
void Label<N>::setBorder(int8_t color, int width) {
    this->border_color = color;
    this->border_width = width;
    this->needsRender();
}

template<size_t N>
void Label<N>::setBorderWidth(int width) {
    this->border_width = width;
    this->needsRender();
}

template<size_t N>
int Label<N>::getBorderWidth() {
    return this->border_width;
}

template<size_t N>
void Label<N>::setCursorPos(int index) {
    this->ensureLayout();
    if (cursor_slots.empty()) {
        this->cursor_index = 0;
    } else {
        if (index < 0) index = 0;
        if (index >= (int)cursor_slots.size()) index = (int)cursor_slots.size() - 1;
        this->cursor_index = index;
    }
    this->needsRender();
}

template<size_t N>
int Label<N>::getCursorPos() {
    return this->cursor_index;
}

template<size_t N>
void Label<N>::setCursorMove(int delta) {
    this->setCursorPos(this->cursor_index + delta);
}

template<size_t N>
void Label<N>::setCursorToEnd() {
    this->setCursorPos(this->getTextLength());
}

template<size_t N>
void Label<N>::setCursorVisible(bool visible) {
    this->cursor_visible = visible;
    this->needsRender();
}

template<size_t N>
bool Label<N>::getCursorVisible() {
    return this->cursor_visible;
}

template<size_t N>
void Label<N>::setCursorBlink(bool enabled, unsigned long interval_ms) {
    this->cursor_blink_enabled = enabled;
    this->cursor_blink_interval_ms = interval_ms;
    this->cursor_last_blink_ms = millis();
    this->cursor_visible = enabled;
    this->needsRender();
}

template<size_t N>
bool Label<N>::getCursorBlink() {
    return this->cursor_blink_enabled;
}

template<size_t N>
void Label<N>::setCursorColor(uint16_t c) {
    this->cursor_color = c;
    this->needsRender();
}

template<size_t N>
int Label<N>::getTextLength() {
    this->ensureLayout();
    return cursor_slots.empty() ? 0 : (int)cursor_slots.size() - 1;
}

template<size_t N>
int Label<N>::getCursorScreenX() {
    this->ensureLayout();
    if (cursor_slots.empty()) return this->getScreenRect().x;
    int idx = this->cursor_index;
    if (idx < 0) idx = 0;
    if (idx >= (int)cursor_slots.size()) idx = (int)cursor_slots.size() - 1;
    const CursorSlot& slot = cursor_slots[idx];
    int line_offset = (slot.line >= 0 && slot.line < (int)line_offsets.size()) ? line_offsets[slot.line] : 0;
    return this->getScreenRect().x + line_offset + slot.x;
}

template<size_t N>
int Label<N>::getCursorScreenY() {
    this->ensureLayout();
    if (cursor_slots.empty()) return this->getScreenRect().y;
    int idx = this->cursor_index;
    if (idx < 0) idx = 0;
    if (idx >= (int)cursor_slots.size()) idx = (int)cursor_slots.size() - 1;
    return this->getScreenRect().y + cursor_slots[idx].line * (line_height + line_spacing);
}

template class Label<PICO_STR_S>;
template class Label<PICO_STR_M>;
template class Label<PICO_STR_L>;
template class Label<PICO_STR_LL>;
template class Label<PICO_PATH_LEN>;
template class Label<PICO_STR_256B>;
template class Label<PICO_STR_512B>;
template class Label<PICO_STR_1KiB>;
template class Label<PICO_STR_2KiB>;
template class Label<PICO_STR_4KiB>;