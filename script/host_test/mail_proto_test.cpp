// メールアプリの読み取り部品(apps/mail/ の Mail_Charset・Mail_Mime・Imap_Proto)のテスト。run.sh から呼ぶ。
//
// 通信は run_net.sh の mail_net_test(偽物のIMAPサーバ相手)の担当で、ここでは
//   - 文字コード: ISO-2022-JP / Shift_JIS / EUC-JP / UTF-8(壊れた並びは 〓)/ windows-1252、分けて渡しても同じ結果
//   - ヘッダー: RFC 2047(B/Q、語の間の空白、UTF-8の1文字を2語に分けたもの)、差出人の名前、日時
//   - 本文: multipart の入れ子・text/plain を優先・HTMLだけのとき・base64 / quoted-printable・添付の名前(RFC 2231 も)・
//     置き場が足りないときに文字の途中で切らない
//   - IMAP: リテラルを含む応答の組み立て(貯める/流す/溢れる)、FETCH の読み取り、quoted string
//   - Microsoft のサインイン: JsonReader の長い値の置き場(stringBuffer)、フォームの値の書き方、
//     更新用トークンの保存(暗号化・行に分ける・壊れていれば読まない)
// を見る。
#include "apps/mail/Mail_Charset.hpp"
#include "apps/mail/Mail_Mime.hpp"
#include "apps/mail/Imap_Proto.hpp"
#include "apps/mail/Mail_OAuth.hpp"
#include "util/Json_Reader.hpp"
#include "storage/SD_Path.hpp"
#include "system/Log_Functions.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}
static void eq_int(long a, long e, const char* label){
    const bool ok = (a == e);
    printf("%s %-46s 実測=%ld 期待=%ld\n", ok ? "[ OK ]" : "[FAIL]", label, a, e);
    if(!ok) failures++;
}
static void eq_str(const char* a, const char* e, const char* label){
    const bool ok = strcmp(a, e) == 0;
    printf("%s %-46s 実測=\"%s\"\n", ok ? "[ OK ]" : "[FAIL]", label, a);
    if(!ok) failures++;
}

// ---- 文字コード ----
static std::string decode(MailCharset::Charset cs, const char* bytes, size_t n, size_t chunk = 0){
    char out[512];
    MailCharset::BufSink sink(out, sizeof(out));
    MailCharset::Decoder d;
    d.begin(cs);
    if(chunk == 0) chunk = n;
    for(size_t i = 0; i < n; i += chunk){
        const size_t k = (n - i < chunk) ? n - i : chunk;
        d.feed((const uint8_t*)bytes + i, k, sink);
    }
    d.finish(sink);
    return std::string(out, sink.length());
}

static void testCharset(){
    using C = MailCharset::Charset;
    eq_int((int)MailCharset::Parse("ISO-2022-JP"), (int)C::Iso2022Jp, "charset名: ISO-2022-JP");
    eq_int((int)MailCharset::Parse("\"Shift_JIS\""), (int)C::ShiftJis, "charset名: 引用符つき Shift_JIS");
    eq_int((int)MailCharset::Parse("windows-31j"), (int)C::ShiftJis, "charset名: windows-31j");
    eq_int((int)MailCharset::Parse("euc-jp"), (int)C::EucJp, "charset名: euc-jp");
    eq_int((int)MailCharset::Parse("us-ascii"), (int)C::Latin1, "charset名: us-ascii");
    eq_int((int)MailCharset::Parse("x-unknown"), (int)C::Utf8, "charset名: 知らない名前はUTF-8");

    const char jis[] = "\x1B\x24\x42\x24\x33\x24\x73\x24\x4B\x24\x41\x24\x4F\x21\x22\x40\x24\x33\x26\x1B\x28\x42";
    eq_str(decode(C::Iso2022Jp, jis, sizeof(jis) - 1).c_str(), "こんにちは、世界", "ISO-2022-JP");
    eq_str(decode(C::Iso2022Jp, jis, sizeof(jis) - 1, 1).c_str(), "こんにちは、世界", "ISO-2022-JP(1バイトずつ)");
    const char sjis[] = "\x83\x5C\x95\x5C\x87\x40\x81\x60\xB1";
    eq_str(decode(C::ShiftJis, sjis, sizeof(sjis) - 1).c_str(), "ソ表①～ｱ", "Shift_JIS(0x5C を含む字・NEC特殊・半角カナ)");
    eq_str(decode(C::ShiftJis, sjis, sizeof(sjis) - 1, 1).c_str(), "ソ表①～ｱ", "Shift_JIS(1バイトずつ)");
    const char euc[] = "\xC6\xFC\xCB\xDC\xB8\xEC\x8E\xB6\x8E\xC5";
    eq_str(decode(C::EucJp, euc, sizeof(euc) - 1).c_str(), "日本語ｶﾅ", "EUC-JP");
    eq_str(decode(C::Utf8, "a\xE3\x81\x82\xFF" "b\xE3\x81", 8).c_str(), "aあ〓b〓", "UTF-8: 壊れた並びは 〓");
    eq_str(decode(C::Utf8, "\xED\xA0\x80", 3).c_str(), "〓", "UTF-8: サロゲートは 〓");
    eq_str(decode(C::Latin1, "caf\xE9 \x93q\x94", 8).c_str(), "café “q”", "windows-1252");
    eq_str(decode(C::Iso2022Jp, "\x1B$B$\n" "A", 6).c_str(), "〓\nA", "ISO-2022-JP: 戻し忘れは改行でASCIIへ");
}

