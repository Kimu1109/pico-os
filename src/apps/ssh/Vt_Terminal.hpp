#pragma once

#include <cstddef>
#include <cstdint>

// 端末エミュレータ(xterm互換の一部)。SSHアプリ(SshScene)の画面の中身。
//
// 受け取ったバイト列(UTF-8 + エスケープシーケンス)を解釈して、文字の格子(セル)を持つだけ。
// 描画は知らない(TerminalViewが描く)ので、ホストテストで中身を直接確かめられる。
//
// 対応している主なもの(TERM=xterm-256colorで普通のシェル・less・vim・top等が動く範囲):
//   - 制御文字: BS HT LF VT FF CR SO SI BEL(無視)
//   - ESC: 7 8(カーソル保存/復元) D E M c ( ) の文字集合(DECの罫線 '0' とASCII 'B')
//   - CSI: カーソル移動(A B C D E F G H f d e a `) 消去(J K X) 挿入/削除(@ P L M) スクロール(S T)
//          SGR(16色/256色/RGB→16色へ丸める、太字/下線/反転) 範囲スクロール(r) 保存/復元(s u)
//          モード(?1 カーソルキー / ?6 / ?7 自動折り返し / ?25 カーソル表示 / ?47 ?1047 ?1049 代替画面 / 4 挿入)
//          問い合わせ(c >c 5n 6n)の返事は reply() に溜まる(呼び出し側がサーバへ送る)
//   - OSC(ウィンドウ題名等)・DCS等の文字列は読み捨てる
//   - 全角文字(東アジアの幅広の文字)は2セルを使う。幅0の結合文字は捨てる
//
// セルは固定長の配列で持つ(確保しない)。大きさは最大 kMaxCols x kMaxRows、
// 通常画面と代替画面の2枚 + 流れ去った行(スクロールバック)kScrollbackLines行。
// 1セル4バイトなので全体で約29KB。
class VtTerminal {
    public:
        static constexpr int kMaxCols = 40;
        static constexpr int kMaxRows = 40;
        static constexpr int kScrollbackLines = 100;
        static constexpr int kReplyCap = 64;

        // 全角文字の右半分に置く印
        static constexpr uint16_t kWideRight = 0xFFFF;

        enum Attr : uint8_t {
            AttrBold      = 1 << 0,
            AttrUnderline = 1 << 1,
            AttrReverse   = 1 << 2,
            AttrWide      = 1 << 3, // 全角文字の左半分
        };

        // fg/bgはANSIの16色の番号(0〜15)。chが0なら空白
        struct Cell {
            uint16_t ch;
            uint8_t color; // 上位4bit=bg, 下位4bit=fg
            uint8_t attr;

            uint8_t fg() const { return color & 0x0F; }
            uint8_t bg() const { return color >> 4; }
        };

        static constexpr uint8_t kDefaultFg = 7;
        static constexpr uint8_t kDefaultBg = 0;

        VtTerminal();

        // 大きさを変える(内容は左上を基準に残る。縮めてカーソルがはみ出す場合は上を流す)
        void resize(int cols, int rows);
        int cols() const { return cols_; }
        int rows() const { return rows_; }

        // 全部消して初期状態へ戻す(スクロールバックも消す)
        void reset();

        void write(const uint8_t* data, size_t len);
        void write(const char* s);

        // y行目(0〜rows-1)。代替画面を使っていればそちら
        const Cell* row(int y) const;
        // 流れ去った行。0が一番新しい。無ければnullptr
        const Cell* scrollbackRow(int i) const;
        int scrollbackCount() const { return sb_count_; }

        int cursorX() const { return cx_; }
        int cursorY() const { return cy_; }
        bool cursorVisible() const { return cursor_visible_; }
        bool appCursorKeys() const { return app_cursor_; }
        bool altScreen() const { return alt_active_; }

