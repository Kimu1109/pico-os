// Wi-Fi転送の取り決めと守り(net/File_Share)のテスト。ソケットもSDも使わない。
//
// 要求の見出しの読み取り(細切れに届く・本文の頭を残す・大きすぎる/壊れた要求・Content-Lengthの読み方)、
// どの要求か(Classify)、%XX・query・クッキーの読み方、パスの確かめ(.. で外へ出ない・/sys を断る・
// FATで使えない名前)、Host の確かめ、キーの比べ方と作り方、総当たりを遅くする数え方、応答の見出し・一覧の1行を確かめる。
// ソケット越しの往復(アップロード・ダウンロード・削除等)はPCビルドで curl とブラウザから確かめた(docs/design/file-share.md)。
#include "net/File_Share.hpp"

#include <cstdio>
#include <cstring>
#include <string>

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}
static void eqStr(const char* actual, const char* expected, const char* label){
    const bool ok = (strcmp(actual, expected) == 0);
    printf("%s %s\n", ok ? "[ OK ]" : "[FAIL]", label);
    if(!ok){
        printf("       実測: [%s]\n       期待: [%s]\n", actual, expected);
        failures++;
    }
}

using namespace FileShare;

// s を chunk バイトずつ食わせる。Done になった時点で、残り(本文の頭)を body に返す
static RequestHead::State FeedAll(RequestHead& r, const std::string& s, size_t chunk, std::string* body = nullptr){
    r.reset();
    RequestHead::State st = RequestHead::State::Reading;
    for(size_t i = 0; i < s.size(); i += chunk){
        const size_t n = (s.size() - i < chunk) ? s.size() - i : chunk;
        size_t used = 0;
        st = r.feed((const uint8_t*)s.data() + i, n, used);
        if(st != RequestHead::State::Reading){
            if(body) *body = s.substr(i + used);
            return st;
        }
    }
    return st;
}

static void TestRequestHead(){
    static RequestHead r; // 約4.5KBあるのでスタックに置かない
    const std::string put =
        "PUT /f/music/%E6%9B%B2.wav?x=1 HTTP/1.1\r\n"
        "Host: picoos.local\r\n"
        "x-pico-key:  abcd2345 \r\n"
        "Cookie: a=1; pk=abcd2345\r\n"
        "Content-Length: 11\r\n"
        "\r\n"
        "hello world";
    for(size_t chunk : {1, 7, 4096}){
        std::string body;
        const auto st = FeedAll(r, put, chunk, &body);
        char label[64];
        snprintf(label, sizeof(label), "見出し: %zuバイトずつでも読める", chunk);
        check(st == RequestHead::State::Done, label);
        check(body == "hello world", "見出し: 空行の後ろ(本文の頭)は食べずに返す");
    }
    check(r.method() == Method::Put, "見出し: PUT");
    eqStr(r.path(), "/f/music/%E6%9B%B2.wav", "見出し: パスは ? より前で %XX のまま");
    eqStr(r.query(), "x=1", "見出し: query");
    eqStr(r.host(), "picoos.local", "見出し: Host");
    eqStr(r.key(), "abcd2345", "見出し: 名前の大文字小文字を問わず、値の前後の空白を除く");
    eqStr(r.cookie(), "a=1; pk=abcd2345", "見出し: Cookie");
    check(r.hasLength() && r.contentLength() == 11, "見出し: Content-Length");

    check(FeedAll(r, "GET / HTTP/1.1\r\nHost: x\r\n\r\n", 3) == RequestHead::State::Done && r.method() == Method::Get &&
          !r.hasLength() && r.query()[0] == '\0', "見出し: GET(本文なし)");
    check(FeedAll(r, "HEAD /f/a HTTP/1.1\r\n\r\n", 5) == RequestHead::State::Done && r.method() == Method::Head, "見出し: HEAD");
    check(FeedAll(r, "DELETE /f/a HTTP/1.1\r\n\r\n", 5) == RequestHead::State::Done && r.method() == Method::Other, "見出し: 知らない方法は Other");
    check(FeedAll(r, "GET / HTTP/1.1\r\nHost: x\r\n", 4) == RequestHead::State::Reading, "見出し: 空行まではまだ読む");

    std::string big = "GET / HTTP/1.1\r\nX-Pad: " + std::string(kMaxHead, 'a') + "\r\n\r\n";
    check(FeedAll(r, big, 100) == RequestHead::State::TooLarge, "見出し: 2KBを超えたら TooLarge");
    check(FeedAll(r, "garbage\r\n\r\n", 64) == RequestHead::State::Bad, "見出し: 要求行が壊れていたら Bad");
    check(FeedAll(r, "GET nopath HTTP/1.1\r\n\r\n", 64) == RequestHead::State::Bad, "見出し: パスが / で始まらなければ Bad");
    check(FeedAll(r, "PUT /f/a HTTP/1.1\r\nContent-Length: 12x\r\n\r\n", 64) == RequestHead::State::Bad, "見出し: 数でない Content-Length は Bad");
    check(FeedAll(r, "PUT /f/a HTTP/1.1\r\nContent-Length: 99999999999999999999999\r\n\r\n", 64) == RequestHead::State::Bad,
          "見出し: 桁あふれの Content-Length は Bad");
    check(FeedAll(r, "PUT /f/a HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n", 64) == RequestHead::State::Bad,
          "見出し: chunked は読まない(Bad)");
    check(FeedAll(r, "PUT /f/a HTTP/1.1\r\nContent-Length: 5000000000\r\n\r\n", 64) == RequestHead::State::Done &&
          r.contentLength() == 5000000000ull, "見出し: 4GBを超える Content-Length も読める");
}

