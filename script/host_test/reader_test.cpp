// 読書アプリの中身(apps/reader/ の Aozora_Text・Reader_Layout・Reader_Book・Reader_Store)のテスト。run.sh から呼ぶ。
//
//   - 青空文庫の形: Shift_JIS/UTF-8の見分け・題名と著者・冒頭の記号の説明を飛ばす・ルビ・｜・外字(U+ / 第3水準)・注記
//   - ページの組み方: 全ページをつなぐと本文の文字が欠けも重なりもせず並ぶ・行の幅・行頭禁則・字下げ・地付き・改ページ
//   - 索引: 次へ/前へで行き来して同じページになる・ページ番号・割合で飛ぶ・間引き(1024ページ超)・目次
//   - 保存: 続きの位置としおりの読み書き・最近読んだ順・忘れる
// 本物の青空文庫のファイルはリポジトリの pc/sdcard/books/蜘蛛の糸.txt(Shift_JIS・CRLF)を使う。
#include "apps/reader/Aozora_Text.hpp"
#include "apps/reader/Reader_Layout.hpp"
#include "apps/reader/Reader_Book.hpp"
#include "apps/reader/Reader_Store.hpp"
#include "storage/SD_Path.hpp"
#include "system/Log_Functions.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

void LogFunctions::Log(LogType, const char*, ...){}

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
static void eq_str(const std::string& a, const std::string& e, const char* label){
    const bool ok = a == e;
    printf("%s %-46s 実測=\"%s\"\n", ok ? "[ OK ]" : "[FAIL]", label, a.c_str());
    if(!ok){
        printf("        期待=\"%s\"\n", e.c_str());
        failures++;
    }
}

class MemSource : public Aozora::Source {
    public:
        explicit MemSource(std::string d) : data(std::move(d)) {}
        uint32_t size() const override { return (uint32_t)data.size(); }
        size_t readAt(uint32_t off, uint8_t* dst, size_t n) override {
            reads++;
            if(off >= data.size()) return 0;
            if(n > data.size() - off) n = data.size() - off;
            memcpy(dst, data.data() + off, n);
            return n;
        }
        std::string data;
        int reads = 0;
};

// 半角は8px、それ以外は16px
static int FixedMeasure(uint32_t cp, void*){ return cp < 0x80 ? 8 : 16; }

static ReaderLayout::Params MakeParams(int width, int lines, bool ruby = false){
    ReaderLayout::Params p;
    p.width = width;
    p.lines = lines;
    p.em = 16;
    p.ruby = ruby;
    p.measure = &FixedMeasure;
    return p;
}

static std::string Utf8(uint32_t cp){
    char u[4];
    const int n = Aozora::EncodeUtf8(cp, u);
    return std::string(u, n);
}

// 本文の文字だけを順に(ルビ・注記・改行を除く)
static std::string AllChars(Aozora::Reader& r){
    std::string s;
    uint32_t pos = 0;
    Aozora::Token t;
    for(;;){
        r.next(pos, t);
        if(t.kind == Aozora::Tok::Eof) break;
        if(t.kind == Aozora::Tok::Char) s += Utf8(t.cp);
    }
    return s;
}

// 行の文字列からルビを除いたもの
static std::string StripRuby(const char* s){
    std::string out;
    bool in = false;
    for(; *s; s++){
        if(*s == ReaderLayout::kRubyOn){ in = true; continue; }
        if(*s == ReaderLayout::kRubyOff){ in = false; continue; }
        if(!in) out += *s;
    }
    return out;
}

static uint32_t FirstCp(const char* s){
    const uint8_t* p = (const uint8_t*)s;
    if(p[0] < 0x80) return p[0];
    if(p[0] < 0xE0) return ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
    if(p[0] < 0xF0) return ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    return ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
}

static int Width(const std::string& s){
    int w = 0;
    for(size_t i = 0; i < s.size();){
        const uint8_t b = (uint8_t)s[i];
        int len = b < 0x80 ? 1 : b < 0xE0 ? 2 : b < 0xF0 ? 3 : 4;
        w += FixedMeasure(FirstCp(s.c_str() + i), nullptr);
        i += len;
    }
    return w;
}