        // 描き直しが要る行のビット(bit y)。takeDirty()で受け取ると消える
        uint64_t takeDirty(){ const uint64_t d = dirty_; dirty_ = 0; return d; }
        // スクロールバックへ行が流れた回数(表示側がスクロール位置を保つのに使う)
        uint32_t scrolledLines() const { return scrolled_; }

        // 問い合わせへの返事(サーバへ送るバイト列)。読んだら clearReply()
        const char* reply() const { return reply_; }
        size_t replyLength() const { return reply_len_; }
        void clearReply(){ reply_len_ = 0; }

        // 表示幅(0/1/2)。東アジアの幅広の文字は2
        static int CharWidth(uint32_t cp);

    private:
        enum class ParseState : uint8_t { Ground, Escape, EscCharset, Csi, Osc, OscEsc, Str, StrEsc };

        Cell main_[kMaxRows * kMaxCols];
        Cell alt_[kMaxRows * kMaxCols];
        Cell sb_[kScrollbackLines * kMaxCols];
        int sb_head_ = 0;  // 次に書く位置
        int sb_count_ = 0;
        uint32_t scrolled_ = 0;

        int cols_ = 30;
        int rows_ = 16;

        int cx_ = 0, cy_ = 0;
        bool wrap_pending_ = false;
        uint8_t cur_color_ = (kDefaultBg << 4) | kDefaultFg;
        uint8_t cur_attr_ = 0;
        int top_ = 0, bottom_ = 0; // スクロール範囲(両端を含む)
        bool origin_ = false;
        bool autowrap_ = true;
        bool insert_ = false;
        bool cursor_visible_ = true;
        bool app_cursor_ = false;
        bool alt_active_ = false;

        // 文字集合: G0/G1がDECの罫線か、どちらを使っているか
        bool g_graphics_[2] = { false, false };
        int gl_ = 0;
        int charset_slot_ = 0; // ESC ( / ESC ) のどちらを受けているか

        struct Saved {
            int x = 0, y = 0;
            uint8_t color = (kDefaultBg << 4) | kDefaultFg;
            uint8_t attr = 0;
            bool origin = false;
            bool g_graphics[2] = { false, false };
            int gl = 0;
        };
        Saved saved_main_, saved_alt_;

        uint64_t dirty_ = ~0ull;

        // 読み取り
        ParseState ps_ = ParseState::Ground;
        static constexpr int kMaxParams = 16;
        int params_[kMaxParams];
        int param_count_ = 0;
        bool param_started_ = false;
        char private_ = 0;
        char intermediate_ = 0;
        uint32_t utf8_cp_ = 0;
        int utf8_left_ = 0;
        uint32_t last_char_ = ' ';

        char reply_[kReplyCap];
        size_t reply_len_ = 0;

        Cell* screen(){ return alt_active_ ? alt_ : main_; }
        const Cell* screen() const { return alt_active_ ? alt_ : main_; }
        Cell* at(int x, int y){ return screen() + y * kMaxCols + x; }
        Cell blank() const { return Cell{ 0, (uint8_t)((cur_color_ & 0xF0) | kDefaultFg), 0 }; }

        void markRow(int y){ if(y >= 0 && y < 64) dirty_ |= (1ull << y); }
        void markAll(){ dirty_ = ~0ull; }

        void putChar(uint32_t cp);
        void newline();
        void scrollUp(int top, int bottom, int n, bool to_scrollback);
        void scrollDown(int top, int bottom, int n);
        void clearCells(int y, int x0, int x1);
        void fixWideAt(int x, int y);
        void moveTo(int x, int y);
        void pushScrollback(const Cell* line);

        void escDispatch(uint8_t c);
        void csiDispatch(uint8_t c);
        void sgr();
        void setMode(bool on);
        void saveCursor();
        void restoreCursor();
        void enterAlt(bool on, bool clear);
        void addReply(const char* s);

        int param(int i, int def) const {
            if(i >= param_count_ || params_[i] <= 0) return def;
            return params_[i];
        }
        void feed(uint8_t b);
};
