
// -------------------------------------------------------------------
// 実装
// -------------------------------------------------------------------

#include "gui/widgets/Label.hpp"
#include "functions/GFX_Functions.hpp"
#include "OS_Data.hpp"

template<size_t N>
void Label<N>::needsRender() {
    this->needs_redraw = true;
    markdirty(this->getScreenRect());
    markdirty(this->prev_cursor_rect);
}

template<size_t N>
int Label<N>::utf8CharLen(uint8_t lead) {
    if ((lead & 0x80) == 0x00) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 1;
}

template<size_t N>
std::vector<FixedString<5>> Label<N>::splitChars(const char* s) {
    std::vector<FixedString<5>> out;
    if (!s) return out;
    size_t i = 0, n = strlen(s);
    while (i < n) {
        int len = utf8CharLen((uint8_t)s[i]);
        if (i + len > n) len = n - i;
        FixedString<5> c;
        c.appendUtf8Char(&s[i], len);
        out.push_back(c);
        i += len;
    }
    return out;
}

template<size_t N>
std::vector<TextRun> Label<N>::parseMarkup(const char* src) {
    std::vector<TextRun> runs;
    TextRun cur;
    if (!src) return runs;
    size_t i = 0, n = strlen(src);

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
            int len = utf8CharLen((uint8_t)src[i]);
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
            int rw = OSData::frame->textWidth(run.text.c_str());
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
    this->fontApply();

    lines.clear();
    cursor_slots.clear();
    line_height = OSData::frame->fontHeight();

    // \n で段落分割
    std::vector<FixedString<N>> paragraphs;
    const char* s = raw_text.c_str();
    size_t start = 0;
    for (size_t i = 0; ; i++) {
        if (s[i] == '\n' || s[i] == '\0') {
            FixedString<N> buf;
            buf.assign(s + start, i - start);
            paragraphs.push_back(buf);
            if (s[i] == '\0') break;
            start = i + 1;
        }
    }

    {
        CursorSlot head;
        head.line = 0;
        head.x = 0;
        cursor_slots.push_back(head);
    }

    for (size_t p = 0; p < paragraphs.size(); p++) {
        std::vector<TextRun> runs = parseMarkup(paragraphs[p].c_str());

        std::vector<TextRun> curLine;
        int curWidth = 0;
        TextRun piece;

        for (auto& run : runs) {
            piece.text.clear();
            piece.bold = run.bold;
            piece.underline = run.underline;
            piece.wavy = run.wavy;
            piece.strikethrough = run.strikethrough;

            for (auto& ch : splitChars(run.text.c_str())) {
                int cw = OSData::frame->textWidth(ch.c_str());
                if (run.bold) cw += 1;

                if (max_width > 0 && curWidth > 0 && curWidth + cw > max_width) {
                    if (piece.text.length() > 0) { curLine.push_back(piece); piece.text.clear(); }
                    lines.push_back(curLine);
                    curLine.clear();
                    curWidth = 0;
                }
                piece.text.append(ch);
                curWidth += cw;

                CursorSlot slot;
                slot.line = (int)lines.size();
                slot.x = curWidth;
                cursor_slots.push_back(slot);
            }
            if (piece.text.length() > 0) { curLine.push_back(piece); piece.text.clear(); }
        }
        lines.push_back(curLine);

        if (p + 1 < paragraphs.size()) {
            CursorSlot slot;
            slot.line = (int)lines.size();
            slot.x = 0;
            cursor_slots.push_back(slot);
        }
    }

    int maxLineWidth = 0;
    for (auto& line : lines) {
        int lw = 0;
        for (auto& run : line) {
            int rw = OSData::frame->textWidth(run.text.c_str());
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

    std::vector<TextRun> runs = parseMarkup(placeholder_text.c_str());
    std::vector<TextRun> curLine;
    int curWidth = 0;
    TextRun piece;

    for (auto& run : runs) {
        piece.text.clear();
        piece.bold = run.bold;
        piece.underline = run.underline;
        piece.wavy = run.wavy;
        piece.strikethrough = run.strikethrough;

        for (auto& ch : splitChars(run.text.c_str())) {
            int cw = OSData::frame->textWidth(ch.c_str());
            if (run.bold) cw += 1;

            if (max_width > 0 && curWidth > 0 && curWidth + cw > max_width) {
                if (piece.text.length() > 0) { curLine.push_back(piece); piece.text.clear(); }
                placeholder_lines.push_back(curLine);
                curLine.clear();
                curWidth = 0;
            }
            piece.text.append(ch);
            curWidth += cw;
        }
        if (piece.text.length() > 0) { curLine.push_back(piece); piece.text.clear(); }
    }
    placeholder_lines.push_back(curLine);

    computeLineOffsets(this->placeholder_lines, this->l_rect.w, this->placeholder_line_offsets);
}

template<size_t N>
void Label<N>::renderRun(const TextRun& run, int x, int y) {
    if (run.text.length() == 0) return;

    OSData::frame->setCursor(x, y);
    OSData::frame->print(run.text.c_str());

    int w = OSData::frame->textWidth(run.text.c_str());

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
template<size_t N>
template<size_t M>
Label<N>::Label(int x, int y, const FixedString<M>& text) {
    this->l_rect.x = x;
    this->l_rect.y = y;
    this->setText(text);
    this->needs_redraw = true;
}

template<size_t N>
Label<N>::Label(int x, int y, const char* text) {
    this->l_rect.x = x;
    this->l_rect.y = y;
    this->setText(text);
    this->needs_redraw = true;
}

template<size_t N>
template<size_t M>
Label<N>::Label(const FixedString<M>& text) : Label(0, 0, text) {}

template<size_t N>
Label<N>::Label(const char* text) : Label(0, 0, text) {}

template<size_t N>
void Label<N>::render() {
    if (!this->visible) return;

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

    size_t line_idx = 0;
    for (auto& line : render_lines) {
        if (this->max_height > 0 && cy >= g_rect.y + g_rect.h) break;

        int offset = (line_idx < render_offsets.size()) ? render_offsets[line_idx] : 0;
        int cx = g_rect.x + offset;
        for (auto& run : line) {
            renderRun(run, cx, cy);
            int rw = OSData::frame->textWidth(run.text.c_str());
            if (run.bold) rw += 1;
            cx += rw;
        }
        cy += line_height + line_spacing;
        line_idx++;
    }

    if (show_placeholder) this->text_color = saved_text_color;
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

template<size_t N>
template<size_t M>
void Label<N>::DrawPlain(FontFn::FontSize size, int8_t color, int x, int y, int maxWidth, const FixedString<M>& text) {
    DrawPlain(size, color, x, y, maxWidth, text.c_str());
}

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

template<size_t N>
template<size_t M>
void Label<N>::setText(const FixedString<M>& text) {
    this->raw_text.assign(text);
    relayout();
}

template<size_t N>
void Label<N>::setText(const char* text) {
    this->raw_text.assign(text);
    relayout();
}

template<size_t N>
template<size_t M>
void Label<N>::setPlaceholder(const FixedString<M>& text) {
    this->placeholder_text.assign(text);
    relayout();
}

template<size_t N>
void Label<N>::setPlaceholder(const char* text) {
    this->placeholder_text.assign(text);
    relayout();
}

template<size_t N>
void Label<N>::setPlaceholderColor(int8_t color) {
    this->placeholder_color = color;
    this->needsRender();
}

template<size_t N>
void Label<N>::setMaxWidth(int width) {
    this->max_width = width;
    relayout();
}

template<size_t N>
int Label<N>::getMaxWidth() {
    return this->max_width;
}

template<size_t N>
void Label<N>::setMaxHeight(int height) {
    this->max_height = height;
    relayout();
}

template<size_t N>
int Label<N>::getMaxHeight() {
    return this->max_height;
}

template<size_t N>
void Label<N>::setDefaultHeight(int height) {
    this->default_height = height;
    relayout();
}

template<size_t N>
int Label<N>::getDefaultHeight() {
    return this->default_height;
}

template<size_t N>
void Label<N>::setLineSpacing(int spacing) {
    this->line_spacing = spacing;
    relayout();
}

template<size_t N>
void Label<N>::setTextAlign(TextAlign align) {
    this->text_align = align;
    relayout();
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
    return cursor_slots.empty() ? 0 : (int)cursor_slots.size() - 1;
}

template<size_t N>
int Label<N>::getCursorScreenX() {
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