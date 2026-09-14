// HttpGet の結合テスト。**実際にソケットで通信する**ので、run.sh(ネットワーク不要の
// スタブ環境)ではなく run_net.sh から動かす。相手は script/reference_server.py。
//
// 確かめたいのは、単体テスト(http_test)では触れない以下の経路:
//   - pc/compat の WiFiClient で本当に繋がって受信できること
//   - 条件付きGETでサーバが304を返し、こちらがそれを304として扱えること
//   - リダイレクトを追えること
//   - 404や、繋がらない相手で正しく失敗すること
#include "task/Http_Get.hpp"
#include "net/Doc_Fetch.hpp"
#include "storage/Doc_Cache.hpp"
#include "storage/SD_Path.hpp"
#include "storage/SD_IO.hpp"
#include "util/Md_Scan.hpp"
#include "functions/Log_Functions.hpp"
#include "OS_Data.hpp"

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
static void eq_str(const char* a, const char* e, const char* label){
    const bool ok = (strcmp(a, e) == 0);
    printf("%s %-46s 実測=%-30s 期待=%s\n", ok ? "[ OK ]" : "[FAIL]", label, a, e);
    if(!ok) failures++;
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

    // ---- DocFetch: 取得 -> キャッシュ -> 開けるパス ----
    // ここが繋がって初めて「サーバ上の文書を読む」が成立する
    {
        printf("\n---- DocFetch ----\n");
        HostSd::files.clear();

        char urlText[192];
        snprintf(urlText, sizeof(urlText), "%s/doc.md", base);

        Url url;
        UrlTools::Parse(url, urlText);

        //1回目: サーバから取ってキャッシュへ
        DocFetch fetch;
        check(fetch.begin(url), "取得を開始できる");
        for(int i = 0; i < 20000 && fetch.state() == DocFetch::State::Fetching; i++) fetch.update();

        check(fetch.state() == DocFetch::State::Ready, "Readyになる");
        check(fetch.source() == DocFetch::Source::Network, "サーバから取ってきたと分かる");

        //キャッシュの配置がPROTOCOL.mdどおりか
        char expected[256];
        snprintf(expected, sizeof(expected), "/cache/127.0.0.1_%u/doc.md", (unsigned)port);
        eq_str(fetch.path().c_str(), expected, "サーバ上のパスをミラーした場所になる");
        check(HostSd::files.count(fetch.path().c_str()) > 0, "本体がSDへ書かれている");
        check(HostSd::files[fetch.path().c_str()].find("pico-os") != std::string::npos,
              "内容が正しい");

        //目録に検証子が入っているか
        char hostKey[64];
        snprintf(hostKey, sizeof(hostKey), "127.0.0.1:%u", (unsigned)port);
        PICO_DocCache::Entry entry;
        check(PICO_DocCache::Lookup(hostKey, "/doc.md", entry), "目録から引ける");
        check(!entry.validator.empty(), "検証子(ETag)が保存されている");

        const std::string firstBody = HostSd::files[fetch.path().c_str()];

        //2回目: 条件付きGETで304になり、本文を取り直さない
        DocFetch again;
        again.begin(url);
        for(int i = 0; i < 20000 && again.state() == DocFetch::State::Fetching; i++) again.update();

        check(again.state() == DocFetch::State::Ready, "2回目もReadyになる");
        check(again.source() == DocFetch::Source::NotModified,
              "304でキャッシュがそのまま使われる");
        check(HostSd::files[again.path().c_str()] == firstBody, "本体は書き換わらない");

        //一時ファイルが残っていないこと(304のときwriterをabortしている)
        int leftover = 0;
        for(const auto& kv : HostSd::files){
            if(kv.first.size() >= 5 && kv.first.compare(kv.first.size() - 5, 5, ".part") == 0) leftover++;
        }
        eq_int(leftover, 0, "一時ファイルが残らない");
    }

    // ---- DocFetch: 取れないときは古いキャッシュで代用する ----
    {
        HostSd::files.clear();

        //閉じているポートのサーバのキャッシュを先に作っておく
        {
            PICO_DocCache::Writer w;
            check(w.begin("127.0.0.1:9", "/offline.md"), "キャッシュを用意する");
            const char* body = "# 保存済みの内容\n";
            w.write(body, strlen(body));
            check(w.commit("etag-old", 1000), "キャッシュを確定する");
        }

        Url url;
        UrlTools::Parse(url, "http://127.0.0.1:9/offline.md");

        DocFetch fetch;
        fetch.begin(url);
        for(int i = 0; i < 20000 && fetch.state() == DocFetch::State::Fetching; i++) fetch.update();

        check(fetch.state() == DocFetch::State::Ready, "繋がらなくてもReadyになる");
        check(fetch.source() == DocFetch::Source::CacheAfterError,
              "古いキャッシュを開いたと分かる(オフライン表示)");
        check(HostSd::files[fetch.path().c_str()].find("保存済み") != std::string::npos,
              "保存済みの内容がそのまま残っている");
        check(fetch.message()[0] != '\0', "理由が伝わる");
    }

    // ---- DocFetch: キャッシュも無ければ失敗する ----
    {
        HostSd::files.clear();

        char urlText[192];
        snprintf(urlText, sizeof(urlText), "%s/no-such-file.md", base);

        Url url;
        UrlTools::Parse(url, urlText);

        DocFetch fetch;
        fetch.begin(url);
        for(int i = 0; i < 20000 && fetch.state() == DocFetch::State::Fetching; i++) fetch.update();

        check(fetch.state() == DocFetch::State::Failed, "404かつキャッシュ無しは失敗する");
        check(fetch.message()[0] != '\0', "理由が伝わる");

        int leftover = 0;
        for(const auto& kv : HostSd::files){
            if(kv.first.size() >= 5 && kv.first.compare(kv.first.size() - 5, 5, ".part") == 0) leftover++;
        }
        eq_int(leftover, 0, "失敗しても一時ファイルが残らない");
    }

    // ---- 画像: 文書を取る -> 走査 -> 画像も取る ----
    // MarkdownSceneが表示前にやる手順をそのままなぞる。
    // **最後の1件が肝** — 取ってきた画像の置き場所と、MarkdownViewが
    // 文書基準で解決するパスが一致していること。ここが噛み合っていないと
    // 「取ってきたのに表示されない」になる
    {
        printf("\n---- 画像の先読み ----\n");
        HostSd::files.clear();

        char urlText[192];
        snprintf(urlText, sizeof(urlText), "%s/doc.md", base);

        Url docUrl;
        UrlTools::Parse(docUrl, urlText);

        //1. 文書を取る
        DocFetch docFetch;
        docFetch.begin(docUrl);
        for(int i = 0; i < 20000 && docFetch.state() == DocFetch::State::Fetching; i++) docFetch.update();
        check(docFetch.state() == DocFetch::State::Ready, "文書を取得できる");

        const std::string docCachePath = docFetch.path().c_str();

        //2. 走査して画像参照を集める(MarkdownScene::collectMissingImages と同じ規則)
        FixedString<PICO_STR_L> imageRef;
        {
            const std::string& text = HostSd::files[docCachePath];
            size_t start = 0;
            while(start < text.size()){
                size_t end = text.find('\n', start);
                if(end == std::string::npos) end = text.size();

                const char* ref = nullptr;
                size_t refLen = 0;
                if(MdScan::ImageRefInLine(text.data() + start, end - start, ref, refLen)){
                    imageRef.assign(ref, refLen);
                    break;
                }
                start = end + 1;
            }
        }
        eq_str(imageRef.c_str(), "img/sample.pimg", "文書から画像参照を見つけられる");

        //3. 文書のURLを基準に解決して取りに行く
        Url imageUrl;
        check(UrlTools::Resolve(imageUrl, docUrl, imageRef.c_str()), "画像URLを解決できる");
        eq_str(imageUrl.path.c_str(), "/img/sample.pimg", "画像のパスが文書基準で解決される");

        DocFetch imgFetch;
        imgFetch.begin(imageUrl);
        for(int i = 0; i < 20000 && imgFetch.state() == DocFetch::State::Fetching; i++) imgFetch.update();

        check(imgFetch.state() == DocFetch::State::Ready, "画像を取得できる");
        check(HostSd::files.count(imgFetch.path().c_str()) > 0, "画像がSDへ書かれている");
        eq_int((long)HostSd::files[imgFetch.path().c_str()].size(), 53, "画像のバイト数が一致する");

        //4. MarkdownViewが文書基準で解決するパスと、画像の置き場所が一致すること
        FixedString<PICO_PATH_LEN> resolvedByView;
        check(PICO_IO::resolve(resolvedByView, docCachePath.c_str(), imageRef.c_str()),
              "View側の解決が成功する");
        eq_str(resolvedByView.c_str(), imgFetch.path().c_str(),
               "View側の解決先と画像の置き場所が一致する");

        //5. 2回目は取りに行かない(キャッシュ済み)
        FixedString<PICO_STR_M> host;
        UrlTools::HostHeader(host, docUrl);
        check(PICO_DocCache::Exists(host.c_str(), imageUrl.path.c_str()),
              "2回目以降はキャッシュ済みと判定される");
    }

    printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
