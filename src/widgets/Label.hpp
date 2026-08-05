#pragma once

#include <vector>
#include "widgets/Widget.hpp"
#include "functions/GFX_Functions.hpp"
#include "OS_Data.hpp"
#include "consts.hpp"

// 1つの書式区間（同じ太字/下線/波線設定を持つ文字列断片）
struct TextRun {
    String text;
    bool bold = false;
    bool underline = false;
    bool wavy = false;
};

class Label : public Widget {
    private:
        String raw_text;                          // マークアップ込みの元テキスト
        std::vector<std::vector<TextRun>> lines;   // 解析・折返し後の行データ
        int max_width = 0;                         // 0 = 折り返し無効（\nのみ改行）
        int line_height = 0;
        int line_spacing = 2;                      // 行間(px)
        uint16_t color = TFT_BLACK;                // 文字色（下線・波線にも使用）

        // 波線はベースラインの上下に振れるため、通常のline_heightより
        // 最大2px下にはみ出す。消し残りを防ぐため、バウンディングボックスの
        // 高さに常にこのマージンを加算しておく。
        static constexpr int kDecorationMargin = 2;

        // ---------- UTF-8 ----------
        static int utf8CharLen(uint8_t lead) {
            if ((lead & 0x80) == 0x00) return 1;
            if ((lead & 0xE0) == 0xC0) return 2;
            if ((lead & 0xF0) == 0xE0) return 3;
            if ((lead & 0xF8) == 0xF0) return 4;
            return 1; // 不正バイト列へのフォールバック
        }

        static std::vector<String> splitChars(const String& s) {
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
        std::vector<TextRun> parseMarkup(const String& src) {
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
        void relayout() {
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

            this->w = (max_width > 0) ? max_width : maxLineWidth;
            this->h = lines.empty() ? 0
                    : (int)lines.size() * (line_height + line_spacing) - line_spacing + kDecorationMargin;
            this->needs_redraw = true;
        }

        // ---------- 1つのRunを描画 ----------
        void renderRun(const TextRun& run, int x, int y) {
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
                for (int dx = step; dx <= rw; dx += step) {
                    int nx = x + dx;
                    int ny = wy + (up ? -amp : amp);
                    OSData::frame->drawLine(px, py, nx, ny, this->color);
                    px = nx; py = ny;
                    up = !up;
                }
            }
        }

    public:
        Label(int x, int y, String text) {
            this->x = x;
            this->y = y;
            this->Text(text);
        }

        Label(String text) {
            this->Text(text);
        }

        void render() override {
            if (!this->needs_redraw) return;

            // 前回の描画内容を消去
            OSData::frame->fillRect(this->prev_x, this->prev_y, this->prev_w, this->prev_h, PICO_BACKGROUND);
            PICO_GFX::markDirty(this->prev_x, this->prev_y, this->prev_w, this->prev_h);

            // print()側の自動折り返しを無効化
            // （こちらで既に折り返し済みの文字列を渡しているため、
            //   ライブラリ側で二重に折り返されると行が崩れる）
            OSData::frame->setTextWrap(false, false);

            // 新しく描画
            int cy = this->y;
            for (auto& line : lines) {
                int cx = this->x;
                for (auto& run : line) {
                    renderRun(run, cx, cy);
                    int rw = OSData::frame->textWidth(run.text);
                    if (run.bold) rw += 1;
                    cx += rw;
                }
                cy += line_height + line_spacing;
            }
            PICO_GFX::markDirty(this->x, this->y, this->w, this->h);

            this->prev_x = this->x; this->prev_y = this->y;
            this->prev_w = this->w; this->prev_h = this->h;

            this->needs_redraw = false;
        }

        // ---------- setter / getter ----------

        // マークアップ対応テキストを設定
        // 記法: **太字** / _下線_ / ~波線~ / \n で改行
        void Text(String text) {
            this->raw_text = text;
            relayout();
        }
        String Text() { return this->raw_text; }

        // 折り返し幅を設定（0で無効、\nのみで改行）
        void MaxWidth(int width) {
            this->max_width = width;
            relayout();
        }
        int MaxWidth() { return this->max_width; }

        void LineSpacing(int spacing) {
            this->line_spacing = spacing;
            relayout();
        }

        void Color(uint16_t c) {
            this->color = c;
            this->needs_redraw = true;
        }

        void X(int x) { this->x = x; this->needs_redraw = true; }
        int X() { return this->x; }

        void Y(int y) { this->y = y; this->needs_redraw = true; }
        int Y() { return this->y; }

        int W() { return this->w; }
        int H() { return this->h; }
};