static void TestClassify(){
    const char* fp = nullptr;
    check(Classify(Method::Get, "/", &fp) == Route::Page, "Classify: / はページ");
    check(Classify(Method::Head, "/index.html", &fp) == Route::Page, "Classify: HEAD /index.html もページ");
    check(Classify(Method::Post, "/", &fp) == Route::MethodNotAllowed, "Classify: POST / は405");
    check(Classify(Method::Get, "/f/a/b.txt", &fp) == Route::Download && strcmp(fp, "/a/b.txt") == 0, "Classify: GET /f/... はダウンロード(/f の後ろを返す)");
    check(Classify(Method::Put, "/f/a.txt", &fp) == Route::Upload && strcmp(fp, "/a.txt") == 0, "Classify: PUT /f/... はアップロード");
    check(Classify(Method::Post, "/f/a.txt", &fp) == Route::MethodNotAllowed, "Classify: POST /f/... は405");
    check(Classify(Method::Get, "/api/ls", &fp) == Route::List, "Classify: GET /api/ls");
    check(Classify(Method::Get, "/api/info", &fp) == Route::Info, "Classify: GET /api/info");
    check(Classify(Method::Post, "/api/mkdir", &fp) == Route::Mkdir, "Classify: POST /api/mkdir");
    check(Classify(Method::Post, "/api/rm", &fp) == Route::Remove, "Classify: POST /api/rm");
    check(Classify(Method::Post, "/api/mv", &fp) == Route::Move, "Classify: POST /api/mv");
    check(Classify(Method::Get, "/api/rm", &fp) == Route::MethodNotAllowed, "Classify: GET /api/rm は405(書き換えはPOSTだけ)");
    check(Classify(Method::Get, "/api/lsx", &fp) == Route::NotFound, "Classify: 知らない場所は404");
    check(Classify(Method::Get, "/favicon.ico", &fp) == Route::NotFound, "Classify: favicon は404");
    check(IsMutating(Route::Upload) && IsMutating(Route::Move) && !IsMutating(Route::List) && !IsMutating(Route::Download),
          "IsMutating: 書き換えだけ");
}

