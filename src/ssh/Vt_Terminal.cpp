#include "ssh/Vt_Terminal.hpp"

#include <cstdio>
#include <cstring>

namespace {
    // ANSIの16色のRGB(xtermの既定値)。256色/RGBの指定を16色へ丸めるときの比較相手
    constexpr uint8_t kAnsiRgb[16][3] = {
        {  0,   0,   0}, {128,   0,   0}, {  0, 128,   0}, {128, 128,   0},
        {  0,   0, 128}, {128,   0, 128}, {  0, 128, 128}, {192, 192, 192},
        {128, 128, 128}, {255,   0,   0}, {  0, 255,   0}, {255, 255,   0},
        {  0,   0, 255}, {255,   0, 255}, {  0, 255, 255}, {255, 255, 255},
    };

    uint8_t NearestAnsi(int r, int g, int b){
        int best = 0;
        long best_d = 0x7FFFFFFF;
        for(int i = 0; i < 16; i++){
            const long dr = r - kAnsiRgb[i][0];
            const long dg = g - kAnsiRgb[i][1];
            const long db = b - kAnsiRgb[i][2];
            const long d = dr * dr + dg * dg + db * db;
            if(d < best_d){
                best_d = d;
                best = i;
            }
        }
        return (uint8_t)best;
    }

    uint8_t Map256(int n){
        if(n < 0) return VtTerminal::kDefaultFg;
        if(n < 16) return (uint8_t)n;
        if(n < 232){
            static const int levels[6] = { 0, 95, 135, 175, 215, 255 };
            n -= 16;
            return NearestAnsi(levels[n / 36], levels[(n / 6) % 6], levels[n % 6]);
        }
        if(n < 256){
            const int v = 8 + (n - 232) * 10;
            return NearestAnsi(v, v, v);
        }
        return VtTerminal::kDefaultFg;
    }

    // DECの罫線(ESC ( 0)の 0x60〜0x7E
    const uint16_t kDecGraphics[31] = {
        0x25C6, 0x2592, 0x2409, 0x240C, 0x240D, 0x240A, 0x00B0, 0x00B1,
        0x2424, 0x240B, 0x2518, 0x2510, 0x250C, 0x2514, 0x253C, 0x23BA,
        0x23BB, 0x2500, 0x23BC, 0x23BD, 0x251C, 0x2524, 0x2534, 0x252C,
        0x2502, 0x2264, 0x2265, 0x03C0, 0x2260, 0x00A3, 0x00B7,
    };

    struct Range { uint32_t lo, hi; };

    bool InRanges(uint32_t cp, const Range* r, int n){
        for(int i = 0; i < n; i++){
            if(cp < r[i].lo) return false; // 昇順に並べてある
            if(cp <= r[i].hi) return true;
        }
        return false;
    }
}

int VtTerminal::CharWidth(uint32_t cp){
    static const Range zero[] = {
        {0x0300, 0x036F}, {0x0483, 0x0489}, {0x0591, 0x05BD}, {0x0610, 0x061A},
        {0x064B, 0x065F}, {0x1AB0, 0x1AFF}, {0x1DC0, 0x1DFF}, {0x200B, 0x200F},
        {0x202A, 0x202E}, {0x2060, 0x2064}, {0x20D0, 0x20FF}, {0x3099, 0x309A},
        {0xFE00, 0xFE0F}, {0xFE20, 0xFE2F}, {0xFEFF, 0xFEFF}, {0xE0100, 0xE01EF},
    };
    static const Range wide[] = {
        {0x1100, 0x115F}, {0x231A, 0x231B}, {0x2329, 0x232A}, {0x23E9, 0x23EC},
        {0x2E80, 0x303E}, {0x3041, 0x33FF}, {0x3400, 0x4DBF}, {0x4E00, 0x9FFF},
        {0xA000, 0xA4CF}, {0xA960, 0xA97F}, {0xAC00, 0xD7A3}, {0xF900, 0xFAFF},
        {0xFE10, 0xFE19}, {0xFE30, 0xFE6F}, {0xFF00, 0xFF60}, {0xFFE0, 0xFFE6},
        {0x1F300, 0x1F64F}, {0x1F900, 0x1F9FF}, {0x20000, 0x3FFFD},
    };
    if(cp < 0x20 || (cp >= 0x7F && cp < 0xA0)) return 0;
    if(cp < 0x300) return 1;
    if(InRanges(cp, zero, sizeof(zero) / sizeof(zero[0]))) return 0;
    if(InRanges(cp, wide, sizeof(wide) / sizeof(wide[0]))) return 2;
    return 1;
}