static std::string ReadFile(const char* path){
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// ---- 青空文庫の形 ----
static void testAozoraFile(){
    printf("\n--- 青空文庫のファイル(蜘蛛の糸) ---\n");
    MemSource src(ReadFile("pc/sdcard/books/蜘蛛の糸.txt"));
    check(src.size() > 5000, "サンプルの本が読める");
    Aozora::Info info;
    Aozora::Analyze(src, info);
    check(info.enc == Aozora::Encoding::ShiftJis, "Shift_JISと見分ける");
    eq_str(info.title.c_str(), "蜘蛛の糸", "題名");
    eq_str(info.author.c_str(), "芥川龍之介", "著者");
    check(info.notes_end > info.notes_begin && info.notes_begin > 0, "記号の説明の範囲が見つかる");

    Aozora::Reader r(src, info);
    const std::string all = AllChars(r);
    check(all.find("テキスト中に現れる記号について") == std::string::npos, "記号の説明は読み飛ばす");
    check(all.find("《") == std::string::npos && all.find("》") == std::string::npos, "ルビの括弧は本文に出ない");
    check(all.find("｜") == std::string::npos, "ルビの始まりの｜は出ない");
    check(all.find("［＃") == std::string::npos, "注記は本文に出ない");
    check(all.find("〓陀多") != std::string::npos, "第3水準の外字は〓");
    check(all.find("蜘蛛の糸芥川龍之介ある日の事でございます") == std::string::npos &&
          all.find("ある日の事でございます。御釈迦様は極楽の蓮池のふち") != std::string::npos, "ルビを除いた本文");

    // 字句: ルビと注記の中身
    uint32_t pos = info.notes_end;
    Aozora::Token t;
    int headings = 0, rubies = 0, indents = 0, aligns = 0;
    std::string first_ruby;
    for(;;){
        r.next(pos, t);
        if(t.kind == Aozora::Tok::Eof) break;
        if(t.kind == Aozora::Tok::Ruby){
            if(rubies++ == 0) first_ruby.assign(t.text, t.len);
        }
        if(t.kind == Aozora::Tok::Note){
            if(t.note == Aozora::Note::Heading) headings++;
            if(t.note == Aozora::Note::Indent && t.num == 8) indents++;
            if(t.note == Aozora::Note::AlignBottom && t.num == 1) aligns++;
        }
    }
    eq_str(first_ruby, "おしゃかさま", "最初のルビ");
    eq_int(headings, 3, "見出しの注記");
    eq_int(indents, 3, "８字下げ");
    eq_int(aligns, 1, "地から１字上げ");

    // ページを全部つなぐと本文と同じ
    ReaderBook book(src);
    book.setParams(MakeParams(224, 13, false));
    std::string joined;
    int pages = 0;
    bool width_ok = true, kinsoku_ok = true, lines_ok = true;
    int first_line_x = -1;
    for(;;){
        const ReaderLayout::Page& pg = book.page();
        if(pg.count > 13) lines_ok = false;
        for(int i = 0; i < pg.count; i++){
            const ReaderLayout::Line& L = pg.lines[i];
            const std::string s = StripRuby(L.text);
            joined += s;
            if(L.x + Width(s) > 224) width_ok = false;
            if(s.size() > 3 && ReaderLayout::IsNoLineStart(FirstCp(s.c_str()))){
                kinsoku_ok = false;
                printf("        行頭禁則: p%d \"%s\"\n", pages, s.c_str());
            }
            if(first_line_x < 0 && s == "一") first_line_x = L.x;
        }
        pages++;
        if(!book.next()) break;
    }
    eq_str(joined == all ? "same" : joined.substr(0, 60), "same", "全ページをつなぐと本文と同じ(欠け・重なり無し)");
    check(width_ok, "どの行も幅に収まる");
    check(lines_ok, "1ページの行数を超えない");
    check(kinsoku_ok, "行頭に禁則の文字が来ない");
    eq_int(first_line_x, 8 * 16, "８字下げの見出しの位置");
    check(book.atEnd(), "最後のページで終わり");

    // 索引: 全ページ数・前へで同じページへ戻る
    while(!book.indexStep(8)) {}
    eq_int(book.pageCount(), pages, "索引のページ数");
    eq_int(book.pageNumber(), pages - 1, "最後のページの番号");
    std::vector<uint32_t> fwd;
    book.goToRatio(0.0f);
    do { fwd.push_back(book.current().pos); } while(book.next());
    bool back_ok = true;
    for(int i = (int)fwd.size() - 1; i >= 0; i--){
        if(book.current().pos != fwd[i]){ back_ok = false; break; }
        if(book.pageNumber() != i){ back_ok = false; break; }
        if(i > 0 && !book.prev()){ back_ok = false; break; }
    }
    check(back_ok, "前へで同じページの頭を逆順にたどる");
    check(!book.prev(), "最初のページより前は無い");

    // 目次
    const ReaderLayout::Toc& toc = book.toc();
    eq_int(toc.count(), 3, "目次の数");
    if(toc.count() == 3){
        eq_str(toc.at(0).title, "一", "目次1");
        eq_str(toc.at(2).title, "三", "目次3");
        book.goToPos(toc.at(1).pos);
        std::string top;
        for(int i = 0; i < book.page().count; i++) top += StripRuby(book.page().lines[i].text);
        check(top.find("二") != std::string::npos, "目次から「二」のページへ飛ぶ");
    }

    // 割合
    book.goToRatio(0.5f);
    const int mid = book.pageNumber();
    check(mid > 0 && mid < pages - 1, "半分の位置へ飛ぶ");
    book.goToRatio(1.0f);
    eq_int(book.pageNumber(), pages - 1, "最後へ飛ぶ");

    // ルビを出すと、親文字の後ろに(かんじ)
    ReaderBook book2(src);
    book2.setParams(MakeParams(224, 13, true));
    std::string raw;
    for(int i = 0; i < book2.page().count; i++) raw += book2.page().lines[i].text;
    check(raw.find(std::string("御釈迦様") + ReaderLayout::kRubyOn + "(おしゃかさま)" + ReaderLayout::kRubyOff) != std::string::npos,
          "ルビを親文字の後ろへ");
    // ルビを出しても: 行の頭にルビだけ・行頭禁則の文字が来ない、本文の文字は欠けない
    bool ruby_head_ok = true, ruby_kinsoku_ok = true;
    std::string joined2;
    for(;;){
        const ReaderLayout::Page& pg = book2.page();
        for(int i = 0; i < pg.count; i++){
            const char* t = pg.lines[i].text;
            joined2 += StripRuby(t);
            if(t[0] == ReaderLayout::kRubyOn){ ruby_head_ok = false; printf("        ルビが行頭: \"%s\"\n", t); }
            if(strlen(t) > 3 && ReaderLayout::IsNoLineStart(FirstCp(t))){ ruby_kinsoku_ok = false; printf("        行頭禁則: \"%s\"\n", t); }
        }
        if(!book2.next()) break;
    }
    check(ruby_head_ok, "ルビだけが行の頭へ行かない(親文字ごと送る)");
    check(ruby_kinsoku_ok, "ルビを出しても行頭に禁則の文字が来ない");
    check(joined2 == all, "ルビを出しても本文の文字が欠けない");
}

// ---- 組み方の細かい所(UTF-8の短い文) ----
static void testLayoutDetails(){
    printf("\n--- 組み方 ---\n");
    // BOM・UTF-8・外字(U+)
    {
        MemSource src("\xEF\xBB\xBF" "あ※［＃「てへん＋劣」、U+6318、140-2］い\n");
        Aozora::Info info;
        Aozora::Analyze(src, info);
        check(info.enc == Aozora::Encoding::Utf8 && info.body_start == 3, "BOM付きUTF-8");
        Aozora::Reader r(src, info);
        eq_str(AllChars(r), "あ" + Utf8(0x6318) + "い", "外字のU+を文字にする");
        check(info.title.empty(), "青空文庫の形でなければ題名は空(ファイル名を使う)");
    }
    // 行頭禁則(追い出し): 幅4文字で「あいうえ。」
    {
        MemSource src("あいうえ。かき\n");
        ReaderBook book(src);
        book.setParams(MakeParams(64, 5));
        const ReaderLayout::Page& pg = book.page();
        eq_int(pg.count, 2, "追い出しで2行");
        if(pg.count == 2){
            eq_str(pg.lines[0].text, "あいう", "1行目");
            eq_str(pg.lines[1].text, "え。かき", "2行目(句点の前の字ごと送る)");
        }
    }
    // 行末禁則: 開き括弧で行が終わらない
    {
        MemSource src("あいう「えお」\n");
        ReaderBook book(src);
        book.setParams(MakeParams(64, 5));
        const ReaderLayout::Page& pg = book.page();
        if(pg.count >= 2){
            eq_str(pg.lines[0].text, "あいう", "開き括弧の前で折る");
            eq_str(pg.lines[1].text, "「えお」", "開き括弧は次の行へ");
        }else{
            check(false, "行末禁則で2行");
        }
    }
    // 字下げ・ここから字下げ・地付き・改ページ
    {
        MemSource src("［＃２字下げ］あいうえおか\n"
                      "［＃ここから１字下げ］\n"
                      "きく\n"
                      "［＃ここで字下げ終わり］\n"
                      "［＃地付き］けこ\n"
                      "さ\n"
                      "［＃改ページ］\n"
                      "し\n");
        ReaderBook book(src);
        book.setParams(MakeParams(96, 10));   // 6字
        const ReaderLayout::Page& pg = book.page();
        std::vector<std::string> texts;
        std::vector<int> xs;
        for(int i = 0; i < pg.count; i++){ texts.push_back(pg.lines[i].text); xs.push_back(pg.lines[i].x); }
        // あいうえ / おか(段落の字下げを引き継ぐ) / 空行(ここから) / きく / 空行(終わり) / けこ / さ
        if(texts.size() >= 7){
            eq_str(texts[0], "あいうえ", "２字下げの1行目");
            eq_int(xs[0], 32, "２字下げの位置");
            eq_str(texts[1], "おか", "折り返しも字下げのまま");
            eq_int(xs[1], 32, "折り返しの位置");
            eq_str(texts[3], "きく", "ここから１字下げ");
            eq_int(xs[3], 16, "ここから１字下げの位置");
            eq_str(texts[5], "けこ", "地付き");
            eq_int(xs[5], 96 - 32, "地付きは右へ寄せる");
            eq_str(texts[6], "さ", "字下げ終わり");
            eq_int(xs[6], 0, "字下げ終わりの位置");
        }else{
            check(false, "字下げの行数");
        }
        eq_int(pg.count, 7, "改ページで終わる");
        check(book.next(), "改ページの次のページ");
        check(book.page().count >= 1 && strcmp(book.page().lines[0].text, "し") == 0, "改ページの後の行がページの頭");
        check(book.atEnd(), "最後のページ");
    }
    // 見出しの範囲の形(ここから〜見出し)
    {
        MemSource src("［＃ここから中見出し］　第一章［＃ここで中見出し終わり］\n本文\n");
        ReaderBook book(src);
        book.setParams(MakeParams(96, 10));
        while(!book.indexStep(4)) {}
        eq_int(book.toc().count(), 1, "範囲の見出しを拾う");
        if(book.toc().count() == 1) eq_str(book.toc().at(0).title, "第一章", "範囲の見出しの文字(頭の空白を落とす)");
    }
}

// ---- 長い本: 索引の間引き ----
static void testLongBook(){
    printf("\n--- 長い本(索引の間引き) ---\n");
    std::string text;
    for(int i = 0; i < 2600; i++){
        char line[64];
        snprintf(line, sizeof(line), "第%d行あいうえおかきくけこ\n", i);
        text += line;
    }
    MemSource src(text);
    ReaderBook book(src);
    book.setParams(MakeParams(224, 1));   // 1行1ページ
    std::vector<uint32_t> fwd;
    do { fwd.push_back(book.current().pos); } while(book.next());
    while(!book.indexStep(100)) {}
    eq_int(book.pageCount(), (long)fwd.size(), "2600ページを数える");
    bool ok = true;
    for(int n : {0, 1, 999, 1024, 1025, 2047, 2048, 2599}){
        if(n >= (int)fwd.size()) continue;
        book.goTo(ReaderLayout::State{});
        ReaderLayout::State s;
        s.pos = fwd[n];
        book.goTo(s);
        if(book.pageNumber() != n){ ok = false; printf("        p%d → %d\n", n, book.pageNumber()); }
        if(n > 0){
            book.prev();
            if(book.current().pos != fwd[n - 1]){ ok = false; printf("        prev p%d\n", n); }
        }
    }
    check(ok, "間引いた後もページ番号と前へが合う");

    // 途中のページから開いて(索引が無い)前へ
    ReaderBook b2(src);
    b2.setParams(MakeParams(224, 1));
    ReaderLayout::State s;
    s.pos = fwd[1500];
    b2.goTo(s);
    eq_int(b2.pageNumber(), -1, "索引が届くまでページ番号は不明");
    check(b2.prev() && b2.current().pos == fwd[1499], "索引が無くても前へ(その場で作る)");
    eq_int(b2.pageNumber(), 1499, "前へ後のページ番号");
}

// ---- 保存 ----
static void testStore(){
    printf("\n--- 続きの位置としおり ---\n");
    OSData::SD_usable = true;
    HostSd::files.clear();

    ReaderStore::State at;
    at.pos = 1234;
    at.block = 2;
    at.para = 1;
    at.flags = ReaderLayout::kMid;
    ReaderStore::Mark marks[2];
    marks[0].at.pos = 100;
    marks[0].label.assign("ある日の\t事");
    marks[1].at.pos = 900;
    marks[1].label.assign("二");
    check(ReaderStore::SaveBook("/books/a.txt", "本A", 5000, at, marks, 2), "本Aを保存");
    ReaderStore::State none;
    check(ReaderStore::SaveBook("/books/b.txt", "本B", 800, none, nullptr, 0), "本Bを保存");

    ReaderStore::State got;
    ReaderStore::Mark gm[ReaderStore::kMaxMarks];
    int gn = 0;
    check(ReaderStore::LoadBook("/books/a.txt", got, gm, gn), "本Aを読む");
    check(got == at, "続きの位置(字下げごと)");
    eq_int(gn, 2, "しおりの数");
    if(gn == 2){
        eq_int(gm[1].at.pos, 900, "しおりの位置");
        eq_str(gm[0].label.c_str(), "ある日の 事", "タブは空白にする");
    }
    check(!ReaderStore::LoadBook("/books/none.txt", got, gm, gn) && gn == 0 && got.pos == 0, "知らない本");

    ReaderStore::BookRecord list[8];
    int n = ReaderStore::ListBooks(list, 8);
    eq_int(n, 2, "本の数");
    if(n == 2) eq_str(list[0].path.c_str(), "/books/b.txt", "最近読んだ本が先頭");

    // Aをもう一度保存すると先頭へ(しおりは書いたものに置き換わる)
    at.pos = 2000;
    check(ReaderStore::SaveBook("/books/a.txt", "本A", 5000, at, marks, 1), "本Aを再保存");
    n = ReaderStore::ListBooks(list, 8);
    if(n == 2){
        eq_str(list[0].path.c_str(), "/books/a.txt", "保存した本が先頭へ");
        eq_int(list[0].at.pos, 2000, "一覧の位置");
        eq_int(list[0].size, 5000, "一覧の大きさ");
        eq_str(list[0].title.c_str(), "本A", "一覧の題名");
    }
    ReaderStore::LoadBook("/books/a.txt", got, gm, gn);
    eq_int(gn, 1, "しおりを減らす");

    check(ReaderStore::ForgetBook("/books/a.txt"), "本Aを忘れる");
    n = ReaderStore::ListBooks(list, 8);
    eq_int(n, 1, "忘れた後の本の数");
    check(!ReaderStore::LoadBook("/books/a.txt", got, gm, gn) && gn == 0, "忘れた本のしおりも消える");

    // 上限
    for(int i = 0; i < ReaderStore::kMaxBooks + 5; i++){
        char p[32];
        snprintf(p, sizeof(p), "/books/%02d.txt", i);
        ReaderStore::SaveBook(p, p, 10, none, marks, 1);
    }
    ReaderStore::BookRecord many[ReaderStore::kMaxBooks + 10];
    n = ReaderStore::ListBooks(many, ReaderStore::kMaxBooks + 10);
    eq_int(n, ReaderStore::kMaxBooks, "覚える本の上限");
    if(n > 0) eq_str(many[0].path.c_str(), "/books/44.txt", "上限でも最新が先頭");
    check(!ReaderStore::LoadBook("/books/00.txt", got, gm, gn) && gn == 0, "古い本はしおりごと忘れる");
    check(HostSd::files.count(std::string(PICO_Path::FILE::SYS_READER_BOOKS) + ".tmp") == 0, "一時ファイルを残さない");

    // 見出し
    FixedString<64> label;
    ReaderStore::MakeLabel("\xE3\x80\x80御釈迦様\x01(おしゃかさま)\x02は極楽の", label);
    eq_str(label.c_str(), "御釈迦様は極楽の", "しおりの見出しはルビと頭の空白を落とす");

    // 設定
    ReaderStore::Settings st = ReaderStore::LoadSettings();
    check(!st.large_font && st.ruby, "設定の既定値");
    st.large_font = true;
    st.ruby = false;
    ReaderStore::SaveSettings(st);
    ReaderStore::Settings st2 = ReaderStore::LoadSettings();
    check(st2.large_font && !st2.ruby, "設定の読み書き");
}

int main(){
    testAozoraFile();
    testLayoutDetails();
    testLongBook();
    testStore();
    printf("\n%s (失敗 %d)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