static void TestStrings(){
    char out[64];
    check(PercentDecode("a%20b%E3%81%82", 14, false, out, sizeof(out)) && strcmp(out, "a b\xE3\x81\x82") == 0, "PercentDecode: %XX を戻す");
    check(PercentDecode("a+b", 3, false, out, sizeof(out)) && strcmp(out, "a+b") == 0, "PercentDecode: パスの + はそのまま");
    check(PercentDecode("a+b", 3, true, out, sizeof(out)) && strcmp(out, "a b") == 0, "PercentDecode: query の + は空白");
    check(!PercentDecode("a%2", 3, false, out, sizeof(out)), "PercentDecode: 途中で切れた % は失敗");
    check(!PercentDecode("a%zz", 4, false, out, sizeof(out)), "PercentDecode: 16進でない % は失敗");
    check(!PercentDecode("a%00b", 5, false, out, sizeof(out)), "PercentDecode: NUL は失敗");
    check(!PercentDecode("abcdef", 6, false, out, 4), "PercentDecode: 入らなければ失敗");

    check(QueryValue("p=%2Fmusic%2Fa+b.wav&to=x", "p", out, sizeof(out)) && strcmp(out, "/music/a b.wav") == 0, "QueryValue: 戻して返す");
    check(QueryValue("p=1&to=%2Fx", "to", out, sizeof(out)) && strcmp(out, "/x") == 0, "QueryValue: 2つ目");
    check(!QueryValue("pp=1", "p", out, sizeof(out)), "QueryValue: 名前の頭が同じだけでは合わない");
    check(QueryValue("dl", "dl", out, sizeof(out)) && out[0] == '\0', "QueryValue: = の無い名前は空の値");
    check(!QueryValue("", "p", out, sizeof(out)), "QueryValue: 無ければ false");

    check(CookieValue("a=1; pk=abcd2345; b=2", "pk", out, sizeof(out)) && strcmp(out, "abcd2345") == 0, "CookieValue: 間のクッキー");
    check(CookieValue("pk=x", "pk", out, sizeof(out)) && strcmp(out, "x") == 0, "CookieValue: 1つだけ");
    check(!CookieValue("xpk=1", "pk", out, sizeof(out)), "CookieValue: 名前の後ろが同じだけでは合わない");
    check(!CookieValue("pk=0123456789", "pk", out, 4), "CookieValue: 入らなければ false");

    FixedString<PICO_STR_256B> enc;
    AppendPercentEncoded(enc, "日本 a-b.txt");
    eqStr(enc.c_str(), "%E6%97%A5%E6%9C%AC%20a-b.txt", "AppendPercentEncoded: 非予約文字以外を %XX に");
    FixedString<PICO_STR_256B> json;
    AppendJsonEscaped(json, "a\"b\\c\n");
    eqStr(json.c_str(), "a\\\"b\\\\c\\u000a", "AppendJsonEscaped: \" \\ 制御文字");
}