// ---- ヘッダー ----
static std::string header(const char* raw){
    char out[256];
    MailMime::DecodeHeader(raw, strlen(raw), out, sizeof(out));
    return out;
}
static std::string display(const char* raw){
    char out[128];
    MailMime::DisplayAddress(raw, out, sizeof(out));
    return out;
}

static void testHeaders(){
    eq_str(header("=?ISO-2022-JP?B?GyRCJCpDTiRpJDsbKEI=?=").c_str(), "お知らせ", "RFC2047: ISO-2022-JP の B");
    eq_str(header("=?UTF-8?B?44GC4w==?= =?UTF-8?B?gYTjgYY=?=").c_str(), "あいう", "RFC2047: UTF-8の1文字を2語に分けたもの");
    eq_str(header("=?utf-8?q?caf=C3=A9_au_lait?=").c_str(), "café au lait", "RFC2047: Q と _");
    eq_str(header("Re: =?UTF-8?B?44GC?= done").c_str(), "Re: あ done", "RFC2047: 普通の語との間の空白は残す");
    eq_str(header("Hello\r\n\tWorld").c_str(), "Hello World", "折り返しを1つの空白に");
    eq_str(header("=?UTF-8?B?not base64?=").c_str(), "=?UTF-8?B?not base64?=", "壊れた語はそのまま");
    eq_str(header("  plain  ").c_str(), "plain", "前後の空白を落とす");

    eq_str(display("\"山田 太郎\" <taro@example.com>").c_str(), "山田 太郎", "差出人: 引用符つきの名前");
    eq_str(display("Taro Yamada <taro@example.com>").c_str(), "Taro Yamada", "差出人: 引用符なしの名前");
    eq_str(display("<taro@example.com>").c_str(), "taro@example.com", "差出人: アドレスだけ(<>)");
    eq_str(display("taro@example.com").c_str(), "taro@example.com", "差出人: アドレスだけ");
    eq_str(display("taro@example.com (Taro)").c_str(), "Taro", "差出人: 括弧の注釈");
    eq_str(display("\"A, B\" <ab@x>, c@y").c_str(), "A, B", "差出人: 名前の中のカンマ");

    int64_t t = 0;
    check(MailMime::ParseInternalDate("\"17-Jul-1996 02:44:25 -0700\"", t), "INTERNALDATE を読める");
    eq_int((long)t, 837596665L, "INTERNALDATE → UTC");
    check(MailMime::ParseInternalDate(" 7-Jul-2026 09:00:00 +0900", t), "INTERNALDATE: 1桁の日");
    eq_int((long)t, 1783382400L, "INTERNALDATE: +0900");
    check(MailMime::ParseDate("Tue, 20 Oct 2026 10:00:00 +0900", t), "Date を読める");
    eq_int((long)t, 1792458000L, "Date → UTC");
    check(MailMime::ParseDate("20 Oct 2026 01:00 GMT", t), "Date: 曜日・秒なし、GMT");
    eq_int((long)t, 1792458000L, "Date: GMT");
    check(!MailMime::ParseDate("not a date", t), "Date: 読めないものはfalse");

    char v[96];
    check(MailMime::GetParam("text/plain; charset=\"ISO-2022-JP\"; format=flowed", "charset", v, sizeof(v)), "引数: charset");
    eq_str(v, "ISO-2022-JP", "引数: 引用符を外す");
    check(MailMime::GetParam("multipart/mixed;\r\n\tboundary=\"----=_Part_1\"", "boundary", v, sizeof(v)), "引数: 折り返しの後の boundary");
    eq_str(v, "----=_Part_1", "引数: boundary の値");
    check(MailMime::GetParam("attachment; filename*=UTF-8''%E8%AB%8B%E6%B1%82.pdf", "filename", v, sizeof(v)), "引数: RFC2231");
    eq_str(v, "請求.pdf", "引数: RFC2231 の値");
    check(MailMime::GetParam("attachment; filename*0*=UTF-8''%E3%81%82; filename*1=.txt", "filename", v, sizeof(v)), "引数: RFC2231 の分割");
    eq_str(v, "あ.txt", "引数: RFC2231 の分割の値");
    check(!MailMime::GetParam("text/plain", "charset", v, sizeof(v)), "引数: 無ければfalse");
}