VtTerminal::VtTerminal(){
    this->reset();
}

void VtTerminal::reset(){
    this->cur_color_ = (kDefaultBg << 4) | kDefaultFg;
    this->cur_attr_ = 0;
    const Cell b = this->blank();
    for(int i = 0; i < kMaxRows * kMaxCols; i++){
        this->main_[i] = b;
        this->alt_[i] = b;
    }
    this->sb_head_ = 0;
    this->sb_count_ = 0;
    this->cx_ = this->cy_ = 0;
    this->wrap_pending_ = false;
    this->top_ = 0;
    this->bottom_ = this->rows_ - 1;
    this->origin_ = false;
    this->autowrap_ = true;
    this->insert_ = false;
    this->cursor_visible_ = true;
    this->app_cursor_ = false;
    this->alt_active_ = false;
    this->g_graphics_[0] = this->g_graphics_[1] = false;
    this->gl_ = 0;
    this->saved_main_ = Saved();
    this->saved_alt_ = Saved();
    this->ps_ = ParseState::Ground;
    this->utf8_left_ = 0;
    this->reply_len_ = 0;
    this->markAll();
}

void VtTerminal::resize(int cols, int rows){
    if(cols < 2) cols = 2;
    if(cols > kMaxCols) cols = kMaxCols;
    if(rows < 2) rows = 2;
    if(rows > kMaxRows) rows = kMaxRows;
    if(cols == this->cols_ && rows == this->rows_) return;

    //縮めてカーソルがはみ出すなら、その分だけ上を流す(通常画面ならスクロールバックへ)
    if(rows < this->rows_ && this->cy_ >= rows){
        const int d = this->cy_ - rows + 1;
        this->scrollUp(0, this->rows_ - 1, d, !this->alt_active_);
        this->cy_ -= d;
    }
    //広げた列に古い内容が残らないようにする
    if(cols > this->cols_){
        const Cell b{ 0, (kDefaultBg << 4) | kDefaultFg, 0 };
        for(int y = 0; y < kMaxRows; y++){
            for(int x = this->cols_; x < cols; x++){
                this->main_[y * kMaxCols + x] = b;
                this->alt_[y * kMaxCols + x] = b;
            }
        }
    }
    //広げた行は空白で始める
    if(rows > this->rows_){
        const Cell b{ 0, (kDefaultBg << 4) | kDefaultFg, 0 };
        for(int y = this->rows_; y < rows; y++){
            for(int x = 0; x < kMaxCols; x++){
                this->main_[y * kMaxCols + x] = b;
                this->alt_[y * kMaxCols + x] = b;
            }
        }
    }

    this->cols_ = cols;
    this->rows_ = rows;
    if(this->cx_ >= cols) this->cx_ = cols - 1;
    if(this->cy_ >= rows) this->cy_ = rows - 1;
    this->top_ = 0;
    this->bottom_ = rows - 1;
    this->wrap_pending_ = false;
    this->markAll();
}

const VtTerminal::Cell* VtTerminal::row(int y) const {
    if(y < 0 || y >= this->rows_) return nullptr;
    return this->screen() + y * kMaxCols;
}

