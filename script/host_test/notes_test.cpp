// メモ・全文検索・最近開いたファイルのテスト。run.sh から呼ぶ。
//
//   - TextMatcher: 大文字/小文字・チャンクの境目をまたぐ一致・KMPの巻き戻し・行番号・抜粋
//   - NoteParser: 題名(見出しの印を落とす)・#タグ の拾い方(行頭/空白の後だけ・句読点で終わる・数字だけ/見出しは外す)
//   - RecentFiles: 新しい順・重ならない・上限・忘れる・名前の変更/フォルダの移動
//   - FileSearchScene(Mode::Content): 中身で探す・/sys/ と文字でないファイルは見ない・行番号と抜粋
//   - NotesScene: /notes/ の読み込み・題名・タグの数・タグ/検索の絞り込み・最近開いた順・新しいメモの名前
#include "util/Text_Matcher.hpp"
#include "apps/notes/Note_Parse.hpp"
#include "apps/notes/NotesScene.hpp"
#include "storage/Recent_Files.hpp"
#include "storage/SD_Path.hpp"
#include "gui/scenes/FileSearchScene.hpp"
#include "gui/scenes/FileViewerScene.hpp"
#include "gui/core/Widget_Functions.hpp"
#include "gui/core/Scene_Functions.hpp"
#include "gui/core/Keyboard_Functions.hpp"
#include "gui/core/GFX_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ---- モック ----
void PICO_GFX::MarkDirty(const Rect&){}
void PICO_GFX::Setup(){}
void PICO_GFX::FlushDirty(){}
void PICO_GFX::DrawDialogBackground(){}
void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}

static int push_calls = 0;
static std::string pushed_name;
void SceneFunctions::Pop(){}
void SceneFunctions::Push(Scene* s){ push_calls++; pushed_name = s->getName(); delete s; }

void FileViewerScene::onEnter(){}
void FileViewerScene::onExit(){}
void FileViewerScene::onUpdate(){}
bool FileViewerScene::onKey(const KeyInputFunctions::Event&){ return false; }
uint32_t FileViewerScene::preferredSampleRate() const { return 0; }


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

// ---------------------------------------------------------------- TextMatcher

static void testMatcher(){
    TextMatcher m;
    check(!m.setNeedle(""), "照合: 空の検索語は断る");
    check(!m.setNeedle(std::string(TextMatcher::kMaxNeedle + 1, 'a').c_str()), "照合: 長すぎる検索語は断る");

    check(m.setNeedle("Hello"), "照合: 検索語を決める");
    check(m.feed("say hELLo", 9), "照合: 大文字/小文字を区別しない");
    eq_int(m.firstPos(), 4, "照合: 最初の一致の位置");

    // チャンクの境目をまたぐ
    m.reset();
    check(!m.feed("xxHel", 5), "照合: 途中まではまだ");
    check(m.feed("loyy", 4), "照合: 境目をまたいで一致");
    eq_int(m.firstPos(), 2, "照合: 境目をまたいだ一致の位置");

    // KMPの巻き戻し("aab" を "aaab" の中で)
    m.setNeedle("aab");
    check(m.feed("aaab", 4), "照合: 失敗しても途中から続ける");
    eq_int(m.firstPos(), 1, "照合: aaab の中の aab");

    // 数と行
    m.setNeedle("会議");
    const char* text = "1行目\n2行目 会議\n3行目\n会議と会議";
    m.feed(text, (int)strlen(text));
    eq_int(m.count(), 3, "照合: 一致の数");
    eq_int(m.firstLine(), 2, "照合: 最初の一致の行");

    // 1バイトずつ流しても同じ
    m.reset();
    for(const char* p = text; *p; p++) m.feed(p, 1);
    eq_int(m.count(), 3, "照合: 1バイトずつでも同じ数");

    // 抜粋
    char out[64];
    const char* buf = "前の行\n  \tここに会議の話\n次の行";
    TextMatcher::MakeSnippet(buf, (int)strlen(buf), (int)(strstr(buf, "会議") - buf), out, sizeof(out));
    eq_str(out, "ここに会議の話", "抜粋: その行だけ・頭の空白を落とす");
    // バッファの頭が文字の途中
    const char* mid = "\x81\x82あいう";   // 継続バイト2つから始まる
    TextMatcher::MakeSnippet(mid, (int)strlen(mid), 5, out, sizeof(out));
    eq_str(out, "あいう", "抜粋: 文字の途中から始めない");
    // 入りきらなければ文字の境目で切る
    TextMatcher::MakeSnippet("あいうえお", 15, 0, out, 8);
    eq_str(out, "あい", "抜粋: 文字の途中で切らない");
}