static void TestPaths(){
    FixedString<PICO_PATH_LEN> p;
    check(CheckPath("/music/a.wav", p) == PathCheck::Ok && p == "/music/a.wav", "CheckPath: ふつうのパス");
    check(CheckPath("music//a.wav", p) == PathCheck::Ok && p == "/music/a.wav", "CheckPath: / 始まりにして // をまとめる");
    check(CheckPath("/", p) == PathCheck::Ok && p == "/", "CheckPath: ルート");
    check(CheckPath("/../../etc/passwd", p) == PathCheck::Ok && p == "/etc/passwd", "CheckPath: ルートを超える .. は捨てる(SDの外へ出ない)");
    check(CheckPath("/sys", p) == PathCheck::Forbidden, "CheckPath: /sys は断る");
    check(CheckPath("/sys/network.cfg", p) == PathCheck::Forbidden, "CheckPath: /sys の中も断る");
    check(CheckPath("/SYS/network.cfg", p) == PathCheck::Forbidden, "CheckPath: 大文字の /SYS も断る(FATは区別しない)");
    check(CheckPath("/a/../sys/x", p) == PathCheck::Forbidden, "CheckPath: 畳んだ結果が /sys なら断る");
    check(CheckPath("/system/x", p) == PathCheck::Ok, "CheckPath: /system は別のフォルダ");
    check(CheckPath("/a/b:c", p) == PathCheck::Bad, "CheckPath: FATで使えない文字は Bad");
    check(CheckPath("/a/b.", p) == PathCheck::Bad, "CheckPath: 末尾の . は Bad");
    check(CheckPath("/a/b\\c", p) == PathCheck::Bad, "CheckPath: \\ は Bad");
    check(CheckPath(std::string(300, 'a').c_str(), p) == PathCheck::Bad, "CheckPath: 長すぎれば Bad");

    const char* raw = "/music/%E6%9B%B2.wav";
    check(PathFromUrl(raw, strlen(raw), p) == PathCheck::Ok && p == "/music/\xE6\x9B\xB2.wav", "PathFromUrl: %XX を戻して確かめる");
    raw = "/%73ys/x";
    check(PathFromUrl(raw, strlen(raw), p) == PathCheck::Forbidden, "PathFromUrl: %XX で隠した /sys も断る");
    raw = "/a%2F..%2F..%2Fsys";
    check(PathFromUrl(raw, strlen(raw), p) == PathCheck::Forbidden, "PathFromUrl: %2F と .. を組み合わせても /sys は断る");
    raw = "/a%00b";
    check(PathFromUrl(raw, strlen(raw), p) == PathCheck::Bad, "PathFromUrl: NUL は Bad");
    check(IsSystemPath("/sys") && IsSystemPath("/Sys/x") && !IsSystemPath("/sysx") && !IsSystemPath("/a/sys"), "IsSystemPath");
}

static void TestGuards(){
    check(HostAllowed("picoos.local", "picoos", "192.168.1.23", 80), "Host: <名前>.local");
    check(HostAllowed("PicoOS.Local", "picoos", "192.168.1.23", 80), "Host: 大文字小文字を問わない");
    check(HostAllowed("picoos", "picoos", "", 80), "Host: 名前だけ");
    check(HostAllowed("192.168.1.23", "picoos", "192.168.1.23", 80), "Host: IP");
    check(HostAllowed("192.168.1.23:80", "picoos", "192.168.1.23", 80), "Host: 待ち受けと同じ番号付き");
    check(!HostAllowed("192.168.1.23:8080", "picoos", "192.168.1.23", 80), "Host: 違う番号は断る");
    check(!HostAllowed("evil.example", "picoos", "192.168.1.23", 80), "Host: よその名前は断る(DNSリバインディング)");
    check(!HostAllowed("picoos.local.evil.example", "picoos", "192.168.1.23", 80), "Host: 頭が同じだけの名前は断る");
    check(!HostAllowed("192.168.1.2", "picoos", "192.168.1.23", 80), "Host: 違うIPは断る");
    check(!HostAllowed("", "picoos", "192.168.1.23", 80), "Host: 無ければ断る");
    check(!HostAllowed("picoos.local:", "picoos", "", 80), "Host: : の後ろが空なら断る");

    check(KeyEquals("abcd2345", "abcd2345"), "KeyEquals: 同じ");
    check(!KeyEquals("abcd2346", "abcd2345"), "KeyEquals: 1文字違い");
    check(!KeyEquals("abcd234", "abcd2345") && !KeyEquals("abcd23456", "abcd2345"), "KeyEquals: 長さ違い");
    check(!KeyEquals("", ""), "KeyEquals: 空どうしは合わない(キーを作る前)");

    uint8_t rnd[kKeyLen] = {0, 1, 30, 31, 62, 200, 255, 7};
    char key[kKeyLen + 1];
    MakeKey(rnd, key);
    bool all_in = strlen(key) == kKeyLen;
    for(size_t i = 0; i < kKeyLen; i++) all_in = all_in && strchr(kKeyAlphabet, key[i]) != nullptr;
    check(all_in, "MakeKey: 8文字で、紛らわしくない字だけ");
    eqStr(key, "23z22g99", "MakeKey: 乱数を字の数で割った余り");

    Throttle t;
    for(int i = 0; i < Throttle::kMaxFailures - 1; i++) t.fail(1000);
    check(!t.blocked(1000), "Throttle: 4回まではまだ断らない");
    t.fail(1000);
    check(t.blocked(1000) && t.blocked(1000 + Throttle::kLockMs - 1), "Throttle: 5回目で30秒断る");
    check(!t.blocked(1000 + Throttle::kLockMs), "Throttle: 30秒たてば戻る");
    t.fail(40000); t.success(); t.fail(40000);
    for(int i = 0; i < 3; i++) t.fail(40000);
    check(!t.blocked(40000), "Throttle: 合えば数え直す");
}

