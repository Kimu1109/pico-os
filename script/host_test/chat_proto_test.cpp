// チャットの応答の読み取り(ChatProto)をソケット抜きで確かめる。run.sh から呼ぶ。
//
// 見ること:
//   - 本文のエスケープ(\\ \n \t)を戻す。知らない "\x" は x のまま、末尾の \ も落とさない
//   - 部屋/発言の1行を列に分ける。足りない列・数値でない列は捨てる。増えた列は無視する(前方互換)
//   - バイト列を好きな切れ目で渡しても同じ行になる(1バイトずつでも)。CRLFのCRは落ちる
//   - 長すぎる行は捨てて数え、次の行から読み直す
#include "chat/Chat_Proto.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}
static void eq_str(const char* a, const char* e, const char* label){
    const bool ok = strcmp(a, e) == 0;
    printf("%s %-40s 実測=\"%s\" 期待=\"%s\"\n", ok ? "[ OK ]" : "[FAIL]", label, a, e);
    if(!ok) failures++;
}
static void eq_int(long a, long e, const char* label){
    const bool ok = (a == e);
    printf("%s %-40s 実測=%ld 期待=%ld\n", ok ? "[ OK ]" : "[FAIL]", label, a, e);
    if(!ok) failures++;
}

struct Collect : public ChatProto::LineSink {
    std::vector<std::string> lines;
protected:
    void onLine(char* line, size_t len) override { lines.emplace_back(line, len); }
};

int main(){
    using namespace ChatProto;

    printf("---- ParseUint ----\n");
    {
        uint32_t v = 0;
        check(ParseUint("0", v) && v == 0, "0");
        check(ParseUint("4294967295", v) && v == 4294967295u, "32bitの上限");
        check(!ParseUint("4294967296", v), "32bitを超えたら失敗");
        check(!ParseUint("", v), "空は失敗");
        check(!ParseUint("12a", v), "数字以外が混ざると失敗");
        check(!ParseUint("-1", v), "負は失敗");
    }

    printf("---- Unescape ----\n");
    {
        Text t;
        check(Unescape("abc", t), "エスケープ無し");
        eq_str(t.c_str(), "abc", "そのまま");
        Unescape("1\\n2\\t3\\\\4", t);
        eq_str(t.c_str(), "1\n2\t3\\4", "\\n \\t \\\\ を戻す");
        Unescape("a\\qb", t);
        eq_str(t.c_str(), "aqb", "知らない \\x は x のまま");
        Unescape("末尾\\", t);
        eq_str(t.c_str(), "末尾\\", "末尾の \\ は落とさない");
        Unescape("こんにちは\\n世界", t);
        eq_str(t.c_str(), "こんにちは\n世界", "日本語を挟んでも戻す");

        std::string big(600, 'x');
        check(!Unescape(big.c_str(), t), "入りきらなければfalse");
        eq_int((long)t.length(), PICO_STR_512B - 1, "入ったところまでは残る");
    }

    printf("---- ParseRoom ----\n");
    {
        char l1[] = "3\t雑談\t120\t5";
        Room r;
        check(ParseRoom(l1, r), "4列の行を読める");
        eq_int(r.id, 3, "id");
        eq_str(r.name.c_str(), "雑談", "名前");
        eq_int(r.last_id, 120, "最新id");
        eq_int(r.unread, 5, "未読");

        char l2[] = "4\t連絡\t0\t0\t将来の列\tさらに";
        check(ParseRoom(l2, r) && r.id == 4 && r.unread == 0, "列が増えていても読める");

        char l3[] = "5\t名前だけ\t9";
        check(!ParseRoom(l3, r), "列が足りなければ捨てる");
        char l4[] = "x\t名前\t1\t1";
        check(!ParseRoom(l4, r), "idが数値でなければ捨てる");
        char l5[] = "0\t名前\t1\t1";
        check(!ParseRoom(l5, r), "id 0 は捨てる");
    }

    printf("---- ParseMessage ----\n");
    {
        char l1[] = "42\t1790000000\tありす\t1行目\\n2行目";
        Message m;
        check(ParseMessage(l1, m), "4列の行を読める");
        eq_int(m.id, 42, "id");
        eq_int(m.epoch, 1790000000, "時刻");
        eq_str(m.name.c_str(), "ありす", "名前");
        eq_str(m.text.c_str(), "1行目\n2行目", "本文のエスケープを戻す");

        char l2[] = "43\t1790000001\tぼぶ\t";
        check(ParseMessage(l2, m) && m.text.empty(), "本文が空でも読める");
        char l3[] = "44\t1790000001\tぼぶ";
        check(!ParseMessage(l3, m), "本文の列が無ければ捨てる");
        char l4[] = "45\tnow\tぼぶ\tx";
        check(!ParseMessage(l4, m), "時刻が数値でなければ捨てる");
    }

    printf("---- LineSink ----\n");
    {
        const std::string body = "1\ta\r\n2\tb\n\n3\tc";
        Collect whole;
        whole.write(body.data(), body.size());
        whole.flush();
        eq_int((long)whole.lines.size(), 3, "3行(空行は飛ばす、最後の改行無しも拾う)");
        if(whole.lines.size() == 3){
            eq_str(whole.lines[0].c_str(), "1\ta", "CRを落とす");
            eq_str(whole.lines[2].c_str(), "3\tc", "改行で終わらない最後の行");
        }

        Collect bytewise;
        for(char c : body) bytewise.write(&c, 1);
        bytewise.flush();
        check(bytewise.lines == whole.lines, "1バイトずつ渡しても同じ");

        std::string longline(kMaxLineBytes + 10, 'z');
        const std::string body2 = "ok1\n" + longline + "\nok2\n";
        Collect c2;
        c2.write(body2.data(), body2.size());
        c2.flush();
        eq_int((long)c2.lines.size(), 2, "長すぎる行は捨てる");
        eq_int(c2.droppedLines(), 1, "捨てた行を数える");
        if(c2.lines.size() == 2) eq_str(c2.lines[1].c_str(), "ok2", "次の行から読み直す");

        //ちょうど上限の行は読める(本文500バイトを全部エスケープした最悪の行を想定している)
        std::string exact(kMaxLineBytes, 'y');
        Collect c3;
        c3.write(exact.data(), exact.size());
        c3.write("\n", 1);
        eq_int((long)c3.lines.size(), 1, "上限ちょうどの行は読める");
    }

    printf("\n%s (%d件失敗)\n", failures ? "失敗" : "全て成功", failures);
    return failures ? 1 : 0;
}
