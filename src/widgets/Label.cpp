#include "widgets/Label.hpp"
#include "functions/GFX_Functions.hpp"
#include "OS_Data.hpp"

void Label::needsRender(){
    this->needs_redraw = true;
    PICO_GFX::markDirty(this->getScreenRect());
    // カーソルの前回描画位置も消去対象に含める
    // (本体rect外にカーソルがはみ出すケースの取りこぼし防止)
    PICO_GFX::markDirty(this->prev_cursor_rect);
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
    this->fontApply();

    lines.clear();
    cursor_slots.clear();
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

    // インデックス0（テキスト先頭）は常に1行目の左端
    {
        CursorSlot head;
        head.line = 0;
        head.x = 0;
        cursor_slots.push_back(head);
    }

    for (size_t p = 0; p < paragraphs.size(); p++) {
        std::vector<TextRun> runs = parseMarkup(paragraphs[p]);

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

                // この文字の直後を、カーソルが置ける位置として登録
                // (折返しが起きた直後の文字は、既に更新済みのlines.size()を指すため
                //  自動的に新しい行を指すようになる)
                CursorSlot slot;
                slot.line = (int)lines.size();
                slot.x = curWidth;
                cursor_slots.push_back(slot);
            }
            if (piece.text.length() > 0) { curLine.push_back(piece); piece.text = ""; }
        }
        lines.push_back(curLine);

        // 段落の区切り(\n)自体もカーソルが止まれる位置として登録する。
        // これにより空行("\n\n"など)にもカーソルを置けるようになる。
        if (p + 1 < paragraphs.size()) {
            CursorSlot slot;
            slot.line = (int)lines.size();
            slot.x = 0;
            cursor_slots.push_back(slot);
        }
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

    this->l_rect.w = (max_width > 0) ? max_width : maxLineWidth;
    this->l_rect.h = lines.empty() ? 0
            : (int)lines.size() * (line_height + line_spacing) - line_spacing + kDecorationMargin;

    // デフォルト高さ(下限)より小さければデフォルト高さを採用
    if (this->default_height > 0 && this->l_rect.h < this->default_height) {
        this->l_rect.h = this->default_height;
    }

    // 高さ上限が設定されていれば切り詰める。
    // (実際の描画はwidget単位のクリップ矩形で自動的に切られるため、
    //  ここではrectの高さを縮めるだけでよい)
    if (this->max_height > 0 && this->l_rect.h > this->max_height) {
        this->l_rect.h = this->max_height;
    }

    // テキスト変更でカーソル位置が範囲外になっていたら補正する
    if (this->cursor_index >= (int)cursor_slots.size()) this->cursor_index = (int)cursor_slots.size() - 1;
    if (this->cursor_index < 0) this->cursor_index = 0;

    relayoutPlaceholder(); //プレスホルダー

    this->fontDefault();
    this->needsRender();
}

// ---------- プレースホルダーの折返し計算 ----------
// raw_textとは独立して計算する。markupも通常テキストと同様に解釈される。
void Label::relayoutPlaceholder() {
    placeholder_lines.clear();
    if (placeholder_text.length() == 0) return;

    std::vector<TextRun> runs = parseMarkup(placeholder_text);
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
            if (run.bold) cw += 1;

            if (max_width > 0 && curWidth > 0 && curWidth + cw > max_width) {
                if (piece.text.length() > 0) { curLine.push_back(piece); piece.text = ""; }
                placeholder_lines.push_back(curLine);
                curLine.clear();
                curWidth = 0;
            }
            piece.text += ch;
            curWidth += cw;
        }
        if (piece.text.length() > 0) { curLine.push_back(piece); piece.text = ""; }
    }
    placeholder_lines.push_back(curLine);
}

// ---------- 1つのRunを描画 ----------
void Label::renderRun(const TextRun& run, int x, int y) {
    this->textColorApply();
    OSData::frame->setCursor(x, y);
    OSData::frame->print(run.text);
    this->textColorDefault();
    

    int rw = OSData::frame->textWidth(run.text);

    if (run.bold) {
        // 疑似太字: 1px右にずらして重ね描き
        OSData::frame->setCursor(x + 1, y);
        OSData::frame->print(run.text);
        rw += 1;
    }

    int baseline = y + line_height - 1;

    if (run.underline) {
        OSData::frame->drawFastHLine(x, baseline, rw, this->text_color);
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
            OSData::frame->drawLine(px, py, nx, ny, this->text_color);
            px = nx; py = ny;
            up = !up;
        }
    }
}