// ---------------------------------------------------------------- NoteParser

struct Tags { std::vector<std::string> v; };
static void CollectTag(void* ctx, const char* tag){ static_cast<Tags*>(ctx)->v.push_back(tag); }

static std::string Join(const std::vector<std::string>& v){
    std::string s;
    for(size_t i = 0; i < v.size(); i++){ if(i) s += ","; s += v[i]; }
    return s;
}

static void parse(const char* text, std::string& title, std::string& tags, int chunk = 0){
    NoteParser p;
    Tags t;
    p.begin(&CollectTag, &t);
    const int n = (int)strlen(text);
    if(chunk <= 0){
        p.feed(text, n);
    }else{
        for(int i = 0; i < n; i += chunk) p.feed(text + i, (i + chunk <= n) ? chunk : n - i);
    }
    p.finish();
    title = p.title();
    tags = Join(t.v);
}

static void testParser(){
    std::string title, tags;
    parse("\n\n  # 買い物リスト\n牛乳 #買い物 #家\n", title, tags);
    eq_str(title, "買い物リスト", "題名: 見出しの印と頭の空行を落とす");
    eq_str(tags, "買い物,家", "タグ: 日本語のタグ");

    parse("会議メモ #work/2026、#todo。#done\n", title, tags);
    eq_str(title, "会議メモ #work/2026、#todo。#done", "題名: 最初の行そのまま");
    eq_str(tags, "work/2026,todo,done", "タグ: 、。の後ろの#も拾う");

    parse("x #a、y #b。 #c） #d\t#e", title, tags);
    eq_str(tags, "a,b,c,d,e", "タグ: 句読点・括弧・タブで終わる");

    parse("C#とpage#topと #1 と ## 見出しと #-x と #ok-", title, tags);
    eq_str(tags, "-x,ok", "タグ: C#・URLの#・数字だけ・見出しは外す、末尾の-は落とす");

    parse("全角空白の後\xE3\x80\x80#全角後 #長" "あああああああああああああ" " #短い", title, tags);
    eq_str(tags, "全角後,短い", "タグ: 全角空白の後は拾う・長すぎるものは外す");

    parse("#先頭タグ だけの行\n本文", title, tags);
    eq_str(title, "#先頭タグ だけの行", "題名: #タグで始まる行は#も題名に");
    eq_str(tags, "先頭タグ", "タグ: 文書の頭のタグ");

    // 1バイトずつ流しても同じ
    parse("# 題\n本文 #タグ1 と #tag_2、終わり #末尾", title, tags, 1);
    eq_str(title, "題", "1バイトずつ: 題名");
    eq_str(tags, "タグ1,tag_2,末尾", "1バイトずつ: タグ(ファイルの終わりで締める)");

    // 長い題名は文字の途中で切らない
    std::string longline;
    for(int i = 0; i < 40; i++) longline += "あ";
    parse(longline.c_str(), title, tags);
    eq_int((long)title.size(), 63, "題名: 63バイトまで(3バイトの文字21個)");

    int len = 6;
    check(NoteParser::TrimTag("abc、", len) && len == 3, "TrimTag: 全角の記号で切る");
    len = 3;
    check(!NoteParser::TrimTag("123", len), "TrimTag: 数字だけは外す");
}

// ---------------------------------------------------------------- RecentFiles

static std::vector<std::string> listRecent(){
    FixedString<PICO_PATH_LEN> out[RecentFiles::kMaxFiles];
    const int n = RecentFiles::List(out, RecentFiles::kMaxFiles);
    std::vector<std::string> v;
    for(int i = 0; i < n; i++) v.push_back(out[i].c_str());
    return v;
}