const VtTerminal::Cell* VtTerminal::scrollbackRow(int i) const {
    if(i < 0 || i >= this->sb_count_) return nullptr;
    const int idx = (this->sb_head_ - 1 - i + kScrollbackLines) % kScrollbackLines;
    return this->sb_ + idx * kMaxCols;
}

void VtTerminal::pushScrollback(const Cell* line){
    memcpy(this->sb_ + this->sb_head_ * kMaxCols, line, sizeof(Cell) * kMaxCols);
    this->sb_head_ = (this->sb_head_ + 1) % kScrollbackLines;
    if(this->sb_count_ < kScrollbackLines) this->sb_count_++;
    this->scrolled_++;
}

void VtTerminal::scrollUp(int top, int bottom, int n, bool to_scrollback){
    if(top > bottom) return;
    const int span = bottom - top + 1;
    if(n > span) n = span;
    if(n <= 0) return;
    Cell* s = this->screen();
    if(to_scrollback && top == 0){
        for(int i = 0; i < n; i++) this->pushScrollback(s + i * kMaxCols);
    }
    memmove(s + top * kMaxCols, s + (top + n) * kMaxCols, sizeof(Cell) * kMaxCols * (span - n));
    const Cell b = this->blank();
    for(int y = bottom - n + 1; y <= bottom; y++){
        for(int x = 0; x < kMaxCols; x++) s[y * kMaxCols + x] = b;
    }
    for(int y = top; y <= bottom; y++) this->markRow(y);
}

void VtTerminal::scrollDown(int top, int bottom, int n){
    if(top > bottom) return;
    const int span = bottom - top + 1;
    if(n > span) n = span;
    if(n <= 0) return;
    Cell* s = this->screen();
    memmove(s + (top + n) * kMaxCols, s + top * kMaxCols, sizeof(Cell) * kMaxCols * (span - n));
    const Cell b = this->blank();
    for(int y = top; y < top + n; y++){
        for(int x = 0; x < kMaxCols; x++) s[y * kMaxCols + x] = b;
    }
    for(int y = top; y <= bottom; y++) this->markRow(y);
}

void VtTerminal::fixWideAt(int x, int y){
    if(x < 0 || x >= this->cols_) return;
    Cell* c = this->at(x, y);
    const Cell b = this->blank();
    if(c->ch == kWideRight && x > 0){
        *this->at(x - 1, y) = b;
        *c = b;
    }else if((c->attr & AttrWide) && x + 1 < this->cols_){
        *this->at(x + 1, y) = b;
        *c = b;
    }
}

void VtTerminal::clearCells(int y, int x0, int x1){
    if(x0 < 0) x0 = 0;
    if(x1 > this->cols_) x1 = this->cols_;
    if(x0 >= x1) return;
    this->fixWideAt(x0, y);
    this->fixWideAt(x1 - 1, y);
    const Cell b = this->blank();
    for(int x = x0; x < x1; x++) *this->at(x, y) = b;
    this->markRow(y);
}

void VtTerminal::moveTo(int x, int y){
    if(x < 0) x = 0;
    if(x >= this->cols_) x = this->cols_ - 1;
    if(y < 0) y = 0;
    if(y >= this->rows_) y = this->rows_ - 1;
    this->cx_ = x;
    this->cy_ = y;
    this->wrap_pending_ = false;
}

void VtTerminal::newline(){
    if(this->cy_ == this->bottom_){
        this->scrollUp(this->top_, this->bottom_, 1, !this->alt_active_);
    }else if(this->cy_ < this->rows_ - 1){
        this->cy_++;
    }
}