// ---------- 背景の描画 ----------
void Label::renderBackground() {
    if (!this->has_background) return; // 無指定時は何も描かない(透明)
    const Rect g_rect = this->getScreenRect();
    OSData::frame->fillRect(g_rect.x, g_rect.y, g_rect.w, g_rect.h, this->background_color);
}

// ---------- ボーダーの描画 ----------
// rect内側にborder_width分だけ塗る(CSSでいうborder-box方式)。
// テキストと重なる場合があるので、太くする場合はMaxWidth等で余白を確保すること。
void Label::renderBorder() {
    if (this->border_width <= 0) return;

    int bw = this->border_width;

    const Rect g_rect = this->getScreenRect();
    int x = g_rect.x;
    int y = g_rect.y;
    int w = g_rect.w;
    int h = g_rect.h;

    // 上辺・下辺
    OSData::frame->fillRect(x, y, w, bw, this->border_color);
    OSData::frame->fillRect(x, y + h - bw, w, bw, this->border_color);
    // 左辺・右辺
    OSData::frame->fillRect(x, y, bw, h, this->border_color);
    OSData::frame->fillRect(x + w - bw, y, bw, h, this->border_color);
}

// ---------- カーソル(挿入位置)の描画 ----------
void Label::renderCursor() {
    if (!this->cursor_visible || cursor_slots.empty()) {
        // 非表示: 前回位置の記録もクリアしておく（次回のmarkDirtyで誤爆しないように）
        this->prev_cursor_rect.x = 0;
        this->prev_cursor_rect.y = 0;
        this->prev_cursor_rect.w = 0;
        this->prev_cursor_rect.h = 0;
        return;
    }

    int idx = this->cursor_index;
    if (idx < 0) idx = 0;
    if (idx >= (int)cursor_slots.size()) idx = (int)cursor_slots.size() - 1;
    const CursorSlot& slot = cursor_slots[idx];

    int cx = this->getScreenRect().x + slot.x;
    int cy = this->getScreenRect().y + slot.line * (line_height + line_spacing);

    OSData::frame->fillRect(cx, cy, this->cursor_width, line_height, this->cursor_color);

    Rect cRect;
    cRect.x = cx;
    cRect.y = cy;
    cRect.w = this->cursor_width;
    cRect.h = line_height;
    PICO_GFX::markDirty(cRect);

    this->prev_cursor_rect.copy(cRect);
}

// ---------- カーソル点滅タイマー ----------
// render()の先頭で毎フレーム呼ばれる想定。needs_redrawの状態に関わらず
// 時間経過をチェックし、必要ならcursor_visibleを切り替えてdirty化する。
void Label::updateCursorBlink() {
    if (!this->visible) return;
    if (!this->cursor_blink_enabled) return;

    unsigned long now = millis();
    if (now - this->cursor_last_blink_ms >= this->cursor_blink_interval_ms) {
        this->cursor_last_blink_ms = now;
        this->cursor_visible = !this->cursor_visible;
        this->needsRender();
    }
}

// ---------- コンストラクタ ----------
Label::Label(int x, int y, String text) {
    this->l_rect.x = x;
    this->l_rect.y = y;
    this->Text(text);
    this->needs_redraw = true;
}

Label::Label(String text) {
    this->Text(text);
    this->needs_redraw = true;
}

