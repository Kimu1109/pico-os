// XMLの読み取り(util/Xml_Reader)のテスト。run.sh から呼ぶ。
//
// ソケットもSDも使わず、バイト列を直接食わせて
//   - Google News のRSSの形から <item> の title/link/pubDate だけ拾える(CDATA・実体参照・属性・名前空間つき要素)
//   - 1バイトずつ食わせても、まとめて食わせても同じ出来事の並びになる
//   - 実体参照(名前つき/10進/16進/未定義/壊れたもの)、CDATA、コメント、処理命令、DOCTYPE、BOM
//   - 長い文字の切り詰め(UTF-8の途中で切らない)、空白の扱い
//   - 誤り(閉じタグの不一致・深すぎる入れ子・ルートの外の文字・ルート2つ・引用符なしの属性)と、途中で終わった文書
// を見る。
#include "util/Xml_Reader.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}
static void eq_str(const std::string& a, const std::string& e, const char* label){
    const bool ok = (a == e);
    printf("%s %-46s 実測=\"%s\"\n", ok ? "[ OK ]" : "[FAIL]", label, a.c_str());
    if(!ok) failures++;
}

// 出来事を1行ずつ文字列にして並べる
struct Recorder : XmlReader::Handler {
    std::vector<std::string> ev;
    void onOpen(const char* name, int depth) override {
        ev.push_back(std::string("<") + name + "@" + std::to_string(depth));
    }
    void onAttribute(const char* name, const char* value, size_t len, bool truncated, int depth) override {
        ev.push_back(std::string("@") + name + "=" + value + (truncated ? "(切)" : "") + "#" + std::to_string(len) + "@" + std::to_string(depth));
    }
    void onText(const char* name, const char* text, size_t len, bool truncated, int depth) override {
        ev.push_back(std::string("T ") + name + ":" + text + (truncated ? "(切)" : "") + "#" + std::to_string(len) + "@" + std::to_string(depth));
    }
    void onClose(const char* name, int depth) override {
        ev.push_back(std::string(">") + name + "@" + std::to_string(depth));
    }
    std::string joined() const {
        std::string s;
        for(size_t i = 0; i < ev.size(); i++){ if(i) s += "|"; s += ev[i]; }
        return s;
    }
};

// まとめて食わせる
static std::string run(const std::string& xml, bool* complete = nullptr, bool* ok = nullptr, std::string* err = nullptr){
    Recorder r;
    XmlReader x;
    x.reset(&r);
    const bool fed = x.feed(xml.data(), xml.size());
    if(complete) *complete = x.complete();
    if(ok) *ok = fed;
    if(err) *err = x.error();
    return r.joined();
}

// 1バイトずつ食わせる
static std::string runBytes(const std::string& xml){
    Recorder r;
    XmlReader x;
    x.reset(&r);
    for(char c : xml) x.feed(&c, 1);
    return r.joined();
}

// RSSから <item> の title/link だけ拾う(アプリが書く形そのもの)
struct NewsPicker : XmlReader::Handler {
    struct Item { std::string title, link, pub, src_url; };
    std::vector<Item> items;
    bool in_item = false;
    std::string cur;
    void onOpen(const char* name, int depth) override {
        (void)depth;
        cur = name;
        if(strcmp(name, "item") == 0){ in_item = true; items.push_back(Item()); }
    }
    void onAttribute(const char* name, const char* value, size_t, bool, int) override {
        if(in_item && cur == "source" && strcmp(name, "url") == 0) items.back().src_url = value;
    }
    void onText(const char* name, const char* text, size_t, bool, int) override {
        if(!in_item) return;
        if(strcmp(name, "title") == 0) items.back().title = text;
        else if(strcmp(name, "link") == 0) items.back().link = text;
        else if(strcmp(name, "pubDate") == 0) items.back().pub = text;
    }
    void onClose(const char* name, int) override {
        if(strcmp(name, "item") == 0) in_item = false;
    }
};

