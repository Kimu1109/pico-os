// URLの分解/解決(util/Url.hpp)と、HTTPレスポンスの解釈(net/Http_Response)のテスト。
//
// HttpResponseはソケットを持たず、受信したバイト列を食わせるだけの作りにしてある。
// おかげでネットワーク無しに全経路を確かめられる。1バイトずつ食わせても同じ結果に
// なることまで見ておく(実際の受信は届いた分だけ細切れに来るため)。
#include "util/Url.hpp"
#include "net/Http_Response.hpp"

#include <cstdio>
#include <cstring>
#include <string>

static int failures = 0;

static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}
static void eq_str(const char* actual, const char* expected, const char* label){
    const bool ok = (strcmp(actual, expected) == 0);
    printf("%s %-46s 実測=%-28s 期待=%s\n", ok ? "[ OK ]" : "[FAIL]", label, actual, expected);
    if(!ok) failures++;
}
static void eq_int(long actual, long expected, const char* label){
    const bool ok = (actual == expected);
    printf("%s %-46s 実測=%ld 期待=%ld\n", ok ? "[ OK ]" : "[FAIL]", label, actual, expected);
    if(!ok) failures++;
}

// 受け取った本文をそのまま溜めるシンク
class BufferSink : public IHttpSink {
    public:
        std::string data;
        bool accept = true;
        bool write(const void* p, size_t n) override {
            if(!accept) return false;
            data.append((const char*)p, n);
            return true;
        }
};

// レスポンスを1回で食わせる / 1バイトずつ食わせる の2通りで回す
static void feedAll(HttpResponse& res, const std::string& raw, bool byteByByte){
    if(byteByByte){
        for(char c : raw) res.feed(&c, 1);
    }else{
        res.feed(raw.data(), raw.size());
    }
}

