// HttpGet の結合テスト。**実際にソケットで通信する**ので、run.sh(ネットワーク不要の
// スタブ環境)ではなく run_net.sh から動かす。相手は script/reference_server.py。
//
// 確かめたいのは、単体テスト(http_test)では触れない以下の経路:
//   - pc/compat の WiFiClient で本当に繋がって受信できること
//   - 条件付きGETでサーバが304を返し、こちらがそれを304として扱えること
//   - リダイレクトを追えること
//   - 404や、繋がらない相手で正しく失敗すること
#include "task/Http_Get.hpp"
#include "functions/Log_Functions.hpp"

#include <cstdio>
#include <cstring>
#include <string>

// ---- モック ----
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

class BufferSink : public IHttpSink {
    public:
        std::string data;
        bool write(const void* p, size_t n) override {
            data.append((const char*)p, n);
            return true;
        }
};

// タスクが終わるまでupdate()を回す(実機のloop()の代わり)
static void pump(HttpGet& task, int max_iterations = 20000){
    for(int i = 0; i < max_iterations; i++){
        if(task.getStatus() != TaskTools::PROCESSING) return;
        task.update();
    }
}

int main(int argc, char** argv){
    const char* host = "127.0.0.1";
    const uint16_t port = (argc > 1) ? (uint16_t)atoi(argv[1]) : 8080;

    char base[128];
    snprintf(base, sizeof(base), "http://%s:%u", host, (unsigned)port);

    std::string etag;

    // ---- 文書の取得 ----
    {
        char urlText[192];
        snprintf(urlText, sizeof(urlText), "%s/doc.md", base);

        Url url;
        check(UrlTools::Parse(url, urlText), "URLを解釈できる");

        BufferSink sink;
        HttpGet task;
        check(task.begin(url, &sink), "取得を開始できる");
        pump(task);

        eq_int(task.getStatus(), TaskTools::SUCCESS, "成功で終わる");
        eq_int(task.response().statusCode(), 200, "200が返る");
        check(!sink.data.empty(), "本文を受け取れる");
        check(sink.data.find("pico-os") != std::string::npos, "本文の中身が期待どおり");
        check(!task.response().validator().empty(), "ETagを受け取れる");
        eq_int((long)task.response().bodyBytes(), (long)sink.data.size(),
               "受信バイト数とシンクへ渡った量が一致する");

        etag = task.response().validator().c_str();
    }

    // ---- 条件付きGET(変更が無ければ304) ----
    {
        char urlText[192];
        snprintf(urlText, sizeof(urlText), "%s/doc.md", base);

        Url url;
        UrlTools::Parse(url, urlText);

        BufferSink sink;
        HttpGet task;
        task.begin(url, &sink, etag.c_str());
        pump(task);

        eq_int(task.getStatus(), TaskTools::SUCCESS, "条件付きGETも成功で終わる");
        eq_int(task.response().statusCode(), 304, "304が返る");
        check(task.isNotModified(), "304と判定できる");
        check(sink.data.empty(), "本文は受け取らない(キャッシュをそのまま使える)");
    }

    // ---- 検索API(TSV) ----
    {
        char urlText[192];
        snprintf(urlText, sizeof(urlText), "%s/v1/search?q=pico", base);

        Url url;
        check(UrlTools::Parse(url, urlText), "検索URLを解釈できる");

        BufferSink sink;
        HttpGet task;
        task.begin(url, &sink);
        pump(task);

        eq_int(task.response().statusCode(), 200, "検索は200が返る");
        check(sink.data.find('\t') != std::string::npos, "TSVが返る");
        check(sink.data.find("/doc.md") != std::string::npos, "パスが1列目に入っている");
    }

    // ---- リダイレクト(サーバは / をホームへ302する) ----
    {
        char urlText[192];
        snprintf(urlText, sizeof(urlText), "%s/", base);

        Url url;
        UrlTools::Parse(url, urlText);

        BufferSink sink;
        HttpGet task;
        task.begin(url, &sink);
        pump(task);

        eq_int(task.getStatus(), TaskTools::SUCCESS, "リダイレクトを追って成功する");
        eq_int(task.response().statusCode(), 200, "最終的に200になる");
        check(!sink.data.empty(), "転送先の本文を受け取れる");
        check(sink.data.find("pico-os") != std::string::npos, "転送先の中身が期待どおり");
    }

    // ---- 見つからない ----
    {
        char urlText[192];
        snprintf(urlText, sizeof(urlText), "%s/no-such-file.md", base);

        Url url;
        UrlTools::Parse(url, urlText);

        BufferSink sink;
        HttpGet task;
        task.begin(url, &sink);
        pump(task);

        eq_int(task.getStatus(), TaskTools::FAILED, "404は失敗として終わる");
        eq_int(task.response().statusCode(), 404, "404が読める");
        check(sink.data.empty(), "404の本文はシンクへ流さない(キャッシュを汚さない)");
    }

    // ---- 繋がらない相手 ----
    {
        Url url;
        //閉じているポートを狙う
        UrlTools::Parse(url, "http://127.0.0.1:9/x.md");

        BufferSink sink;
        HttpGet task;
        task.begin(url, &sink);
        pump(task);

        eq_int(task.getStatus(), TaskTools::FAILED, "繋がらない相手は失敗する");
        check(task.failure() == HttpGet::Fail::ConnectFailed, "接続失敗として分かる");
    }

    // ---- httpsは接続前に弾く ----
    {
        Url url;
        UrlTools::Parse(url, "https://example.test/x.md");

        BufferSink sink;
        HttpGet task;
        task.begin(url, &sink);
        pump(task);

        eq_int(task.getStatus(), TaskTools::FAILED, "httpsは失敗する");
        check(task.failure() == HttpGet::Fail::NotHttp, "未対応として分かる(接続はしない)");
    }

    printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