static const char* kRss =
    "\xEF\xBB\xBF<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<!-- Google News -->\n"
    "<rss xmlns:media=\"http://search.yahoo.com/mrss/\" version=\"2.0\">\n"
    "  <channel>\n"
    "    <generator>NFE/5.0</generator>\n"
    "    <title>\"Raspberry Pi\" - Google ニュース</title>\n"
    "    <item>\n"
    "      <title>Pico 2 W が発売 &amp; 値下げ - 日経</title>\n"
    "      <link>https://news.google.com/rss/articles/CBMi?oc=5&amp;hl=ja</link>\n"
    "      <guid isPermaLink=\"false\">CBMi</guid>\n"
    "      <pubDate>Tue, 14 Oct 2026 03:00:00 GMT</pubDate>\n"
    "      <description>&lt;a href=\"https://x\"&gt;記事&lt;/a&gt;&nbsp;&lt;font color=\"#6f6f6f\"&gt;日経&lt;/font&gt;</description>\n"
    "      <source url=\"https://www.nikkei.com\">日経</source>\n"
    "      <media:content url=\"https://img/1.jpg\" medium=\"image\"/>\n"
    "    </item>\n"
    "    <item>\n"
    "      <title><![CDATA[RP2350 <速報> & 解説]]></title>\n"
    "      <link>https://news.google.com/rss/articles/CBMj</link>\n"
    "      <pubDate>Mon, 13 Oct 2026 21:30:00 GMT</pubDate>\n"
    "    </item>\n"
    "  </channel>\n"
    "</rss>\n";