void VtTerminal::putChar(uint32_t cp){
    if(this->g_graphics_[this->gl_] && cp >= 0x60 && cp <= 0x7E) cp = kDecGraphics[cp - 0x60];
    const int w = CharWidth(cp);
    if(w == 0) return;
    if(cp > 0xFFFE) cp = 0xFFFD;

    if(this->wrap_pending_){
        this->wrap_pending_ = false;
        if(this->autowrap_){
            this->cx_ = 0;
            this->newline();
        }
    }
    if(w == 2 && this->cx_ >= this->cols_ - 1){
        if(!this->autowrap_) return;
        this->clearCells(this->cy_, this->cx_, this->cols_);
        this->cx_ = 0;
        this->newline();
    }

    Cell* row = this->at(0, this->cy_);
    if(this->insert_){
        this->fixWideAt(this->cols_ - w, this->cy_);
        memmove(row + this->cx_ + w, row + this->cx_, sizeof(Cell) * (this->cols_ - this->cx_ - w));
    }
    this->fixWideAt(this->cx_, this->cy_);
    if(w == 2) this->fixWideAt(this->cx_ + 1, this->cy_);

    row[this->cx_] = Cell{ (uint16_t)cp, this->cur_color_, (uint8_t)(this->cur_attr_ | (w == 2 ? AttrWide : 0)) };
    if(w == 2) row[this->cx_ + 1] = Cell{ kWideRight, this->cur_color_, this->cur_attr_ };
    this->last_char_ = cp;
    this->markRow(this->cy_);

    if(this->cx_ + w >= this->cols_){
        this->cx_ = this->cols_ - 1;
        this->wrap_pending_ = this->autowrap_;
    }else{
        this->cx_ += w;
    }
}

void VtTerminal::addReply(const char* s){
    const size_t n = strlen(s);
    if(this->reply_len_ + n > sizeof(this->reply_)) return;
    memcpy(this->reply_ + this->reply_len_, s, n);
    this->reply_len_ += n;
}

void VtTerminal::saveCursor(){
    Saved& s = this->alt_active_ ? this->saved_alt_ : this->saved_main_;
    s.x = this->cx_;
    s.y = this->cy_;
    s.color = this->cur_color_;
    s.attr = this->cur_attr_;
    s.origin = this->origin_;
    s.g_graphics[0] = this->g_graphics_[0];
    s.g_graphics[1] = this->g_graphics_[1];
    s.gl = this->gl_;
}

void VtTerminal::restoreCursor(){
    const Saved& s = this->alt_active_ ? this->saved_alt_ : this->saved_main_;
    this->cur_color_ = s.color;
    this->cur_attr_ = s.attr;
    this->origin_ = s.origin;
    this->g_graphics_[0] = s.g_graphics[0];
    this->g_graphics_[1] = s.g_graphics[1];
    this->gl_ = s.gl;
    this->moveTo(s.x, s.y);
}

void VtTerminal::enterAlt(bool on, bool clear){
    if(on == this->alt_active_) return;
    this->alt_active_ = on;
    if(on && clear){
        const Cell b{ 0, (kDefaultBg << 4) | kDefaultFg, 0 };
        for(int i = 0; i < kMaxRows * kMaxCols; i++) this->alt_[i] = b;
    }
    this->markAll();
}

void VtTerminal::setMode(bool on){
    for(int i = 0; i < this->param_count_; i++){
        const int m = this->params_[i];
        if(this->private_ == '?'){
            switch(m){
                case 1: this->app_cursor_ = on; break;
                case 6:
                    this->origin_ = on;
                    this->moveTo(0, on ? this->top_ : 0);
                    break;
                case 7: this->autowrap_ = on; break;
                case 25:
                    this->cursor_visible_ = on;
                    this->markRow(this->cy_);
                    break;
                case 47:
                case 1047:
                    this->enterAlt(on, on);
                    break;
                case 1048:
                    if(on) this->saveCursor(); else this->restoreCursor();
                    break;
                case 1049:
                    if(on){
                        this->saveCursor();
                        this->enterAlt(true, true);
                    }else if(this->alt_active_){ //代替画面に居ないときは何もしない(カーソルを飛ばさない)
                        this->enterAlt(false, false);
                        this->restoreCursor();
                    }
                    break;
                default: break; // マウス・括弧付き貼り付け等は無視
            }
        }else if(this->private_ == 0){
            if(m == 4) this->insert_ = on;
        }
    }
}

