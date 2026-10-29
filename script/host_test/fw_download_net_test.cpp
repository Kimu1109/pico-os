// Webからのファームの更新の取得(apps/firmware/Fw_Download)を、本物のHTTPで確かめる(run_net.sh)。
//
// http.server が <dir>/www を配る。置き場(<dir>/www/firmware/)の一覧と更新ファイルはこのテストが書く
// (http.server は毎回ファイルを読み直すので、途中で中身を変えられる)。
// 更新ファイルの中身は読まない(見出し・署名は Fw_Update の担当)ので、ただのバイト列でよい。
//
// SDは stubs/ のメモリ上のもの。
//
// 使い方: fw_download_net_test <port> <dir>
#include "apps/firmware/Fw_Download.hpp"
#include "system/Log_Functions.hpp"
#include "OS_Data.hpp"

#include <cstdarg>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>

void LogFunctions::Log(LogType, const char* fmt, ...){
    if(!getenv("FW_TEST_VERBOSE")) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

static std::string g_www;

static void WriteHost(const std::string& rel, const std::string& data){
    std::ofstream f(g_www + "/" + rel, std::ios::binary | std::ios::trunc);
    f.write(data.data(), (std::streamsize)data.size());
}

static std::string ShaHex(const std::string& data){
    uint8_t d[32];
    Sha256 s;
    s.update(data.data(), data.size());
    s.finish(d);
    char hex[65];
    for(int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", d[i]);
    return hex;
}

static std::string Row(uint32_t serial, const char* git, const char* channel, const char* env, int sig,
                       size_t bytes, const std::string& sha, const char* file){
    char head[128];
    snprintf(head, sizeof(head), "%lu\t%s\t2026-10-28 18:43\t%s\t%s\t%d\t%lu\t", (unsigned long)serial, git, channel,
             env, sig, (unsigned long)bytes);
    return std::string(head) + sha + "\t" + file + "\n";
}

static void Run(FwDownload& d){
    for(int i = 0; i < 60000 && d.busy(); i++){
        d.update();
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    if(getenv("FW_TEST_VERBOSE")) fprintf(stderr, "  -> %s\n", d.message());
}

static bool Has(const char* path){ return HostSd::files.count(path) > 0; }

int main(int argc, char** argv){
    if(argc < 3){
        fprintf(stderr, "usage: %s <port> <dir>\n", argv[0]);
        return 2;
    }
    setvbuf(stdout, nullptr, _IONBF, 0);
    const int port = atoi(argv[1]);
    g_www = std::string(argv[2]) + "/www";
    OSData::SD_usable = true;

    // 1.5MBほどの更新ファイル(本物と同じくらいの大きさ。全体の10秒ではなく無通信で打ち切ることも見る)
    std::string good(1500000, '\0');
    for(size_t i = 0; i < good.size(); i++) good[i] = (char)((i * 2654435761u) >> 13);
    const std::string small = "small-pfw-body";
    WriteHost("firmware/pico-os-412-abcdef1.pfw", good);
    WriteHost("firmware/pico-os-411-badsha0.pfw", small);
    WriteHost("firmware/pico-os-410-short00.pfw", good);
    const std::string index = std::string(FwIndex::kMagic) + "\n" +
        Row(412, "abcdef1", "stable", "rpipico2w", 1, good.size(), ShaHex(good), "pico-os-412-abcdef1.pfw") +
        Row(411, "badsha0", "nightly", "rpipico2w", 1, small.size(), ShaHex("other"), "pico-os-411-badsha0.pfw") +
        Row(410, "short00", "nightly", "rpipico2w", 1, 1000, ShaHex(good), "pico-os-410-short00.pfw") +
        Row(409, "missing", "nightly", "rpipico2w", 1, 10, ShaHex("x"), "pico-os-409-missing.pfw") +
        Row(408, "lite000", "nightly", "rpipico2w_lite", 1, 10, ShaHex("x"), "pico-os-408-lite000.pfw") +
        Row(407, "unsign0", "nightly", "rpipico2w", 0, 10, ShaHex("x"), "pico-os-407-unsign0.pfw");
    WriteHost("firmware/index.tsv", index);
    WriteHost("notindex/index.tsv", "<html>hello</html>\n");

    char base[128];
    snprintf(base, sizeof(base), "http://127.0.0.1:%d/firmware", port); // 末尾の / は足される

    FwIndex::Filter filter;
    filter.env = "rpipico2w";

    // ---- 1. 一覧 ----
    FwDownload* d = new FwDownload();
    check(strcmp(d->baseUrl(), FwDownload::kDefaultUrl) == 0, "既定の置き場は GitHub Pages");
    check(!d->setBaseUrl("ftp://example.com/") && !d->setBaseUrl(nullptr), "http(s)でないURLは断る");
    check(d->setBaseUrl(base) && d->baseUrl()[strlen(d->baseUrl()) - 1] == '/', "置き場のURLの末尾に / を足す");
    check(d->beginIndex(filter) && d->busy(), "一覧を取り始める");
    Run(*d);
    check(d->state() == FwDownload::State::Listed && d->count() == 4, "一覧を取れる(構成違い・署名なしは外す)");
    check(d->indexResult().hidden_env == 1 && d->indexResult().hidden_unsigned == 1, "外した数を数える");
    check(d->entry(0) && d->entry(0)->serial == 412 && !d->entry(4), "並び順のまま・範囲外はnullptr");

    // ---- 2. 落とす ----
    check(d->beginFile(0, "/firmware"), "更新ファイルを落とし始める");
    check(d->progressTotal() == good.size(), "進み具合の全体は一覧の大きさ");
    bool saw_progress = false;
    for(int i = 0; i < 60000 && d->busy(); i++){
        d->update();
        if(d->progressDone() > 0 && d->progressDone() < good.size()) saw_progress = true;
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    check(saw_progress, "途中の進み具合が見える");
    check(d->state() == FwDownload::State::Done && strcmp(d->path(), "/firmware/pico-os-412-abcdef1.pfw") == 0,
          "落とせた");
    check(Has("/firmware/pico-os-412-abcdef1.pfw") && HostSd::files["/firmware/pico-os-412-abcdef1.pfw"] == good,
          "SDに中身がそのまま置かれる");
    check(!Has("/firmware/pico-os-412-abcdef1.pfw.part"), "途中のファイル(.part)は残らない");

    // 同じものをもう一度(前のファイルを置き換える)
    HostSd::files["/firmware/pico-os-412-abcdef1.pfw"] = "old";
    check(d->beginFile(0, "/firmware"), "Doneの後に選び直せる");
    Run(*d);
    check(d->state() == FwDownload::State::Done && HostSd::files["/firmware/pico-os-412-abcdef1.pfw"] == good,
          "前の同じ名前のファイルを置き換える");

    // ---- 3. 一覧と合わない ----
    check(d->beginFile(1, "/firmware"), "SHA-256の合わないものを取り始める");
    Run(*d);
    check(d->state() == FwDownload::State::Failed && strstr(d->message(), "合いません"), "SHA-256が合わなければ断る");
    check(!Has("/firmware/pico-os-411-badsha0.pfw") && !Has("/firmware/pico-os-411-badsha0.pfw.part"),
          "断ったものはSDに残さない");

    check(d->beginFile(2, "/firmware"), "Failedの後でも一覧から選び直せる");
    Run(*d);
    check(d->state() == FwDownload::State::Failed && strstr(d->message(), "大きい"), "一覧より大きければ途中でやめる");
    check(!Has("/firmware/pico-os-410-short00.pfw.part"), "やめたものはSDに残さない");

    check(d->beginFile(3, "/firmware"), "置き場に無いものを取り始める");
    Run(*d);
    check(d->state() == FwDownload::State::Failed && strstr(d->message(), "404"), "404を伝える");
    check(!Has("/firmware/pico-os-409-missing.pfw") && !Has("/firmware/pico-os-409-missing.pfw.part"),
          "404の本文はSDに書かない");

    // ---- 4. 取り消す ----
    check(d->beginFile(0, "/firmware"), "取り消すために取り始める");
    for(int i = 0; i < 60000 && d->busy() && d->progressDone() == 0; i++){
        d->update();
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    d->cancel();
    check(d->state() == FwDownload::State::Listed && !d->busy(), "取り消すと一覧に戻る");
    check(!Has("/firmware/pico-os-412-abcdef1.pfw.part"), "取り消したものはSDに残さない");
    check(!d->beginFile(99, "/firmware") && !d->beginFile(-1, "/firmware"), "範囲外の番号は断る");

    // ---- 5. SDが無い ----
    OSData::SD_usable = false;
    check(!d->beginFile(0, "/firmware") && d->state() == FwDownload::State::Failed, "SDが使えなければ断る");
    OSData::SD_usable = true;
    delete d;

    // ---- 6. 一覧が取れない ----
    d = new FwDownload();
    snprintf(base, sizeof(base), "http://127.0.0.1:%d/nothing/", port);
    d->setBaseUrl(base);
    check(!d->beginFile(0, "/firmware"), "一覧を取る前はファイルを取れない");
    d->beginIndex(filter);
    Run(*d);
    check(d->state() == FwDownload::State::Failed && strstr(d->message(), "404") && d->count() == 0, "一覧が無い(404)");
    snprintf(base, sizeof(base), "http://127.0.0.1:%d/notindex/", port);
    d->setBaseUrl(base);
    d->beginIndex(filter);
    Run(*d);
    check(d->state() == FwDownload::State::Failed && strstr(d->message(), "形式"), "一覧でないものは断る");

    // 構成が違う本体には何も残らない
    snprintf(base, sizeof(base), "http://127.0.0.1:%d/firmware/", port);
    d->setBaseUrl(base);
    FwIndex::Filter other;
    other.env = "rpipico2w_320x480";
    d->beginIndex(other);
    Run(*d);
    check(d->state() == FwDownload::State::Listed && d->count() == 0 && strstr(d->message(), "rpipico2w_320x480"),
          "構成向けの配布が無ければそう伝える");
    // 一覧が大きすぎる
    WriteHost("firmware/index.tsv", std::string(FwIndex::kMagic) + "\n" + std::string(FwIndex::kMaxBytes, '#'));
    d->beginIndex(filter);
    Run(*d);
    check(d->state() == FwDownload::State::Failed && strstr(d->message(), "大きすぎ"), "大きすぎる一覧は断る");
    // 通信先が無い
    d->setBaseUrl("http://127.0.0.1:1/firmware/");
    d->beginIndex(filter);
    Run(*d);
    check(d->state() == FwDownload::State::Failed && strstr(d->message(), "通信に失敗"), "繋がらなければ伝える");
    delete d;

    printf(failures ? "\n%d 件失敗\n" : "\n全部通りました\n", failures);
    return failures ? 1 : 0;
}