static void TestResponse(){
    char head[512];
    FormatHeader(201, nullptr, 0, nullptr, head, sizeof(head));
    eqStr(head, "HTTP/1.1 201 Created\r\nContent-Length: 0\r\nCache-Control: no-store\r\n"
                "X-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n", "FormatHeader: 本文なし");
    FormatHeader(200, "text/plain", -1, "X-A: 1\r\n", head, sizeof(head));
    eqStr(head, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nCache-Control: no-store\r\n"
                "X-Content-Type-Options: nosniff\r\nConnection: close\r\nX-A: 1\r\n\r\n", "FormatHeader: 長さなし(閉じて終わり)と追加の見出し");
    FormatHeader(200, "text/plain", 5000000000ll, nullptr, head, sizeof(head));
    check(strstr(head, "Content-Length: 5000000000\r\n") != nullptr, "FormatHeader: 4GBを超える長さ");
    char tiny[24];
    FormatHeader(200, "text/plain", 1, nullptr, tiny, sizeof(tiny));
    check(strlen(tiny) < sizeof(tiny), "FormatHeader: 入らなくてもはみ出さない");

    eqStr(ContentType("a.TXT"), "text/plain; charset=utf-8", "ContentType: 大文字の拡張子");
    eqStr(ContentType("a.png"), "image/png", "ContentType: png");
    eqStr(ContentType("index.html"), "text/plain; charset=utf-8", "ContentType: SDのHTMLはページとして動かさない");
    eqStr(ContentType("noext"), "application/octet-stream", "ContentType: 拡張子なし");

    FixedString<64> list;
    check(AppendListLine(list, "a.txt", false, 12) && AppendListLine(list, "dir", true, 99), "AppendListLine: 足せる");
    eqStr(list.c_str(), "a.txt\tf\t12\ndir\td\t0\n", "AppendListLine: 名前\\t種類\\t大きさ(フォルダは0)");
    check(!AppendListLine(list, std::string(40, 'x').c_str(), false, 1) && list == "a.txt\tf\t12\ndir\td\t0\n",
          "AppendListLine: 入りきらなければ何も足さない");

    check(PageHtmlLength() == strlen(kPageHtml) && strstr(kPageHtml, "X-Pico-Key") != nullptr, "ページ: 長さと、キーの見出しを送ること");
    check(strstr(kPageHtml, "innerHTML") == nullptr, "ページ: SDの名前を innerHTML へ入れない");
}

int main(){
    TestRequestHead();
    TestClassify();
    TestStrings();
    TestPaths();
    TestGuards();
    TestResponse();
    printf("\n%s(失敗 %d)\n", failures == 0 ? "すべて成功" : "失敗あり", failures);
    return failures == 0 ? 0 : 1;
}