void VtTerminal::sgr(){
    if(this->param_count_ == 0){
        this->cur_attr_ = 0;
        this->cur_color_ = (kDefaultBg << 4) | kDefaultFg;
        return;
    }
    uint8_t fg = this->cur_color_ & 0x0F;
    uint8_t bg = this->cur_color_ >> 4;
    for(int i = 0; i < this->param_count_; i++){
        const int p = this->params_[i];
        if(p == 0){
            this->cur_attr_ = 0;
            fg = kDefaultFg;
            bg = kDefaultBg;
        }else if(p == 1){
            this->cur_attr_ |= AttrBold;
        }else if(p == 22){
            this->cur_attr_ &= (uint8_t)~AttrBold;
        }else if(p == 4){
            this->cur_attr_ |= AttrUnderline;
        }else if(p == 24){
            this->cur_attr_ &= (uint8_t)~AttrUnderline;
        }else if(p == 7){
            this->cur_attr_ |= AttrReverse;
        }else if(p == 27){
            this->cur_attr_ &= (uint8_t)~AttrReverse;
        }else if(p >= 30 && p <= 37){
            fg = (uint8_t)(p - 30);
        }else if(p == 39){
            fg = kDefaultFg;
        }else if(p >= 40 && p <= 47){
            bg = (uint8_t)(p - 40);
        }else if(p == 49){
            bg = kDefaultBg;
        }else if(p >= 90 && p <= 97){
            fg = (uint8_t)(p - 90 + 8);
        }else if(p >= 100 && p <= 107){
            bg = (uint8_t)(p - 100 + 8);
        }else if(p == 38 || p == 48){
            uint8_t c = 0;
            bool ok = false;
            if(i + 2 < this->param_count_ && this->params_[i + 1] == 5){
                c = Map256(this->params_[i + 2]);
                i += 2;
                ok = true;
            }else if(i + 4 < this->param_count_ && this->params_[i + 1] == 2){
                c = NearestAnsi(this->params_[i + 2], this->params_[i + 3], this->params_[i + 4]);
                i += 4;
                ok = true;
            }else{
                break; //形が分からないので残りは読まない
            }
            if(ok){
                if(p == 38) fg = c; else bg = c;
            }
        }
    }
    this->cur_color_ = (uint8_t)((bg << 4) | fg);
}

void VtTerminal::escDispatch(uint8_t c){
    switch(c){
        case '7': this->saveCursor(); break;
        case '8': this->restoreCursor(); break;
        case 'D': this->wrap_pending_ = false; this->newline(); break;
        case 'E': this->cx_ = 0; this->wrap_pending_ = false; this->newline(); break;
        case 'M':
            this->wrap_pending_ = false;
            if(this->cy_ == this->top_) this->scrollDown(this->top_, this->bottom_, 1);
            else if(this->cy_ > 0) this->cy_--;
            break;
        case 'c': this->reset(); break;
        default: break; // = > (キーパッド)等は無視
    }
}