static void testRecent(){
    HostSd::files.clear();
    HostSd::dirs_enabled = true;
    OSData::SD_usable = true;
    check(listRecent().empty(), "最近: 無ければ0件");
    RecentFiles::Touch("/a.txt");
    RecentFiles::Touch("/b.txt");
    RecentFiles::Touch("/a.txt");
    eq_str(Join(listRecent()), "/a.txt,/b.txt", "最近: 新しい順で重ならない");
    check(!RecentFiles::Touch("/sys/wifi.cfg"), "最近: /sys/ は覚えない");
    check(!RecentFiles::Touch("relative.txt"), "最近: 絶対パスだけ");

    for(int i = 0; i < RecentFiles::kMaxFiles + 5; i++){
        char p[32];
        snprintf(p, sizeof(p), "/f%02d.txt", i);
        RecentFiles::Touch(p);
    }
    const auto v = listRecent();
    eq_int((long)v.size(), RecentFiles::kMaxFiles, "最近: 上限まで");
    eq_str(v.front(), "/f24.txt", "最近: 先頭が一番新しい");

    RecentFiles::Forget("/f24.txt");
    eq_str(listRecent().front(), "/f23.txt", "最近: 忘れる");

    RecentFiles::Touch("/docs/x/y.md");
    RecentFiles::Touch("/docs/z.md");
    RecentFiles::Touch("/docsother.md");
    RecentFiles::Rename("/docs", "/archive/docs");
    const auto r = listRecent();
    eq_str(r[0] + "," + r[1] + "," + r[2], "/docsother.md,/archive/docs/z.md,/archive/docs/x/y.md",
           "最近: フォルダの移動に付いていく(名前が似ているだけのものは触らない)");
    RecentFiles::Rename("/docsother.md", "/renamed.md");
    eq_str(listRecent()[0], "/renamed.md", "最近: 名前の変更に付いていく");
}

// ---------------------------------------------------------------- 全文検索

static void runSearch(FileSearchScene& s){
    s.onEnter();
    for(int i = 0; i < 1000 && !s.finished(); i++) s.onUpdate();
}

static void testContentSearch(){
    HostSd::files.clear();
    HostSd::dirs_enabled = true;
    OSData::SD_usable = true;
    HostSd::files["/notes/a.txt"] = "一行目\n二行目に Pico の話\n";
    HostSd::files["/notes/b.md"] = "nothing here";
    HostSd::files["/books/long.txt"] = std::string(5000, 'x') + "\n終わりのほうに pico\n";
    HostSd::files["/pico.txt"] = "名前だけ一致して中身は違う";
    HostSd::files["/sys/secret.cfg"] = "pico = 1";
    HostSd::files["/img.bmp"] = "pico";

    FileSearchScene s("/", "PICO", FileSearchScene::Mode::Content);
    runSearch(s);
    check(s.finished(), "全文: 終わる");
    eq_int(s.resultCount(), 2, "全文: 中身に含む文字のファイルだけ(/sys/・画像・名前だけは外す)");
    bool saw_a = false, saw_long = false;
    for(int i = 0; i < s.resultCount(); i++){
        if(strcmp(s.resultAt(i), "/notes/a.txt") == 0){
            saw_a = true;
            eq_int(s.resultLine(i), 2, "全文: 行番号");
            eq_str(s.resultSnippet(i), "二行目に Pico の話", "全文: その行の抜粋");
        }
        if(strcmp(s.resultAt(i), "/books/long.txt") == 0){
            saw_long = true;
            eq_str(s.resultSnippet(i), "終わりのほうに pico", "全文: 何回にも分けて読んだファイルの抜粋");
        }
    }
    check(saw_a && saw_long, "全文: どちらも見つかる");
    s.onExit();
    WidgetFunctions::ClearSceneWidgets();

    FileSearchScene n("/", "pico", FileSearchScene::Mode::Name);
    runSearch(n);
    eq_int(n.resultCount(), 1, "名前の検索は今までどおり名前だけ");
    n.onExit();
    WidgetFunctions::ClearSceneWidgets();
}

// ---------------------------------------------------------------- NotesScene

static void runNotes(NotesScene& s){
    // 検索語を変えた後は、次の onUpdate() で読み直しが始まる
    int i = 0;
    do { s.onUpdate(); } while(++i < 1000 && s.scanning());
}

static std::string shownTitles(const NotesScene& s){
    std::string r;
    for(int i = 0; i < s.shownCount(); i++){
        if(i) r += ",";
        r += s.shownAt(i)->title.c_str();
    }
    return r;
}