// ---------- render ----------
void Label::render() {
    if (!this->visible) return;

    // 点滅タイマーはneeds_redrawに関わらず毎フレームチェックする
    this->updateCursorBlink();

    if (!this->needs_redraw) return;

    // 前回の描画内容を消去
    if(prev_l_rect != l_rect)
        PICO_GFX::markDirty(getScreenPrevRect());

    // 背景・ボーダーはテキストより先に描画する
    this->renderBackground();
    this->renderBorder();

    // 新しく描画
    this->fontApply();

    const Rect g_rect = getScreenRect();
    int cy = g_rect.y;

    // raw_textが空 かつ プレースホルダーが設定されていれば、そちらを描画対象にする
    bool show_placeholder = this->raw_text.length() == 0 && this->placeholder_text.length() > 0;
    auto& render_lines = show_placeholder ? this->placeholder_lines : this->lines;

    // プレースホルダー描画中だけ一時的に色を差し替える(renderRunの実装はそのまま流用)
    int8_t saved_text_color = this->text_color;
    if (show_placeholder) this->text_color = this->placeholder_color;

    for (auto& line : render_lines) {
        if (this->max_height > 0 && cy >= g_rect.y + g_rect.h) break;

        int cx = g_rect.x;
        for (auto& run : line) {
            renderRun(run, cx, cy);
            int rw = OSData::frame->textWidth(run.text);
            if (run.bold) rw += 1;
            cx += rw;
        }
        cy += line_height + line_spacing;
    }

    if (show_placeholder) this->text_color = saved_text_color;
    this->fontDefault();

    // カーソル(挿入位置)の描画
    this->renderCursor();

    PICO_GFX::markDirty(g_rect);

    this->prev_l_rect.copy(this->l_rect);

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

void Label::Placeholder(String text) {
    this->placeholder_text = text;
    relayout();
}

String Label::Placeholder() {
    return this->placeholder_text;
}

void Label::PlaceholderColor(int8_t color) {
    this->placeholder_color = color;
    this->needsRender();
}

void Label::MaxWidth(int width) {
    this->max_width = width;
    relayout();
}

int Label::MaxWidth() {
    return this->max_width;
}

void Label::MaxHeight(int height) {
    this->max_height = height;
    relayout();
}

int Label::MaxHeight() {
    return this->max_height;
}

void Label::DefaultHeight(int height) {
    this->default_height = height;
    relayout();
}

int Label::DefaultHeight() {
    return this->default_height;
}

void Label::LineSpacing(int spacing) {
    this->line_spacing = spacing;
    relayout();
}

void Label::SetTextColor(int8_t c) {
    this->text_color = c;
    this->needsRender();
}

// ---------- 背景・ボーダー関連 ----------
void Label::BackgroundColor(int8_t palette_color) {
    this->background_color = palette_color;
    this->has_background = true;
    this->needsRender();
}

bool Label::HasBackground() {
    return this->has_background;
}

void Label::NoBackground() {
    this->has_background = false;
    this->needsRender();
}

void Label::Border(int8_t color, int width) {
    this->border_color = color;
    this->border_width = width;
    this->needsRender();
}

void Label::BorderWidth(int width) {
    this->border_width = width;
    this->needsRender();
}

int Label::BorderWidth() {
    return this->border_width;
}

// ---------- カーソル(挿入位置)関連 ----------
void Label::CursorPos(int index) {
    if (cursor_slots.empty()) {
        this->cursor_index = 0;
    } else {
        if (index < 0) index = 0;
        if (index >= (int)cursor_slots.size()) index = (int)cursor_slots.size() - 1;
        this->cursor_index = index;
    }
    this->needsRender();
}

int Label::CursorPos() {
    return this->cursor_index;
}

void Label::CursorMove(int delta) {
    this->CursorPos(this->cursor_index + delta);
}

void Label::CursorToEnd() {
    this->CursorPos(this->TextLength());
}

void Label::CursorVisible(bool visible) {
    this->cursor_visible = visible;
    this->needsRender();
}

bool Label::CursorVisible() {
    return this->cursor_visible;
}

void Label::CursorBlink(bool enabled, unsigned long interval_ms) {
    this->cursor_blink_enabled = enabled;
    this->cursor_blink_interval_ms = interval_ms;
    this->cursor_last_blink_ms = millis();

    // 有効化した瞬間は見える状態から開始、無効化時は消しておく
    this->cursor_visible = enabled;

    this->needsRender();
}

bool Label::CursorBlink() {
    return this->cursor_blink_enabled;
}

void Label::CursorColor(uint16_t c) {
    this->cursor_color = c;
    this->needsRender();
}

int Label::TextLength() {
    return cursor_slots.empty() ? 0 : (int)cursor_slots.size() - 1;
}

int Label::CursorScreenX() {
    if (cursor_slots.empty()) return this->getScreenRect().x;
    int idx = this->cursor_index;
    if (idx < 0) idx = 0;
    if (idx >= (int)cursor_slots.size()) idx = (int)cursor_slots.size() - 1;
    return this->getScreenRect().x + cursor_slots[idx].x;
}

int Label::CursorScreenY() {
    if (cursor_slots.empty()) return this->getScreenRect().y;
    int idx = this->cursor_index;
    if (idx < 0) idx = 0;
    if (idx >= (int)cursor_slots.size()) idx = (int)cursor_slots.size() - 1;
    return this->getScreenRect().y + cursor_slots[idx].line * (line_height + line_spacing);
}
