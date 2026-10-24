// AP設定モードのページ(net/Setup_Portal)のテスト。ソケットは使わず、要求のバイト列を食わせて応答を見る。
//
// 要求の読み取り(細切れに届く・Content-Lengthまで待つ・大きすぎる/壊れた要求)、フォームの値の
// 復号(+ と %XX)、ページの出し分け(どのGETも入力のページ・POST /save で保存の結果)、HTMLへの
// 埋め込みの文字参照、どの名前にも本体のアドレスを答えるDNSの応答を確かめる。
// APとHTTPの往復はPCビルドで localhost のページへ curl で送って確かめた(docs/design/security-wifi.md)。
#include "net/Setup_Portal.hpp"

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

using namespace SetupPortal;

static std::string saved_ssid_arg, saved_pass_arg;
static SaveResult next_result = SaveResult::Saved;
static SaveResult FakeSave(const char* ssid, const char* pass){
    saved_ssid_arg = ssid;
    saved_pass_arg = pass;
    return next_result;
}

static Request::State FeedAll(Request& r, const std::string& s, size_t chunk){
    r.reset();
    Request::State st = Request::State::Reading;
    for(size_t i = 0; i < s.size(); i += chunk){
        st = r.feed(s.data() + i, std::min(chunk, s.size() - i));
        if(st != Request::State::Reading) break;
    }
    return st;
}