void VtTerminal::csiDispatch(uint8_t c){
    const int n = this->param(0, 1);
    const int raw0 = this->param_count_ > 0 ? this->params_[0] : 0;

    if(this->intermediate_ == '!' && c == 'p'){ //DECSTR(軟リセット)
        this->cur_attr_ = 0;
        this->cur_color_ = (kDefaultBg << 4) | kDefaultFg;
        this->top_ = 0;
        this->bottom_ = this->rows_ - 1;
        this->origin_ = false;
        this->autowrap_ = true;
        this->insert_ = false;
        this->app_cursor_ = false;
        this->cursor_visible_ = true;
        return;
    }
    if(this->intermediate_ != 0) return; // DECSCUSR等は無視

    if(this->private_ != 0 && c != 'h' && c != 'l' && c != 'c' && c != 'n') return;

    switch(c){
        case '@': {
            Cell* row = this->at(0, this->cy_);
            int k = n;
            if(k > this->cols_ - this->cx_) k = this->cols_ - this->cx_;
            this->fixWideAt(this->cx_, this->cy_);
            this->fixWideAt(this->cols_ - k, this->cy_);
            memmove(row + this->cx_ + k, row + this->cx_, sizeof(Cell) * (this->cols_ - this->cx_ - k));
            const Cell b = this->blank();
            for(int x = this->cx_; x < this->cx_ + k; x++) row[x] = b;
            this->markRow(this->cy_);
            this->wrap_pending_ = false;
            break;
        }
        case 'A': {
            const int lim = this->cy_ >= this->top_ ? this->top_ : 0;
            int y = this->cy_ - n;
            if(y < lim) y = lim;
            this->moveTo(this->cx_, y);
            break;
        }
        case 'B':
        case 'e': {
            const int lim = this->cy_ <= this->bottom_ ? this->bottom_ : this->rows_ - 1;
            int y = this->cy_ + n;
            if(y > lim) y = lim;
            this->moveTo(this->cx_, y);
            break;
        }
        case 'C':
        case 'a': this->moveTo(this->cx_ + n, this->cy_); break;
        case 'D': this->moveTo(this->cx_ - n, this->cy_); break;
        case 'E': {
            int y = this->cy_ + n;
            if(y > this->bottom_ && this->cy_ <= this->bottom_) y = this->bottom_;
            this->moveTo(0, y);
            break;
        }
        case 'F': {
            int y = this->cy_ - n;
            if(y < this->top_ && this->cy_ >= this->top_) y = this->top_;
            this->moveTo(0, y);
            break;
        }
        case 'G':
        case '`': this->moveTo(n - 1, this->cy_); break;
        case 'H':
        case 'f': {
            int y = this->param(0, 1) - 1;
            const int x = this->param(1, 1) - 1;
            if(this->origin_){
                y += this->top_;
                if(y > this->bottom_) y = this->bottom_;
            }
            this->moveTo(x, y);
            break;
        }
        case 'd': {
            int y = n - 1;
            if(this->origin_){
                y += this->top_;
                if(y > this->bottom_) y = this->bottom_;
            }
            this->moveTo(this->cx_, y);
            break;
        }
        case 'I':
            for(int i = 0; i < n; i++){
                int x = (this->cx_ / 8 + 1) * 8;
                this->moveTo(x, this->cy_);
            }
            break;
        case 'Z':
            for(int i = 0; i < n; i++){
                int x = this->cx_ > 0 ? ((this->cx_ - 1) / 8) * 8 : 0;
                this->moveTo(x, this->cy_);
            }
            break;
        case 'J':
            if(raw0 == 0){
                this->clearCells(this->cy_, this->cx_, this->cols_);
                for(int y = this->cy_ + 1; y < this->rows_; y++) this->clearCells(y, 0, this->cols_);
            }else if(raw0 == 1){
                for(int y = 0; y < this->cy_; y++) this->clearCells(y, 0, this->cols_);
                this->clearCells(this->cy_, 0, this->cx_ + 1);
            }else if(raw0 == 2){
                for(int y = 0; y < this->rows_; y++) this->clearCells(y, 0, this->cols_);
            }else if(raw0 == 3){
                this->sb_count_ = 0;
                this->markAll();
            }
            break;
        case 'K':
            if(raw0 == 0) this->clearCells(this->cy_, this->cx_, this->cols_);
            else if(raw0 == 1) this->clearCells(this->cy_, 0, this->cx_ + 1);
            else if(raw0 == 2) this->clearCells(this->cy_, 0, this->cols_);
            break;
        case 'L':
            if(this->cy_ >= this->top_ && this->cy_ <= this->bottom_){
                this->scrollDown(this->cy_, this->bottom_, n);
                this->moveTo(0, this->cy_);
            }
            break;
        case 'M':
            if(this->cy_ >= this->top_ && this->cy_ <= this->bottom_){
                this->scrollUp(this->cy_, this->bottom_, n, false);
                this->moveTo(0, this->cy_);
            }
            break;
        case 'P': {
            Cell* row = this->at(0, this->cy_);
            int k = n;
            if(k > this->cols_ - this->cx_) k = this->cols_ - this->cx_;
            this->fixWideAt(this->cx_, this->cy_);
            this->fixWideAt(this->cx_ + k - 1, this->cy_);
            memmove(row + this->cx_, row + this->cx_ + k, sizeof(Cell) * (this->cols_ - this->cx_ - k));
            const Cell b = this->blank();
            for(int x = this->cols_ - k; x < this->cols_; x++) row[x] = b;
            this->markRow(this->cy_);
            this->wrap_pending_ = false;
            break;
        }
        case 'S': this->scrollUp(this->top_, this->bottom_, n, false); break;
        case 'T': this->scrollDown(this->top_, this->bottom_, n); break;
        case 'X': this->clearCells(this->cy_, this->cx_, this->cx_ + n); break;
        case 'b': {
            int k = n > 255 ? 255 : n;
            for(int i = 0; i < k; i++) this->putChar(this->last_char_);
            break;
        }
        case 'c':
            if(this->private_ == '>') this->addReply("\x1b[>0;276;0c");
            else if(this->private_ == 0 && raw0 == 0) this->addReply("\x1b[?1;2c");
            break;
        case 'h': this->setMode(true); break;
        case 'l': this->setMode(false); break;
        case 'm': this->sgr(); break;
        case 'n':
            if(this->private_ != 0) break;
            if(raw0 == 5){
                this->addReply("\x1b[0n");
            }else if(raw0 == 6){
                char buf[32];
                const int y = this->origin_ ? this->cy_ - this->top_ : this->cy_;
                snprintf(buf, sizeof(buf), "\x1b[%d;%dR", y + 1, this->cx_ + 1);
                this->addReply(buf);
            }
            break;
        case 'r': {
            const int t = this->param(0, 1) - 1;
            int b = this->param(1, this->rows_) - 1;
            if(b >= this->rows_) b = this->rows_ - 1;
            if(t < b){
                this->top_ = t;
                this->bottom_ = b;
                this->moveTo(0, this->origin_ ? t : 0);
            }
            break;
        }
        case 's': this->saveCursor(); break;
        case 'u': this->restoreCursor(); break;
        default: break;
    }
}