int main(){
    printf("---- Url::Parse ----\n");
    {
        Url u;
        check(UrlTools::Parse(u, "http://192.168.1.10/docs/intro.md"), "基本形を解釈できる");
        eq_str(u.host.c_str(), "192.168.1.10", "ホスト");
        eq_int(u.port, 80, "既定ポートは80");
        eq_str(u.path.c_str(), "/docs/intro.md", "パス");
        check(u.query.empty(), "クエリは空");
        check(!u.secure, "httpならsecureはfalse");

        check(UrlTools::Parse(u, "http://example.test:8080/a/b.md?q=x&n=1"), "ポートとクエリ付き");
        eq_int(u.port, 8080, "ポートを読む");
        eq_str(u.path.c_str(), "/a/b.md", "パスとクエリを分けて持つ");
        eq_str(u.query.c_str(), "q=x&n=1", "クエリ");

        check(UrlTools::Parse(u, "http://example.test"), "パス無しでも解釈できる");
        eq_str(u.path.c_str(), "/", "パス無しは \"/\" になる");

        check(UrlTools::Parse(u, "http://example.test/a/./b/../c.md"), "\".\"と\"..\"を含むパス");
        eq_str(u.path.c_str(), "/a/c.md", "解釈の時点で畳んでおく");

        check(UrlTools::Parse(u, "https://example.test/x"), "httpsも分解はできる");
        check(u.secure, "httpsならsecureがtrue");
        eq_int(u.port, 443, "httpsの既定ポートは443");

        check(!UrlTools::Parse(u, "example.test/x"), "schemeが無ければ失敗する");
        check(!UrlTools::Parse(u, "ftp://example.test/x"), "未知のschemeは失敗する");
        check(!UrlTools::Parse(u, "http:///x"), "ホストが空なら失敗する");
        check(!UrlTools::Parse(u, "http://example.test:/x"), "':'の後に数字が無ければ失敗する");
        check(!UrlTools::Parse(u, "http://example.test:99999/x"), "ポートが範囲外なら失敗する");
        check(!UrlTools::Parse(u, nullptr), "nullptrは失敗する");
    }

    printf("\n---- Url::Resolve ----\n");
    {
        Url base;
        UrlTools::Parse(base, "http://example.test/docs/pico/intro.md");

        Url out;
        check(UrlTools::Resolve(out, base, "gpio.md"), "相対参照を解決できる");
        eq_str(out.path.c_str(), "/docs/pico/gpio.md", "同じディレクトリ");
        eq_str(out.host.c_str(), "example.test", "ホストは引き継ぐ");

        UrlTools::Resolve(out, base, "../setup.md");
        eq_str(out.path.c_str(), "/docs/setup.md", "1つ上のディレクトリ");

        UrlTools::Resolve(out, base, "/index.md");
        eq_str(out.path.c_str(), "/index.md", "ルート基準");

        UrlTools::Resolve(out, base, "http://other.test:8080/x.md");
        eq_str(out.host.c_str(), "other.test", "絶対URLはホストごと移る");
        eq_int(out.port, 8080, "ポートも移る");
        eq_str(out.path.c_str(), "/x.md", "パスも差し替わる");

        UrlTools::Resolve(out, base, "search?q=abc");
        eq_str(out.path.c_str(), "/docs/pico/search", "クエリ付きの相対参照(パス)");
        eq_str(out.query.c_str(), "q=abc", "クエリ付きの相対参照(クエリ)");

        UrlTools::Resolve(out, base, "?q=only");
        eq_str(out.path.c_str(), "/docs/pico/intro.md", "クエリだけの参照はパスを据え置く");
        eq_str(out.query.c_str(), "q=only", "クエリだけ差し替わる");

        //クエリを持つURLから相対参照した場合、クエリは引き継がない
        Url withQuery;
        UrlTools::Parse(withQuery, "http://example.test/a/b.md?old=1");
        UrlTools::Resolve(out, withQuery, "c.md");
        check(out.query.empty(), "相対参照すると元のクエリは消える");
    }

    printf("\n---- クエリのパーセントエンコード ----\n");
    {
        //検索語はそのままではリクエスト行へ書けない(PROTOCOL.md「3. 検索」)
        FixedString<PICO_STR_LL> encoded;

        UrlTools::EncodeComponent(encoded, "pico-os_2.0~x");
        eq_str(encoded.c_str(), "pico-os_2.0~x", "unreservedはそのまま通す");

        UrlTools::EncodeComponent(encoded, "a b&c=d/e?f");
        eq_str(encoded.c_str(), "a%20b%26c%3Dd%2Fe%3Ff", "区切り文字は全て%XXにする");

        //日本語は1文字3バイト = %XXが3つ。バイト数を手で数えると間違えるので
        //期待値も見た目で書き下す
        UrlTools::EncodeComponent(encoded, "画像");
        eq_str(encoded.c_str(), "%E7%94%BB%E5%83%8F", "UTF-8はバイト単位で%XXにする");

        UrlTools::EncodeComponent(encoded, "");
        eq_str(encoded.c_str(), "", "空文字は空のまま");

        //収まらない場合は黙って切らずに失敗させる
        FixedString<PICO_STR_S> small;
        check(!UrlTools::EncodeComponent(small, "あいうえおかきくけこ"), "収まらなければ失敗する");
    }

    printf("\n---- リクエスト行とHostヘッダ ----\n");
    {
        Url u;
        FixedString<PICO_STR_256B> target;
        FixedString<PICO_STR_M> hostHeader;

        UrlTools::Parse(u, "http://example.test/a/b.md?q=1");
        UrlTools::RequestTarget(target, u);
        eq_str(target.c_str(), "/a/b.md?q=1", "リクエスト行はパス+クエリ");

        UrlTools::HostHeader(hostHeader, u);
        eq_str(hostHeader.c_str(), "example.test", "既定ポートならHostにポートを付けない");

        UrlTools::Parse(u, "http://example.test:8080/a");
        UrlTools::HostHeader(hostHeader, u);
        eq_str(hostHeader.c_str(), "example.test:8080", "既定以外のポートはHostに付ける");

        FixedString<PICO_PATH_LEN> formatted;
        UrlTools::Format(formatted, u);
        eq_str(formatted.c_str(), "example.test:8080/a", "表示用はschemeを含めない");
    }

    // ---- HTTPレスポンス ----
    // 1回で食わせた場合と1バイトずつ食わせた場合の両方で同じ結果になること
    for(int pass = 0; pass < 2; pass++){
        const bool byteByByte = (pass == 1);
        printf("\n---- HttpResponse (%s) ----\n", byteByByte ? "1バイトずつ" : "まとめて");

        {
            BufferSink sink;
            HttpResponse res;
            res.reset(&sink);
            feedAll(res,
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/markdown; charset=utf-8\r\n"
                "Content-Length: 11\r\n"
                "ETag: \"abc123\"\r\n"
                "\r\n"
                "# はじめ", byteByByte);

            eq_int(res.statusCode(), 200, "200を読む");
            check(res.isDone(), "Content-Lengthぶん受け取ったら完了");
            eq_str(sink.data.c_str(), "# はじめ", "本文が渡る");
            eq_str(res.validator().c_str(), "abc123", "ETagの引用符を外して持つ");
            eq_int(res.contentLength(), 11, "Content-Lengthを読む");
            eq_int(res.bodyBytes(), 11, "受け取ったバイト数");
        }
        {
            //Content-Length未指定 = 接続が閉じるまでが本文
            BufferSink sink;
            HttpResponse res;
            res.reset(&sink);
            feedAll(res, "HTTP/1.1 200 OK\n\nabcdef", byteByByte);
            check(!res.isDone(), "長さ未指定なら受信中のまま");
            check(res.finish(), "接続が閉じたら確定する");
            check(res.isDone(), "完了になる");
            eq_str(sink.data.c_str(), "abcdef", "本文が渡る(LFのみの改行も扱える)");
        }
        {
            //宣言より短いまま切れた = 途中で切れている
            BufferSink sink;
            HttpResponse res;
            res.reset(&sink);
            feedAll(res, "HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\nshort", byteByByte);
            check(!res.finish(), "宣言より短いまま切れたら失敗する");
            check(res.error() == HttpTools::Error::Truncated, "Truncatedになる");
        }
        {
            //宣言より多く届いても、余分は本文に含めない
            BufferSink sink;
            HttpResponse res;
            res.reset(&sink);
            feedAll(res, "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabcXXXX", byteByByte);
            check(res.isDone(), "宣言ぶんで完了する");
            eq_str(sink.data.c_str(), "abc", "余分は捨てる");
        }
        {
            BufferSink sink;
            HttpResponse res;
            res.reset(&sink);
            feedAll(res, "HTTP/1.1 304 Not Modified\r\nETag: \"v2\"\r\n\r\n", byteByByte);
            eq_int(res.statusCode(), 304, "304を読む");
            check(res.isNotModified(), "304と判定できる");
            check(res.isDone(), "本文を待たずに完了する");
            check(sink.data.empty(), "本文は無い");
        }
        {
            BufferSink sink;
            HttpResponse res;
            res.reset(&sink);
            feedAll(res,
                "HTTP/1.1 302 Found\r\n"
                "Location: /docs/moved.md\r\n"
                "Content-Length: 0\r\n"
                "\r\n", byteByByte);
            check(res.isRedirect(), "3xxと判定できる");
            eq_str(res.location().c_str(), "/docs/moved.md", "Locationを読む");
            check(res.isDone(), "Content-Length:0で完了する");
        }
        {
            //PROTOCOL.mdで禁止している形。黙って本文として書かずにエラーにする
            BufferSink sink;
            HttpResponse res;
            res.reset(&sink);
            feedAll(res, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n", byteByByte);
            check(res.hasFailed(), "chunkedは失敗にする");
            check(res.error() == HttpTools::Error::Chunked, "Chunkedと分かる");
            check(sink.data.empty(), "本文を1バイトも書かない");
        }
        {
            BufferSink sink;
            HttpResponse res;
            res.reset(&sink);
            feedAll(res, "HELLO WORLD\r\n\r\n", byteByByte);
            check(res.hasFailed(), "ステータス行が不正なら失敗する");
            check(res.error() == HttpTools::Error::BadStatusLine, "BadStatusLineと分かる");
        }
        {
            BufferSink sink;
            HttpResponse res;
            res.reset(&sink);
            std::string raw = "HTTP/1.1 200 OK\r\nX-Long: ";
            raw.append(HttpResponse::kMaxLineLen + 50, 'a');
            raw += "\r\n\r\n";
            feedAll(res, raw, byteByByte);
            check(res.hasFailed(), "長すぎるヘッダ行は失敗する");
            check(res.error() == HttpTools::Error::LineTooLong, "LineTooLongと分かる");
        }
        {
            //書き込み先(キャッシュ)が受け取りを拒否した場合
            BufferSink sink;
            sink.accept = false;
            HttpResponse res;
            res.reset(&sink);
            feedAll(res, "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello", byteByByte);
            check(res.hasFailed(), "書き込み先が拒否したら失敗する");
            check(res.error() == HttpTools::Error::SinkFailed, "SinkFailedと分かる");
        }
        {
            //ETagが無ければLast-Modifiedを使う。両方あればETagを優先する
            BufferSink sink;
            HttpResponse res;
            res.reset(&sink);
            feedAll(res, "HTTP/1.1 200 OK\r\nLast-Modified: Mon, 01 Jan 2029 00:00:00 GMT\r\nContent-Length: 0\r\n\r\n", byteByByte);
            eq_str(res.validator().c_str(), "Mon, 01 Jan 2029 00:00:00 GMT",
                   "ETagが無ければLast-Modifiedを使う");

            HttpResponse res2;
            res2.reset(&sink);
            feedAll(res2, "HTTP/1.1 200 OK\r\nLast-Modified: Mon, 01 Jan 2029 00:00:00 GMT\r\nETag: W/\"weak\"\r\nContent-Length: 0\r\n\r\n", byteByByte);
            eq_str(res2.validator().c_str(), "weak", "ETagがあれば優先し、W/と引用符を外す");
        }
    }

    // ---- 本文のゲート(HttpBodyGate) ----
    // **回帰テスト**: ヘッダの終わりと本文の先頭が同じ受信で届くと、本文の先頭を
    // 取りこぼしていた(サーバ情報の先頭36バイトが欠けた)。原因は「feed()が
    // 終わってからゲートを開ける」順序で、その回に渡った本文が捨てられていたこと。
    // 判定を書き込みの瞬間に移したので、1回のfeedで全部渡しても欠けない。
    printf("\n---- 本文のゲート ----\n");
    {
        //200: 本文がそのまま通る(ヘッダと本文を1回で渡す)
        BufferSink sink;
        HttpResponse res;
        HttpBodyGate gate;
        gate.attach(&res, &sink);
        res.reset(&gate);

        const char* raw = "HTTP/1.1 200 OK\r\nContent-Length: 11\r\n\r\nhello world";
        res.feed(raw, strlen(raw));
        eq_str(sink.data.c_str(), "hello world",
               "ヘッダと本文が同じ受信で届いても欠けない");
        eq_int((long)res.bodyBytes(), (long)sink.data.size(),
               "受信バイト数とシンクへ渡った量が一致する");
    }
    {
        //404: エラーページをキャッシュへ書かせない
        BufferSink sink;
        HttpResponse res;
        HttpBodyGate gate;
        gate.attach(&res, &sink);
        res.reset(&gate);

        const char* raw = "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n\r\nnot found";
        res.feed(raw, strlen(raw));
        check(res.isDone(), "404でも最後まで読み切る");
        check(sink.data.empty(), "404の本文は書き込み先へ流さない");
    }
    {
        //302: 転送先の案内文をキャッシュへ書かせない
        BufferSink sink;
        HttpResponse res;
        HttpBodyGate gate;
        gate.attach(&res, &sink);
        res.reset(&gate);

        const char* raw = "HTTP/1.1 302 Found\r\nLocation: /x.md\r\nContent-Length: 4\r\n\r\nmove";
        res.feed(raw, strlen(raw));
        check(res.isRedirect(), "3xxと判定できる");
        check(sink.data.empty(), "3xxの本文は書き込み先へ流さない");
        eq_str(res.location().c_str(), "/x.md", "Locationは読める");
    }

    printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