int main(){
    static Request req;     // 2KB強あるのでスタックに置かない
    static Response body;
    FixedString<PICO_STR_M> saved;

    // ---- 要求の読み取り ----
    {
        const std::string get = "GET /generate_204?x=1 HTTP/1.1\r\nHost: a\r\n\r\n";
        check(FeedAll(req, get, 3) == Request::State::Done, "細切れのGETを読める");
        check(!req.isPost(), "GET");
        eqStr(req.path(), "/generate_204", "パス(クエリは落とす)");

        const std::string head_only = "POST /save HTTP/1.1\r\nContent-Length: 10\r\n\r\nssid=";
        check(FeedAll(req, head_only, 64) == Request::State::Reading, "本文が揃うまで待つ");
        check(req.feed("abcde", 5) == Request::State::Done, "Content-Lengthで揃う");
        eqStr(req.body(), "ssid=abcde", "本文");

        check(FeedAll(req, "BREW /pot HTTP/1.1\r\n\r\n", 64) == Request::State::Bad, "知らないメソッドは壊れた要求");
        const std::string big = "POST /save HTTP/1.1\r\ncontent-length: 5000\r\n\r\n";
        check(FeedAll(req, big, 64) == Request::State::TooLarge, "大きすぎる本文の申告");
        check(FeedAll(req, std::string(3000, 'a'), 512) == Request::State::TooLarge, "改行の無い長い要求");
        check(Respond(req, &FakeSave, "picoos", body, saved) == 413, "大きすぎれば413");
    }

    // ---- フォームの値 ----
    {
        char out[32];
        check(FormValue("a=1&ssid=Cafe+%26+Co&pass=", "ssid", out, sizeof(out)), "値を見つける");
        eqStr(out, "Cafe & Co", "+ と %XX を戻す");
        check(FormValue("a=1&ssid=x&pass=", "pass", out, sizeof(out)) && out[0] == '\0', "空の値");
        check(!FormValue("a=1", "ssid", out, sizeof(out)), "無いキー");
        check(!FormValue("ssidx=1", "ssid", out, sizeof(out)), "前方一致では拾わない");
        check(FormValue("ssid=%E3%81%82%zz", "ssid", out, sizeof(out)), "壊れた%はそのまま");
        eqStr(out, "あ%zz", "UTF-8を戻し、壊れた%は残す");
        char tiny[4];
        check(!FormValue("ssid=abcdef", "ssid", tiny, sizeof(tiny)), "入りきらなければfalse");
    }

    // ---- ページ ----
    {
        FeedAll(req, "GET /hotspot-detect.html HTTP/1.1\r\n\r\n", 64);
        check(Respond(req, &FakeSave, "pi<co>", body, saved) == 200, "どのGETも200");
        check(strstr(body.c_str(), "action=\"/save\"") != nullptr, "入力のページ");
        check(strstr(body.c_str(), "pi&lt;co&gt;") != nullptr, "本体の名前は文字参照にして埋める");
        check(saved.empty(), "GETでは保存しない");

        next_result = SaveResult::Saved;
        const std::string post_body = "ssid=%3Cb%3EHome&pass=p%40ss+word";
        const std::string post = "POST /save HTTP/1.1\r\nContent-Length: " + std::to_string(post_body.size()) + "\r\n\r\n" + post_body;
        FeedAll(req, post, 7);
        check(Respond(req, &FakeSave, "picoos", body, saved) == 200, "POST /save");
        eqStr(saved_ssid_arg.c_str(), "<b>Home", "保存へ渡すSSID");
        eqStr(saved_pass_arg.c_str(), "p@ss word", "保存へ渡すパスワード");
        eqStr(saved.c_str(), "<b>Home", "保存できたSSIDを返す");
        check(strstr(body.c_str(), "&lt;b&gt;Home") != nullptr && strstr(body.c_str(), "<b>Home") == nullptr,
              "SSIDはページへ文字参照で埋める");

        next_result = SaveResult::Full;
        FeedAll(req, post, 64);
        Respond(req, &FakeSave, "picoos", body, saved);
        check(saved.empty() && strstr(body.c_str(), "いっぱい") != nullptr, "いっぱいなら保存しない旨");

        next_result = SaveResult::Saved;
        saved_ssid_arg = "unchanged";
        const std::string empty_ssid = "POST /save HTTP/1.1\r\nContent-Length: 10\r\n\r\nssid=&pass";
        FeedAll(req, empty_ssid, 64);
        Respond(req, &FakeSave, "picoos", body, saved);
        check(saved.empty() && saved_ssid_arg == "unchanged", "空のSSIDは保存へ渡さない");

        char head[200];
        FormatHeader(200, 1234, head, sizeof(head));
        check(strstr(head, "HTTP/1.1 200 OK\r\n") == head && strstr(head, "Content-Length: 1234\r\n") != nullptr
              && strstr(head, "\r\n\r\n") != nullptr, "ヘッダ");
    }

    // ---- DNS ----
    {
        // ID=0x1234, RD, 質問1つ: connectivitycheck.gstatic.com A IN
        std::string q("\x12\x34\x01\x00\x00\x01\x00\x00\x00\x00\x00\x00", 12);
        q += std::string("\x11") + "connectivitycheck" + std::string("\x07") + "gstatic" + std::string("\x03") + "com" + std::string("\x00", 1);
        q += std::string("\x00\x01\x00\x01", 4);
        uint8_t out[600];
        const uint32_t ip = 0xC0A82A01u; // 192.168.42.1
        const size_t n = BuildDnsReply((const uint8_t*)q.data(), q.size(), ip, out, sizeof(out));
        check(n == q.size() + 16, "Aの質問に1つ答える");
        check(out[0] == 0x12 && out[1] == 0x34, "IDを写す");
        check((out[2] & 0x80) && (out[2] & 0x01) && out[3] == 0x80, "応答の印・RD・RA");
        check(out[7] == 1, "ANCOUNT=1");
        check(n >= 4 && out[n - 4] == 192 && out[n - 3] == 168 && out[n - 2] == 42 && out[n - 1] == 1, "本体のアドレス");

        std::string aaaa = q;
        aaaa[aaaa.size() - 3] = 28; // AAAA
        const size_t n2 = BuildDnsReply((const uint8_t*)aaaa.data(), aaaa.size(), ip, out, sizeof(out));
        check(n2 == aaaa.size() && out[7] == 0, "AAAAには答えの無い成功");

        std::string resp = q;
        resp[2] = (char)0x81;
        check(BuildDnsReply((const uint8_t*)resp.data(), resp.size(), ip, out, sizeof(out)) == 0, "応答には答えない");
        check(BuildDnsReply((const uint8_t*)q.data(), 20, ip, out, sizeof(out)) == 0, "途中で切れた質問には答えない");
        check(BuildDnsReply((const uint8_t*)q.data(), q.size(), ip, out, 20) == 0, "書く場所が足りなければ答えない");
    }

    printf("\n%s (失敗 %d 件)\n", failures == 0 ? "全て成功" : "失敗あり", failures);
    return failures == 0 ? 0 : 1;
}