// ---- 本文 ----
struct Body {
    std::string text;
    bool truncated;
    bool html;
    int attachments;
    std::string names;
    std::string from, subject, to;
};

static Body extract(const std::string& mail, size_t cap = 4096, size_t chunk = 0){
    static MailMime::TextExtractor x;
    std::vector<char> out(cap);
    x.begin(out.data(), cap);
    if(chunk == 0) chunk = mail.size() ? mail.size() : 1;
    for(size_t i = 0; i < mail.size(); i += chunk){
        x.feed((const uint8_t*)mail.data() + i, std::min(chunk, mail.size() - i));
    }
    x.finish();
    Body b;
    b.text.assign(out.data(), x.length());
    b.truncated = x.truncated();
    b.html = x.fromHtml();
    b.attachments = x.attachmentCount();
    b.names = x.attachmentNames();
    b.from = x.from();
    b.subject = x.subject();
    b.to = x.to();
    return b;
}

static void testBody(){
    //1. 一番単純な ISO-2022-JP の text/plain(7bit)
    {
        std::string m = "From: =?ISO-2022-JP?B?GyRCJCpDTiRpJDsbKEI=?= <info@example.jp>\r\n"
                        "To: you@example.jp\r\n"
                        "Subject: =?ISO-2022-JP?B?GyRCJCpDTiRpJDsbKEI=?=\r\n"
                        "Content-Type: text/plain; charset=ISO-2022-JP\r\n"
                        "\r\n"
                        "\x1B$BK\\J8$G$9!#\x1B(B\r\n"
                        "\r\n\r\n\r\n"
                        "\x1B$BFs9TL\\\x1B(B\r\n";
        Body b = extract(m);
        eq_str(b.text.c_str(), "本文です。\n\n二行目", "本文: ISO-2022-JP・空行は1行に詰める");
        eq_str(b.subject.c_str(), "お知らせ", "本文: 件名");
        eq_str(b.from.c_str(), "お知らせ <info@example.jp>", "本文: 差出人(そのまま)");
        eq_str(b.to.c_str(), "you@example.jp", "本文: 宛先");
        Body c = extract(m, 4096, 3);
        eq_str(c.text.c_str(), b.text.c_str(), "本文: 3バイトずつ渡しても同じ");
    }
    //2. multipart/mixed の中に alternative(plain + html)と添付
    {
        std::string m = "Subject: test\r\n"
                        "MIME-Version: 1.0\r\n"
                        "Content-Type: multipart/mixed; boundary=\"outer\"\r\n"
                        "\r\n"
                        "This is a multi-part message.\r\n"
                        "--outer\r\n"
                        "Content-Type: multipart/alternative;\r\n boundary=\"inner\"\r\n"
                        "\r\n"
                        "--inner\r\n"
                        "Content-Type: text/plain; charset=UTF-8\r\n"
                        "Content-Transfer-Encoding: base64\r\n"
                        "\r\n"
                        "5pys5paH44Gn44GZ44CCDQrkuozooYznm64NCg==\r\n"
                        "--inner\r\n"
                        "Content-Type: text/html; charset=UTF-8\r\n"
                        "\r\n"
                        "<p>HTML版</p>\r\n"
                        "--inner--\r\n"
                        "--outer\r\n"
                        "Content-Type: application/pdf; name=\"=?ISO-2022-JP?B?GyRCQEE1YT1xGyhC?=.pdf\"\r\n"
                        "Content-Disposition: attachment; filename=\"=?ISO-2022-JP?B?GyRCQEE1YT1xGyhCLnBkZg==?=\"\r\n"
                        "Content-Transfer-Encoding: base64\r\n"
                        "\r\n"
                        "JVBERi0xLjQK\r\n"
                        "--outer\r\n"
                        "Content-Type: image/png\r\n"
                        "Content-Transfer-Encoding: base64\r\n"
                        "\r\n"
                        "iVBORw0KGgo=\r\n"
                        "--outer--\r\n"
                        "epilogue\r\n";
        Body b = extract(m);
        eq_str(b.text.c_str(), "本文です。\n二行目", "multipart: text/plain(base64)を使う");
        check(!b.html, "multipart: HTMLではない");
        eq_int(b.attachments, 2, "multipart: 添付は2つ");
        eq_str(b.names.c_str(), "請求書.pdf, image/png", "multipart: 添付の名前(無ければ型)");
        Body c = extract(m, 4096, 7);
        eq_str(c.text.c_str(), b.text.c_str(), "multipart: 7バイトずつでも同じ");
    }
    //3. HTMLしか無い(quoted-printable)。タグ・style・実体参照・空白
    {
        std::string m = "Content-Type: text/html; charset=\"utf-8\"\r\n"
                        "Content-Transfer-Encoding: quoted-printable\r\n"
                        "\r\n"
                        "<html><head><style>p { color: red; }</style><title>T</title></head>\r\n"
                        "<body><!-- comment > here --><p>=E3=81=93=E3=82=93=E3=81=AB=\r\n"
                        "=E3=81=A1=E3=81=AF &amp; &lt;tag&gt;</p>\r\n"
                        "<ul><li>one</li><li>two&nbsp;&#x2605;</li></ul>a&b; x<br>y\r\n"
                        "</body></html>\r\n";
        Body b = extract(m);
        check(b.html, "HTML: HTMLから作った");
        eq_str(b.text.c_str(), "こんにちは & <tag>\n\n・one\n・two ★\n\na&b; x\ny", "HTML: 文字だけにする");
    }
    //4. alternative の並びが html → plain(後ろの plain を使う)
    {
        std::string m = "Content-Type: multipart/alternative; boundary=b\r\n\r\n"
                        "--b\r\nContent-Type: text/html\r\n\r\n<b>html</b>\r\n"
                        "--b\r\nContent-Type: text/plain\r\n\r\nplain text\r\n"
                        "--b--\r\n";
        Body b = extract(m);
        eq_str(b.text.c_str(), "plain text", "alternative: 後ろの text/plain を優先");
        eq_int(b.attachments, 0, "alternative: 添付扱いしない");
    }
    //5. Shift_JIS の 8bit 本文。置き場が足りないときは文字の途中で切らない
    {
        std::string m = "Content-Type: text/plain; charset=Shift_JIS\r\nContent-Transfer-Encoding: 8bit\r\n\r\n";
        for(int i = 0; i < 20; i++) m += "\x83\x5C\x95\x5C";  // ソ表 × 20
        Body b = extract(m, 32);
        check(b.truncated, "置き場不足: 切ったと分かる");
        eq_str(b.text.c_str(), "ソ表ソ表ソ表ソ表ソ表", "置き場不足: 文字の切れ目で止める");
    }
    //6. 1行が置き場(1KB)より長い本文(区切って渡す)と、境界に似た行
    {
        std::string longline(3000, 'x');
        std::string m = "Content-Type: multipart/mixed; boundary=zz\r\n\r\n"
                        "--zz\r\nContent-Type: text/plain\r\n\r\n" + longline + "\r\n--zzz not a boundary\r\n--zz--\r\n";
        Body b = extract(m, 8192);
        eq_int((long)b.text.size(), 3000 + 1 + 20, "長い行: 区切っても全部読む");
        check(b.text.find("--zzz not a boundary") != std::string::npos, "長い行: 境界に似た行は本文");
    }
    //7. ヘッダーだけで本文の無いメール
    {
        Body b = extract("Subject: only header\r\n");
        eq_str(b.subject.c_str(), "only header", "ヘッダーだけ: 件名");
        eq_str(b.text.c_str(), "", "ヘッダーだけ: 本文は空");
    }
}