int main(){
    // ---- 基本 ----
    {
        bool complete = false;
        eq_str(run("<a><b>x</b><c/></a>", &complete),
               "<a@1|<b@2|T b:x#1@2|>b@2|<c@2|>c@2|>a@1", "要素・文字・自己終了");
        check(complete, "ルートを閉じ終えたら complete");
    }
    {
        eq_str(run("<a x=\"1\" y='2 &amp; 3'  z = \"\"/>"),
               "<a@1|@x=1#1@1|@y=2 & 3#5@1|@z=#0@1|>a@1", "属性(引用符2種・実体・空・=の前後の空白)");
    }
    {
        eq_str(run("<a>\n  <b> hello \n world </b>\n</a>"),
               "<a@1|<b@2|T b:hello \n world#13@2|>b@2|>a@1", "前後の空白は落とし、途中の空白は残す");
    }
    {
        eq_str(run("<a> <b/> </a>"), "<a@1|<b@2|>b@2|>a@1", "空白だけの文字は渡さない");
    }
    {
        eq_str(run("<a>前<b/>後</a>"), "<a@1|T a:前#3@1|<b@2|>b@2|T a:後#3@1|>a@1", "子要素で文字を区切る");
    }
    {
        eq_str(run("<a>x<!-- c -->y</a>"), "<a@1|T a:xy#2@1|>a@1", "コメントは文字を区切らない");
    }
    {
        eq_str(run("<media:content a:b=\"1\"/>"), "<media:content@1|@a:b=1#1@1|>media:content@1", "名前空間つきの名前はそのまま");
    }
    {
        eq_str(run("<a   >t</a  >"), "<a@1|T a:t#1@1|>a@1", "タグの中の余分な空白");
    }

    // ---- 実体参照 ----
    {
        eq_str(run("<a>&lt;&gt;&amp;&quot;&apos;</a>"), "<a@1|T a:<>&\"'#5@1|>a@1", "名前つき実体");
        eq_str(run("<a>&#65;&#x3042;&#X1F600;</a>"), "<a@1|T a:Aあ😀#8@1|>a@1", "数値実体(10進・16進・補助平面)");
        eq_str(run("<a>&nbsp;x&foo;</a>"), "<a@1|T a:&nbsp;x&foo;#12@1|>a@1", "未定義の実体は書かれたまま");
        eq_str(run("<a>a & b &#; &#xZ; c</a>"), "<a@1|T a:a & b &#; &#xZ; c#17@1|>a@1", "壊れた実体は書かれたまま");
        eq_str(run("<a>&#0;&#xD800;&#99999999;</a>"), "<a@1|T a:\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD#9@1|>a@1", "範囲外・サロゲート・桁あふれは U+FFFD");
        eq_str(run("<a>&amp</a>"), "<a@1|T a:&amp#4@1|>a@1", "; が来ないまま閉じても落ちない");
    }

    // ---- CDATA ----
    {
        eq_str(run("<a><![CDATA[<b>x</b> & ]] ]]]></a>"), "<a@1|T a:<b>x</b> & ]] ]#15@1|>a@1", "CDATA(] の並びを含む)");
        eq_str(run("<a>1<![CDATA[2]]>3&amp;</a>"), "<a@1|T a:123&#4@1|>a@1", "CDATAと普通の文字が1つの文字になる");
    }

    // ---- 読み飛ばすもの ----
    {
        eq_str(run("<?xml version=\"1.0\"?><?pi a?b?><a/>"), "<a@1|>a@1", "処理命令(中の ? も)");
        eq_str(run("<!DOCTYPE a [ <!ENTITY e \"x>y\"> <!-- c --> ]><a/>"), "<a@1|>a@1", "DOCTYPE(内部サブセット・引用符の中の >)");
        eq_str(run("<!DOCTYPE html><a/>"), "<a@1|>a@1", "単純なDOCTYPE");
        eq_str(run("\xEF\xBB\xBF<a/>"), "<a@1|>a@1", "BOM");
        eq_str(run("<a/>\n<!-- end -->\n"), "<a@1|>a@1", "ルートの後ろの空白とコメント");
    }

    // ---- RSS ----
    {
        NewsPicker p;
        XmlReader x;
        x.reset(&p);
        check(x.feed(kRss, strlen(kRss)), "RSSを読める");
        check(x.complete(), "RSSを最後まで読めた");
        check(p.items.size() == 2, "item が2件");
        if(p.items.size() == 2){
            eq_str(p.items[0].title, "Pico 2 W が発売 & 値下げ - 日経", "1件目の題名(実体)");
            eq_str(p.items[0].link, "https://news.google.com/rss/articles/CBMi?oc=5&hl=ja", "1件目のリンク(&amp;)");
            eq_str(p.items[0].pub, "Tue, 14 Oct 2026 03:00:00 GMT", "1件目の日時");
            eq_str(p.items[0].src_url, "https://www.nikkei.com", "1件目の source の url 属性");
            eq_str(p.items[1].title, "RP2350 <速報> & 解説", "2件目の題名(CDATA)");
            eq_str(p.items[1].link, "https://news.google.com/rss/articles/CBMj", "2件目のリンク");
        }
        // description の実体は1段だけ戻る(中のHTMLの < > はそのまま文字)
        Recorder r;
        XmlReader x2;
        x2.reset(&r);
        x2.feed(kRss, strlen(kRss));
        bool found = false;
        for(const auto& e : r.ev){
            if(e.rfind("T description:<a href=\"https://x\">記事</a>&nbsp;<font color=\"#6f6f6f\">日経</font>#", 0) == 0) found = true;
        }
        check(found, "description の実体が1段だけ戻る");

        // 1バイトずつでも、7バイトずつでも同じ並び
        const std::string whole = run(kRss);
        eq_str(runBytes(kRss) == whole ? "同じ" : "違う", "同じ", "RSSを1バイトずつ食わせても同じ");
        Recorder r7;
        XmlReader x7;
        x7.reset(&r7);
        const size_t n = strlen(kRss);
        for(size_t i = 0; i < n; i += 7) x7.feed(kRss + i, (n - i < 7) ? n - i : 7);
        check(r7.joined() == whole, "RSSを7バイトずつ食わせても同じ");
    }

    // ---- 切り詰め ----
    {
        std::string longtext(400, 'a');
        const std::string out = run("<a>" + longtext + "</a>");
        const size_t expect = XmlReader::kValueBytes - 1;
        eq_str(out.substr(0, 9), "<a@1|T a:", "長い文字の頭");
        check(out.find("(切)#" + std::to_string(expect) + "@1") != std::string::npos, "255バイトで切れて truncated が立つ");

        // 日本語(3バイト)で境目をまたがせる: 85文字=255バイト、先頭に1バイト足して境目を文字の途中にする
        std::string jp = "x";
        for(int i = 0; i < 100; i++) jp += "あ";
        const std::string o2 = run("<a>" + jp + "</a>");
        // 255バイト目で切れると 'x' + 84文字(253B)+ 2バイトの端 → 端を落として253バイト
        check(o2.find("(切)#253@1") != std::string::npos, "UTF-8の途中では切らない");

        // 属性値も同じ
        const std::string o3 = run("<a v=\"" + longtext + "\"/>");
        check(o3.find("(切)#255@1") != std::string::npos, "長い属性値も切れる");

        // 長い名前は切り詰めて、開きと閉じが同じ切り詰め方で合う
        const std::string name(60, 'n');
        bool ok = false, complete = false;
        run("<" + name + "></" + name + ">", &complete, &ok);
        check(ok && complete, "長い名前でも開きと閉じが合う");
    }

    // ---- 誤り ----
    {
        bool complete, ok; std::string err;
        run("<a><b></a>", &complete, &ok, &err);
        check(!ok && !complete, "閉じタグの不一致は誤り");
        run("</a>", &complete, &ok, &err);
        check(!ok, "開いていない要素を閉じると誤り");
        run("hello<a/>", &complete, &ok, &err);
        check(!ok, "ルートの外の文字は誤り");
        run("<a/><b/>", &complete, &ok, &err);
        check(!ok, "ルートが2つは誤り");
        run("<a x=1/>", &complete, &ok, &err);
        check(!ok, "引用符なしの属性値は誤り");
        run("<a x/>", &complete, &ok, &err);
        check(!ok, "値のない属性は誤り");
        run("<a x=\"<\"/>", &complete, &ok, &err);
        check(!ok, "属性値の中の < は誤り");
        run("<a><1/></a>", &complete, &ok, &err);
        check(!ok, "数字で始まる要素名は誤り");
        run("<a/ >", &complete, &ok, &err);
        check(!ok, "/ の後ろが > でなければ誤り");
        run("<![CDATA[x]]><a/>", &complete, &ok, &err);
        check(!ok, "ルートの外のCDATAは誤り");
        run("<!x>", &complete, &ok, &err);
        check(!ok, "<! の後ろが読めなければ誤り");

        std::string deep;
        for(int i = 0; i < XmlReader::kMaxDepth; i++) deep += "<a>";
        run(deep, &complete, &ok, &err);
        check(ok, "kMaxDepth段までは読める");
        run(deep + "<a>", &complete, &ok, &err);
        check(!ok, "kMaxDepth段を超えると誤り");

        run("<a><b>text", &complete, &ok, &err);
        check(ok && !complete, "途中で終わった文書は complete にならない");
        run("", &complete, &ok, &err);
        check(ok && !complete, "空の入力は complete にならない");

        // 誤りの後は何もしない
        Recorder r;
        XmlReader x;
        x.reset(&r);
        x.feed("<a></b>", 7);
        const size_t before = r.ev.size();
        check(x.failed(), "failed()");
        check(!x.feed("<c/>", 4) && r.ev.size() == before, "誤りの後は何も渡さない");
        x.reset(&r);
        check(!x.failed() && x.feed("<a/>", 4) && x.complete(), "reset で読み直せる");
    }

    // ---- 乱数で壊したバイト列でも落ちない(ASan/UBSanに見てもらう) ----
    {
        unsigned seed = 12345;
        auto rnd = [&seed]() { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
        const size_t n = strlen(kRss);
        int fed_ok = 0;
        for(int round = 0; round < 300; round++){
            std::string s(kRss, n);
            const int edits = 1 + (int)(rnd() % 6);
            for(int e = 0; e < edits; e++) s[rnd() % s.size()] = (char)(rnd() & 0xFF);
            Recorder r;
            XmlReader x;
            x.reset(&r);
            if(x.feed(s.data(), s.size())) fed_ok++;
        }
        printf("[ OK ] 壊したRSSを300回読んでも落ちない(誤りなしで読めたのは%d回)\n", fed_ok);
    }

    printf("\n%s (%d件失敗)\n", failures == 0 ? "全て成功" : "失敗あり", failures);
    return failures == 0 ? 0 : 1;
}
