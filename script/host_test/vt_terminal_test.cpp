// 端末エミュレータ(src/ssh/Vt_Terminal)のホストテスト。描画は無く、文字の格子を直接見る。
#include "ssh/Vt_Terminal.hpp"

#include <cstdio>
#include <cstring>
#include <string>

static int failures = 0;
#define CHECK(cond) do { \
    if(!(cond)){ printf("  [NG] %s:%d %s\n", __FILE__, __LINE__, #cond); failures++; } \
    else { printf("  [OK] %s\n", #cond); } \
} while(0)

static VtTerminal t; // 約29KBあるのでスタックに置かない

// y行目をASCIIで(空白は' '、全角の左半分は'W'、右半分は'w'、それ以外の非ASCIIは'?')
static std::string Row(int y){
    std::string s;
    const VtTerminal::Cell* r = t.row(y);
    for(int x = 0; x < t.cols(); x++){
        const uint16_t ch = r[x].ch;
        if(ch == 0) s.push_back(' ');
        else if(ch == VtTerminal::kWideRight) s.push_back('w');
        else if(r[x].attr & VtTerminal::AttrWide) s.push_back('W');
        else if(ch < 128) s.push_back((char)ch);
        else s.push_back('?');
    }
    return s;
}

static std::string Sb(int i){
    std::string s;
    const VtTerminal::Cell* r = t.scrollbackRow(i);
    if(!r) return "(none)";
    for(int x = 0; x < t.cols(); x++) s.push_back(r[x].ch && r[x].ch < 128 ? (char)r[x].ch : ' ');
    return s;
}

static void Fresh(int cols, int rows){
    t.resize(cols, rows);
    t.reset();
    t.takeDirty();
}

int main(){
    printf("===== 文字と折り返し =====\n");
    Fresh(10, 4);
    t.write("0123456789");
    CHECK(Row(0) == "0123456789");
    CHECK(t.cursorX() == 9 && t.cursorY() == 0); // 右端で止まって折り返し待ち
    t.write("AB");
    CHECK(Row(1) == "AB        ");
    CHECK(t.cursorX() == 2 && t.cursorY() == 1);
    t.write("\r\nxy\rZ");
    CHECK(Row(2) == "Zy        ");
    t.write("\tT");
    CHECK(Row(2) == "Zy      T ");
    t.write("\bQ");
    CHECK(Row(2) == "Zy      Q ");
    // 折り返しを切ると右端で上書きし続ける
    Fresh(5, 2);
    t.write("\x1b[?7l" "abcdefg");
    CHECK(Row(0) == "abcdg");
    CHECK(Row(1) == "     ");

    printf("===== カーソル移動と消去 =====\n");
    Fresh(10, 5);
    t.write("\x1b[3;5HX");
    CHECK(Row(2) == "    X     ");
    t.write("\x1b[2AY\x1b[3BZ\x1b[10D<\x1b[20C>");
    CHECK(Row(0) == "     Y    ");
    CHECK(Row(3) == "<     Z  >");
    t.write("\x1b[1;1H" "aaaaaaaaaa" "\x1b[1;4H\x1b[K");
    CHECK(Row(0) == "aaa       ");
    t.write("\x1b[4;5H\x1b[1K");
    CHECK(Row(3) == "      Z  >");
    t.write("\x1b[2J");
    CHECK(Row(0) == "          " && Row(3) == "          ");
    t.write("\x1b[1;1Habcdefghij\x1b[1;3H\x1b[2P");
    CHECK(Row(0) == "abefghij  ");
    t.write("\x1b[2@");
    CHECK(Row(0) == "ab  efghij");
    t.write("\x1b[3X");
    CHECK(Row(0) == "ab   fghij");
    t.write("\x1b[1;1HA\x1b[3b");
    CHECK(Row(0) == "AAAA fghij");

    printf("===== 色と属性 =====\n");
    Fresh(10, 3);
    t.write("\x1b[31;44mR\x1b[0mN\x1b[1;7mB\x1b[38;5;196mC\x1b[38;2;0;0;255mD\x1b[91;102mE\x1b[39;49mF");
    const VtTerminal::Cell* r0 = t.row(0);
    CHECK(r0[0].fg() == 1 && r0[0].bg() == 4);
    CHECK(r0[1].fg() == VtTerminal::kDefaultFg && r0[1].bg() == VtTerminal::kDefaultBg && r0[1].attr == 0);
    CHECK((r0[2].attr & VtTerminal::AttrBold) && (r0[2].attr & VtTerminal::AttrReverse));
    CHECK(r0[3].fg() == 9);   // 256色の196(純粋な赤)→明るい赤
    CHECK(r0[4].fg() == 12);  // RGB(0,0,255)→明るい青
    CHECK(r0[5].fg() == 9 && r0[5].bg() == 10);
    CHECK(r0[6].fg() == VtTerminal::kDefaultFg && r0[6].bg() == VtTerminal::kDefaultBg);
    // 消去は今の背景色で塗る
    t.write("\x1b[42m\x1b[2;1H\x1b[K\x1b[0m");
    CHECK(t.row(1)[5].bg() == 2);

    printf("===== 全角 =====\n");
    Fresh(6, 3);
    t.write("a\xe3\x81\x82" "b"); // aあb
    CHECK(Row(0) == "aWwb  ");
    CHECK(t.cursorX() == 4);
    t.write("\x1b[1;3Hx"); // 全角の右半分を上書きすると左半分も消える
    CHECK(Row(0) == "a xb  ");
    // 右端に1セルしか無いと次の行へ送る
    t.write("\x1b[2;6H\xe6\x97\xa5");
    CHECK(Row(1) == "      ");
    CHECK(Row(2) == "Ww    ");
    // 途中で切れて届いてもよい
    Fresh(6, 2);
    t.write((const uint8_t*)"\xe6", 1);
    t.write((const uint8_t*)"\x97", 1);
    t.write((const uint8_t*)"\xa5", 1);
    CHECK(Row(0) == "Ww    ");
    CHECK(t.row(0)[0].ch == 0x65E5);
    // 結合文字は捨てる / 幅
    CHECK(VtTerminal::CharWidth(0x0301) == 0);
    CHECK(VtTerminal::CharWidth(0x3042) == 2);
    CHECK(VtTerminal::CharWidth(0xFF21) == 2);
    CHECK(VtTerminal::CharWidth(0xFF76) == 1); // 半角カナ
    CHECK(VtTerminal::CharWidth(0x2500) == 1);
    CHECK(VtTerminal::CharWidth(0x1F600) == 2);

    printf("===== スクロールとスクロールバック =====\n");
    Fresh(4, 3);
    const uint32_t scrolled_before = t.scrolledLines(); // 増えた分だけを見る(表示側は差分を使う)
    t.write("L1\r\nL2\r\nL3\r\nL4\r\nL5");
    CHECK(Row(0) == "L3  " && Row(2) == "L5  ");
    CHECK(t.scrollbackCount() == 2);
    CHECK(Sb(0) == "L2  " && Sb(1) == "L1  ");
    CHECK(t.scrolledLines() - scrolled_before == 2);
    // 範囲スクロールはスクロールバックへ流さない
    Fresh(4, 4);
    t.write("A\r\nB\r\nC\r\nD\x1b[2;3r\x1b[3;1H\n\n");
    CHECK(Row(0) == "A   " && Row(1) == "    " && Row(2) == "    " && Row(3) == "D   ");
    CHECK(t.scrollbackCount() == 0);
    // 逆改行(上端で下へずらす)
    t.write("\x1b[r\x1b[1;1HX\x1bM");
    CHECK(Row(0) == "    " && Row(1) == "X   ");
    // 行の挿入/削除
    Fresh(4, 4);
    t.write("1\r\n2\r\n3\r\n4\x1b[2;1H\x1b[L");
    CHECK(Row(0) == "1   " && Row(1) == "    " && Row(2) == "2   " && Row(3) == "3   ");
    t.write("\x1b[M");
    CHECK(Row(1) == "2   " && Row(3) == "    ");
    CHECK(t.scrollbackCount() == 0);

    printf("===== 代替画面 =====\n");
    Fresh(5, 2);
    t.write("main\x1b[?1049h");
    CHECK(t.altScreen());
    CHECK(Row(0) == "     ");
    t.write("\x1b[1;1Hvim");
    CHECK(Row(0) == "vim  ");
    t.write("\x1b[?1049l");
    CHECK(!t.altScreen());
    CHECK(Row(0) == "main ");
    CHECK(t.cursorX() == 4 && t.cursorY() == 0);
    // 代替画面に居ないときの ?1049l ではカーソルを動かさない
    t.write("\r\nab\x1b[?1049l");
    CHECK(t.cursorX() == 2 && t.cursorY() == 1);

    printf("===== 問い合わせへの返事 =====\n");
    Fresh(10, 5);
    t.write("\x1b[3;7H\x1b[6n");
    CHECK(std::string(t.reply(), t.replyLength()) == "\x1b[3;7R");
    t.clearReply();
    t.write("\x1b[c\x1b[>c\x1b[5n");
    CHECK(std::string(t.reply(), t.replyLength()) == "\x1b[?1;2c\x1b[>0;276;0c\x1b[0n");
    t.clearReply();

    printf("===== モード・文字集合・読み捨て =====\n");
    Fresh(10, 2);
    CHECK(!t.appCursorKeys());
    t.write("\x1b[?1h");
    CHECK(t.appCursorKeys());
    t.write("\x1b[?25l");
    CHECK(!t.cursorVisible());
    t.write("\x1b(0lqk\x1b(Bq");
    CHECK(t.row(0)[0].ch == 0x250C && t.row(0)[1].ch == 0x2500 && t.row(0)[2].ch == 0x2510 && t.row(0)[3].ch == 'q');
    t.write("\x1b]0;window title\x07" "A\x1b]2;x\x1b\\" "B\x1bP1$r\x1b\\" "C");
    CHECK(Row(0) == "???qABC   ");
    t.write("\x1b[?2004h\x1b[>4;1m\x1b[2 q\x1b=\x1b>D");
    CHECK(t.row(0)[7].ch == 'D');
    // 挿入モード
    Fresh(6, 1);
    t.write("abc\x1b[1;1H\x1b[4hXY\x1b[4lZ");
    CHECK(Row(0) == "XYZbc ");

    printf("===== 大きさの変更 =====\n");
    Fresh(6, 4);
    t.write("1\r\n2\r\n3\r\n4");
    t.resize(6, 2); // カーソルが4行目なので上の2行が流れる
    CHECK(Row(0) == "3     " && Row(1) == "4     ");
    CHECK(t.cursorY() == 1);
    CHECK(t.scrollbackCount() == 2 && Sb(0) == "2     ");
    t.resize(8, 3);
    CHECK(t.cols() == 8 && t.rows() == 3);
    CHECK(Row(2) == "        ");
    t.resize(100, 100);
    CHECK(t.cols() == VtTerminal::kMaxCols && t.rows() == VtTerminal::kMaxRows);
    t.write("\x1b[999;999H#");
    CHECK(t.cursorX() == VtTerminal::kMaxCols - 1 && t.cursorY() == VtTerminal::kMaxRows - 1);

    printf("===== 描き直しの印 =====\n");
    Fresh(10, 5);
    t.write("\x1b[3;1Hx");
    const uint64_t d = t.takeDirty();
    CHECK(d == (1ull << 2));
    CHECK(t.takeDirty() == 0);

    printf("\n%s (%d件の失敗)\n", failures == 0 ? "全て成功" : "失敗あり", failures);
    return failures == 0 ? 0 : 1;
}