// ---- IMAP ----
struct Collect : Imap::Reader::Handler {
    std::vector<std::string> responses;
    std::vector<bool> overflows;
    std::string streamed;
    std::string literal_line;
    int ends = 0;
    bool stream_body = true;
    bool onLiteralStart(const char* line, size_t len, uint32_t) override {
        literal_line.assign(line, len);
        return stream_body && strstr(line, "BODY[]") != nullptr;
    }
    void onLiteralData(const uint8_t* d, size_t n) override { streamed.append((const char*)d, n); }
    void onLiteralEnd() override { ends++; }
    void onResponse(const char* t, size_t n, bool of) override {
        responses.emplace_back(t, n);
        overflows.push_back(of);
    }
};

static void feedAll(Imap::Reader& r, Collect& c, const std::string& s, size_t chunk){
    for(size_t i = 0; i < s.size(); i += chunk){
        r.feed((const uint8_t*)s.data() + i, std::min(chunk, s.size() - i), c);
    }
}

static void testImap(){
    //応答の分類
    Imap::Response r;
    Imap::ParseResponse("* 23 EXISTS", r);
    check(r.kind == Imap::Kind::Untagged && r.has_number && r.number == 23 && Imap::KeywordIs(r, "EXISTS"), "応答: * 23 EXISTS");
    Imap::ParseResponse("a7 NO [AUTHENTICATIONFAILED] Invalid credentials", r);
    check(r.kind == Imap::Kind::Tagged && r.tag == 7 && r.status == Imap::Status::No, "応答: a7 NO");
    eq_str(r.text, "[AUTHENTICATIONFAILED] Invalid credentials", "応答: 説明");
    Imap::ParseResponse("* OK [UIDVALIDITY 3857529045] UIDs valid", r);
    check(r.kind == Imap::Kind::Untagged && r.status == Imap::Status::Ok && !r.has_number, "応答: * OK [..]");
    Imap::ParseResponse("+ go ahead", r);
    check(r.kind == Imap::Kind::Continuation, "応答: +");

    //リテラルを含むヘッダーの FETCH(1バイトずつ / まとめて)
    const std::string hdr = "From: \"=?UTF-8?B?5bGx55Sw?=\" <yamada@example.jp>\r\n"
                            "Subject: =?ISO-2022-JP?B?GyRCJCpDTiRpJDsbKEI=?=\r\n"
                            "Date: Tue, 20 Oct 2026 10:00:00 +0900\r\n\r\n";
    const std::string fetch = "* 12 FETCH (UID 345 FLAGS (\\Seen \\Flagged) INTERNALDATE \"20-Oct-2026 10:00:05 +0900\" "
                              "RFC822.SIZE 4567 BODY[HEADER.FIELDS (FROM SUBJECT DATE)] {" + std::to_string(hdr.size()) + "}\r\n"
                              + hdr + ")\r\n";
    for(size_t chunk : {(size_t)1, (size_t)4096}){
        Imap::Reader reader;
        reader.reset();
        Collect c;
        feedAll(reader, c, fetch + "a3 OK FETCH completed\r\n", chunk);
        eq_int((long)c.responses.size(), 2, chunk == 1 ? "組み立て(1バイトずつ): 応答は2つ" : "組み立て(まとめて): 応答は2つ");
        if(c.responses.size() != 2) continue;
        Imap::Fetch f;
        check(Imap::ParseFetch(c.responses[0].c_str(), c.responses[0].size(), f), "FETCH: 読める");
        eq_int(f.seq, 12, "FETCH: 番号");
        eq_int(f.uid, 345, "FETCH: UID");
        check(f.has_flags && f.seen && f.flagged && !f.answered, "FETCH: FLAGS");
        eq_int(f.size, 4567, "FETCH: 大きさ");
        eq_int((long)f.date, 1792458005L, "FETCH: INTERNALDATE");
        eq_int((long)f.header_len, (long)hdr.size(), "FETCH: ヘッダーのリテラル");
        Imap::Summary s;
        Imap::ApplyFetch(f, s);
        eq_str(s.from.c_str(), "山田", "Summary: 差出人の名前");
        eq_str(s.subject.c_str(), "お知らせ", "Summary: 件名");
        check(s.seen && s.uid == 345 && s.seq == 12, "Summary: UID・既読");
    }

    //本文のリテラルは流す。応答の中には "" が残り、後ろの FLAGS も読める
    {
        const std::string body = "Subject: x\r\n\r\nline1\r\nline2\r\n";
        const std::string s = "* 5 FETCH (UID 9 BODY[]<0> {" + std::to_string(body.size()) + "}\r\n" + body + " FLAGS (\\Seen))\r\n";
        Imap::Reader reader;
        reader.reset();
        Collect c;
        feedAll(reader, c, s, 5);
        eq_str(c.streamed.c_str(), body.c_str(), "流す: 中身がそのまま届く");
        eq_int(c.ends, 1, "流す: 終わりの知らせ");
        eq_int((long)c.responses.size(), 1, "流す: 応答は1つ");
        Imap::Fetch f;
        check(c.responses.size() == 1 && Imap::ParseFetch(c.responses[0].c_str(), c.responses[0].size(), f)
              && f.uid == 9 && f.has_flags && f.seen, "流す: 後ろの FLAGS まで読める");
    }

    //貯めきれない大きなリテラル(流さない)は、入る分だけにして印を書き直す
    {
        std::string big(10000, 'h');
        const std::string s = "* 6 FETCH (UID 10 BODY[HEADER] {" + std::to_string(big.size()) + "}\r\n" + big + " FLAGS ())\r\n"
                              "* 7 EXISTS\r\n";
        Imap::Reader reader;
        reader.reset();
        Collect c;
        c.stream_body = false;
        feedAll(reader, c, s, 333);
        eq_int((long)c.responses.size(), 2, "大きなリテラル: 後ろの応答とずれない");
        Imap::Fetch f;
        check(c.responses.size() == 2 && Imap::ParseFetch(c.responses[0].c_str(), c.responses[0].size(), f)
              && f.uid == 10 && f.header_len > 0 && f.header_len < 4096 && f.has_flags, "大きなリテラル: 途中までで読める");
        check(c.responses.size() == 2 && c.responses[1] == "* 7 EXISTS", "大きなリテラル: 次の応答");
    }

    //行そのものが長すぎる応答は overflow で渡し、続く応答は正しく読む
    {
        std::string s = "* OK " + std::string(6000, 'z') + "\r\n* 3 EXISTS\r\n";
        Imap::Reader reader;
        reader.reset();
        Collect c;
        feedAll(reader, c, s, 1000);
        check(c.responses.size() == 2 && c.overflows[0] && !c.overflows[1] && c.responses[1] == "* 3 EXISTS", "溢れた行: 次とずれない");
    }

    //ENVELOPE 等の知らない項目は読み飛ばす
    {
        const char* s = "* 1 FETCH (ENVELOPE (\"date\" \"subj\" ((\"n\" NIL \"a\" \"b\")) NIL NIL NIL NIL NIL NIL \"<id>\") UID 77)";
        Imap::Fetch f;
        check(Imap::ParseFetch(s, strlen(s), f) && f.has_uid && f.uid == 77, "FETCH: 知らない項目を読み飛ばす");
    }

    char q[64];
    check(Imap::Quote("pa\"ss\\word", q, sizeof(q)), "Quote: \" と \\");
    eq_str(q, "\"pa\\\"ss\\\\word\"", "Quote: エスケープ");
    check(!Imap::Quote("日本語", q, sizeof(q)), "Quote: 8bitは不可");
    check(!Imap::Quote("a\r\nb", q, sizeof(q)), "Quote: 改行は不可");
}