static void testNotes(){
    HostSd::files.clear();
    HostSd::dirs_enabled = true;
    OSData::SD_usable = true;
    HostSd::files["/notes/2026-10-01_0900.txt"] = "# 会議\n予算の話 #仕事\n";
    HostSd::files["/notes/2026-10-02_0900.txt"] = "買い物\n牛乳 #家 #買い物\n";
    HostSd::files["/notes/2026-10-03_0900.md"] = "企画メモ #仕事 #Idea\n予算は来月\n";
    HostSd::files["/notes/sub/old.txt"] = "古いメモ #idea\n";
    HostSd::files["/notes/empty.txt"] = "";
    HostSd::files["/notes/image.bmp"] = "#仕事";
    HostSd::files["/memo.txt"] = "外のメモ #仕事";

    NotesScene s;
    s.onEnter();
    runNotes(s);
    check(!s.scanning(), "メモ: 読み終わる");
    eq_int(s.index()->count, 5, "メモ: /notes/ の下の .txt/.md だけ(下のフォルダも)");
    // 並びは名前の新しい順
    eq_str(shownTitles(s), "古いメモ #idea,empty,企画メモ #仕事 #Idea,買い物,会議", "メモ: 名前の新しい順・空なら名前が題名");

    // タグ(#Idea と #idea は同じ)
    int work = -1, idea = -1;
    for(int i = 0; i < s.index()->tag_count; i++){
        const auto& t = s.index()->tags[i];
        if(strcmp(t.name.c_str(), "仕事") == 0) work = t.count;
        if(strcasecmp(t.name.c_str(), "idea") == 0) idea = t.count;
    }
    eq_int(s.index()->tag_count, 4, "メモ: タグの種類(仕事・家・買い物・Idea)");
    eq_int(work, 2, "メモ: #仕事 は2件");
    eq_int(idea, 2, "メモ: #Idea と #idea は同じタグ");

    s.setTagFilter("仕事");
    eq_str(shownTitles(s), "企画メモ #仕事 #Idea,会議", "メモ: タグで絞り込む");

    // 検索(読み直してから)。タグと重ねる
    s.setQuery("予算");
    runNotes(s);
    eq_str(shownTitles(s), "企画メモ #仕事 #Idea,会議", "メモ: タグ+全文検索");
    s.setTagFilter("");
    s.setQuery("牛乳");
    runNotes(s);
    eq_str(shownTitles(s), "買い物", "メモ: 全文検索だけ");
    eq_int(s.shownAt(0)->hits, 1, "メモ: 出てきた数");
    s.setQuery("");
    runNotes(s);

    // 最近開いたものが先頭
    RecentFiles::Touch("/notes/2026-10-01_0900.txt");
    RecentFiles::Touch("/notes/2026-10-02_0900.txt");
    s.onExit();
    WidgetFunctions::ClearSceneWidgets();
    s.onEnter();
    runNotes(s);
    eq_str(shownTitles(s), "買い物,会議,古いメモ #idea,empty,企画メモ #仕事 #Idea", "メモ: 最近開いた順が先");

    // 開く → エディタ
    push_calls = 0;
    s.setTagFilter("家");
    eq_int(s.shownCount(), 1, "メモ: #家 は1件");
    s.onExit();
    WidgetFunctions::ClearSceneWidgets();

    // 消えたタグの絞り込みは読み直しで解ける
    HostSd::files.erase("/notes/2026-10-02_0900.txt");
    s.onEnter();
    runNotes(s);
    eq_int(s.shownCount(), 4, "メモ: 無くなったタグの絞り込みは解く");
    s.onExit();
    WidgetFunctions::ClearSceneWidgets();

    // /notes/ が無い
    HostSd::files.clear();
    NotesScene e;
    e.onEnter();
    runNotes(e);
    eq_int(e.shownCount(), 0, "メモ: /notes/ が無ければ0件");
    e.onExit();
    WidgetFunctions::ClearSceneWidgets();

    // 新しいメモの名前(時計が合っていなければ memo.txt。重なれば番号)
    FixedString<PICO_PATH_LEN> p;
    check(NotesScene::MakeNewNotePath(p) && strncmp(p.c_str(), "/notes/", 7) == 0, "新規: /notes/ の中");
    HostSd::files[p.c_str()] = "";
    FixedString<PICO_PATH_LEN> p2;
    check(NotesScene::MakeNewNotePath(p2) && !(p2 == p), "新規: 重ならない名前");
    check(NotesScene::IsNoteFile("a.MD") && !NotesScene::IsNoteFile(".hidden.txt") && !NotesScene::IsNoteFile("a.bmp"),
          "新規: メモの拡張子");
}

int main(){
    testMatcher();
    testParser();
    testRecent();
    testContentSearch();
    testNotes();
    if(failures){
        printf("\n%d 件失敗\n", failures);
        return 1;
    }
    printf("\nすべて成功\n");
    return 0;
}