void VtTerminal::feed(uint8_t b){
    //制御文字はどの状態でも(文字列の中以外は)その場で効く
    if(b < 0x20 && this->ps_ != ParseState::Osc && this->ps_ != ParseState::Str){
        if(b == 0x1B){
            this->ps_ = ParseState::Escape;
            this->utf8_left_ = 0;
            return;
        }
        if(b == 0x18 || b == 0x1A){
            this->ps_ = ParseState::Ground;
            return;
        }
        this->utf8_left_ = 0;
        switch(b){
            case 0x08:
                if(this->cx_ > 0) this->cx_--;
                this->wrap_pending_ = false;
                break;
            case 0x09: {
                int x = (this->cx_ / 8 + 1) * 8;
                if(x >= this->cols_) x = this->cols_ - 1;
                this->cx_ = x;
                this->wrap_pending_ = false;
                break;
            }
            case 0x0A: case 0x0B: case 0x0C:
                this->wrap_pending_ = false;
                this->newline();
                break;
            case 0x0D:
                this->cx_ = 0;
                this->wrap_pending_ = false;
                break;
            case 0x0E: this->gl_ = 1; break;
            case 0x0F: this->gl_ = 0; break;
            default: break; // BEL等
        }
        return;
    }

    switch(this->ps_){
        case ParseState::Ground:
            if(b < 0x80){
                this->utf8_left_ = 0;
                if(b != 0x7F) this->putChar(b);
                return;
            }
            if(this->utf8_left_ > 0 && (b & 0xC0) == 0x80){
                this->utf8_cp_ = (this->utf8_cp_ << 6) | (b & 0x3F);
                if(--this->utf8_left_ == 0) this->putChar(this->utf8_cp_);
                return;
            }
            if(this->utf8_left_ > 0) this->putChar(0xFFFD); //途中で切れた
            if((b & 0xE0) == 0xC0){ this->utf8_cp_ = b & 0x1F; this->utf8_left_ = 1; }
            else if((b & 0xF0) == 0xE0){ this->utf8_cp_ = b & 0x0F; this->utf8_left_ = 2; }
            else if((b & 0xF8) == 0xF0){ this->utf8_cp_ = b & 0x07; this->utf8_left_ = 3; }
            else{ this->utf8_left_ = 0; this->putChar(0xFFFD); }
            return;

        case ParseState::Escape:
            switch(b){
                case '[':
                    this->ps_ = ParseState::Csi;
                    this->param_count_ = 0;
                    this->params_[0] = 0;
                    this->param_started_ = false;
                    this->private_ = 0;
                    this->intermediate_ = 0;
                    return;
                case ']':
                    this->ps_ = ParseState::Osc;
                    return;
                case 'P': case 'X': case '^': case '_':
                    this->ps_ = ParseState::Str;
                    return;
                case '(': this->charset_slot_ = 0; this->ps_ = ParseState::EscCharset; return;
                case ')': this->charset_slot_ = 1; this->ps_ = ParseState::EscCharset; return;
                case '*': case '+': case '#': case '%': case ' ':
                    this->charset_slot_ = -1;
                    this->ps_ = ParseState::EscCharset;
                    return;
                default:
                    this->ps_ = ParseState::Ground;
                    this->escDispatch(b);
                    return;
            }

        case ParseState::EscCharset:
            if(this->charset_slot_ >= 0) this->g_graphics_[this->charset_slot_] = (b == '0');
            this->ps_ = ParseState::Ground;
            return;

        case ParseState::Csi:
            if(b >= '0' && b <= '9'){
                int& p = this->params_[this->param_count_];
                p = p * 10 + (b - '0');
                if(p > 9999) p = 9999;
                this->param_started_ = true;
            }else if(b == ';' || b == ':'){
                if(this->param_count_ < kMaxParams - 1){
                    this->param_count_++;
                    this->params_[this->param_count_] = 0;
                }
                this->param_started_ = true;
            }else if(b >= '<' && b <= '?'){
                this->private_ = (char)b;
            }else if(b >= 0x20 && b <= 0x2F){
                this->intermediate_ = (char)b;
            }else if(b >= 0x40 && b <= 0x7E){
                this->param_count_ = this->param_started_ ? this->param_count_ + 1 : 0;
                this->ps_ = ParseState::Ground;
                this->csiDispatch(b);
            }else{
                this->ps_ = ParseState::Ground;
            }
            return;

        case ParseState::Osc:
        case ParseState::Str:
            if(b == 0x07) this->ps_ = ParseState::Ground;
            else if(b == 0x1B) this->ps_ = (this->ps_ == ParseState::Osc) ? ParseState::OscEsc : ParseState::StrEsc;
            return;

        case ParseState::OscEsc:
        case ParseState::StrEsc:
            this->ps_ = ParseState::Ground; // ESC \ (ST)。それ以外が来ても文字列は終わりにする
            return;
    }
}

void VtTerminal::write(const uint8_t* data, size_t len){
    for(size_t i = 0; i < len; i++) this->feed(data[i]);
}

void VtTerminal::write(const char* s){
    this->write((const uint8_t*)s, strlen(s));
}