// ---- Microsoft のサインイン ----
namespace {
    struct LongPick : JsonReader::Handler {
        char token[1200] = {};
        size_t token_len = 0;
        bool token_trunc = false;
        std::string short_value;
        bool short_trunc = false;
        char* stringBuffer(const char* key, int depth, size_t& cap) override {
            if(depth == 1 && key && strcmp(key, "access_token") == 0){ cap = sizeof(token); return token; }
            return nullptr;
        }
        void onValue(const char* key, JsonReader::Type, const char* text, size_t len, bool truncated, int depth) override {
            if(depth != 1 || !key) return;
            if(strcmp(key, "access_token") == 0){ token_len = len; token_trunc = truncated; check(text == token, "長い値は差し出した置き場を指す"); }
            if(strcmp(key, "other") == 0){ short_value = text; short_trunc = truncated; }
        }
    };
}

static void testOAuth(){
    //長い値(1000文字。\/ のエスケープ込み)を1バイトずつ
    std::string tok;
    for(int i = 0; i < 1000; i++) tok += (char)('a' + i % 26);
    std::string json = "{\"token_type\":\"Bearer\",\"access_token\":\"" + tok.substr(0, 500) + "\\/" + tok.substr(500)
                     + "\",\"other\":\"" + std::string(300, 'x') + "\",\"expires_in\":3599}";
    LongPick h;
    JsonReader r;
    r.reset(&h);
    for(char c : json) r.feed(&c, 1);
    check(r.complete(), "JsonReader: 読み終える");
    eq_int((long)h.token_len, 1001, "長い値を丸ごと読む");
    check(!h.token_trunc && std::string(h.token) == tok.substr(0, 500) + "/" + tok.substr(500), "長い値の中身(\\/ も戻す)");
    check(h.short_trunc && h.short_value.size() == JsonReader::kValueBytes - 1, "ほかの値はこれまでどおり256Bで切る");

    //置き場より長ければ切って truncated
    std::string big = "{\"access_token\":\"" + std::string(2000, 'z') + "\"}";
    LongPick h2;
    r.reset(&h2);
    r.feed(big.data(), big.size());
    check(r.complete() && h2.token_trunc && h2.token_len == sizeof(h2.token) - 1, "置き場より長い値は切る");

    FixedString<MailOAuth::kBodyCap> body;
    check(MailOAuth::AppendFormValue(body, "https://outlook.office.com/IMAP.AccessAsUser.All offline_access"), "フォーム: 書ける");
    eq_str(body.c_str(), "https%3A%2F%2Foutlook.office.com%2FIMAP.AccessAsUser.All%20offline_access", "フォーム: %XX");
    body.clear();
    MailOAuth::AppendFormValue(body, "M.C5_BAY.0.U.-a!!*$~");
    eq_str(body.c_str(), "M.C5_BAY.0.U.-a%21%21%2A%24~", "フォーム: 記号");

    //更新用トークンの保存
    OSData::SD_usable = true;
    const char* path = PICO_Path::FILE::CFG::SYS_MAIL_TOKEN;
    std::string rt = "M.C5_BAY.0.U.-";
    for(int i = 0; i < 900; i++) rt += (char)('A' + i % 26);
    check(MailOAuth::SaveRefreshToken(rt.c_str()), "トークンを保存できる");
    const std::string saved = HostSd::files[path];
    check(saved.find("M.C5_BAY") == std::string::npos && saved.find("enc") != std::string::npos, "平文では置かない");
    int lines = 0;
    for(char c : saved) if(c == '\n') lines++;
    eq_int(lines, 1 + 4, "255Bずつの行に分ける(見出し+4行)");
    char back[MailOAuth::kRefreshTokenCap];
    bool rewrite = true;
    check(MailOAuth::LoadRefreshToken(back, sizeof(back), &rewrite) && rt == back, "読み戻せる");
    check(!rewrite, "今の鍵で書いたものは書き直さない");
    //行を入れ替えると(用途の文字列が行ごとに違うので)元に戻らない
    std::string swapped = saved;
    const size_t a = swapped.find('\n') + 1, b = swapped.find('\n', a) + 1, c = swapped.find('\n', b) + 1;
    swapped = swapped.substr(0, a) + swapped.substr(b, c - b) + swapped.substr(a, b - a) + swapped.substr(c);
    HostSd::files[path] = swapped;
    check(!MailOAuth::LoadRefreshToken(back, sizeof(back)) || rt != back, "行ごとに鍵ストリームが違う");
    HostSd::files[path] = "# x\nenc1:zz\n";
    check(!MailOAuth::LoadRefreshToken(back, sizeof(back)) && back[0] == '\0', "壊れていれば読まない");
    HostSd::files[path] = "plain-token-123\n";
    check(MailOAuth::LoadRefreshToken(back, sizeof(back), &rewrite) && strcmp(back, "plain-token-123") == 0 && rewrite,
          "平文で置かれたものは読めて、書き直す印が付く");
    MailOAuth::ClearRefreshToken();
    check(HostSd::files.count(path) == 0, "消せる");
    check(!MailOAuth::LoadRefreshToken(back, sizeof(back)), "無ければ読めない");
}

int main(){
    testCharset();
    testHeaders();
    testBody();
    testImap();
    testOAuth();
    if(failures){
        printf("\n%d 件失敗\n", failures);
        return 1;
    }
    printf("\nすべて成功\n");
    return 0;
}
