// 録音の文字起こしの通信係(TranscribeClient)を本物のソケットで確かめる結合テスト。run_net.sh から呼ぶ。
//
// **本物のソケットとTLS(OpenSSL、pc/compat/)を使う。** 相手は script/host_test/transcribe_fake_server.py
// (OpenAI互換の /v1/audio/transcriptions の偽物。run_net.sh が平文・HTTPS・上限を小さくしたものの3つを立てる)。
// 本物の api.openai.com へは行かない(キーと料金が要るため)。
//
// 見ること:
//   - 録音(数百KB)をSDから流して送り、返事の文章が <名前>.txt に入る。相手が受け取ったWAVの長さとSHA-256が手元と同じ
//   - 送っている割合が進み、送り終えると「返事を待っている」になる。書きかけ(.part)は残らない
//   - model / language / prompt が届く。language を空にすると送らない
//   - キーが違えば401の理由(OpenAIの形のJSON)が分かり、前の .txt は壊れない
//   - 送り終える前に相手が断った(413)ときも、その理由が分かる
//   - 500({"error":"…"}。whisper.cpp の形)の理由
//   - 途中でやめられる。HTTPSでも同じ
//   - /sys/transcribe.cfg と平文のキーのファイルから読んで送れる
//
// 使い方: transcribe_net_test <httpのport> <httpsのport> <ca.pem> <キー> <上限を小さくしたhttpのport>
#include "apps/recorder/Transcribe_Client.hpp"
#include "system/Log_Functions.hpp"
#include "storage/SD_Path.hpp"
#include "OS_Data.hpp"

#include <openssl/sha.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <unistd.h>

void LogFunctions::Log(LogType, const char* fmt, ...){
    va_list args;
    va_start(args, fmt);
    printf("    [log] ");
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

using S = TranscribeClient::State;

static std::string makeWav(size_t data_bytes, uint32_t seed){
    std::string w = "RIFF";
    w.append(40, '\0');
    uint32_t x = seed;
    for(size_t i = 0; i < data_bytes; i++){
        x = x * 1664525u + 1013904223u;
        w += (char)(x >> 24);
    }
    return w;
}

static std::string sha256Hex(const std::string& s){
    unsigned char d[SHA256_DIGEST_LENGTH];
    SHA256((const unsigned char*)s.data(), s.size(), d);
    char hex[SHA256_DIGEST_LENGTH * 2 + 1];
    for(int i = 0; i < SHA256_DIGEST_LENGTH; i++) snprintf(hex + i * 2, 3, "%02x", d[i]);
    return hex;
}

// millis() はスタブで進まないので、回数で打ち切る(最大20秒)。送っている割合の推移も覚える
struct Run {
    bool finished = false;
    bool saw_sending = false;
    bool saw_waiting = false;
    int max_percent = 0;
    bool percent_monotonic = true;
};
static Run runToEnd(TranscribeClient& c, const std::function<bool()>& stop_early = nullptr){
    Run r;
    int last = -1;
    for(int i = 0; i < 20000; i++){
        c.update(true);
        if(c.state() == S::Sending){
            r.saw_sending = true;
            const int p = c.progressPercent();
            if(p < last) r.percent_monotonic = false;
            last = p;
            if(p > r.max_percent) r.max_percent = p;
        }
        if(c.state() == S::Waiting) r.saw_waiting = true;
        if(stop_early && stop_early()) return r;
        if(!c.busy()){
            r.finished = true;
            return r;
        }
        usleep(1000);
    }
    return r;
}

static void scenario(const char* label, const std::string& api, const char* key){
    printf("\n--- %s (%s) ---\n", label, api.c_str());
    HostSd::files.erase("/recordings/rec_0001.txt");
    const std::string wav = makeWav(400 * 1000, 7);
    HostSd::files["/recordings/rec_0001.wav"] = wav;

    TranscribeClient::Config cfg;
    cfg.api.assign(api.c_str());
    cfg.model.assign("whisper-1");
    cfg.language.assign("ja");
    cfg.prompt.assign("議事録");
    TranscribeClient c;
    check(c.configure(cfg, key), "設定できる");
    check(c.start("/recordings/rec_0001.wav"), "送り始める");
    const Run r = runToEnd(c);
    check(r.finished && c.state() == S::Done, "終わる");
    if(c.state() != S::Done) printf("       理由: %s\n", c.message());
    check(r.saw_sending && r.saw_waiting, "送っている → 返事を待っている と進む");
    check(r.percent_monotonic && r.max_percent >= 90, "送った割合が増えていく");
    check(HostSd::files.count("/recordings/rec_0001.txt.part") == 0, "書きかけを残さない");
    const std::string txt = HostSd::files.count("/recordings/rec_0001.txt") ? HostSd::files["/recordings/rec_0001.txt"] : "";
    char expect[256];
    snprintf(expect, sizeof(expect), "model=whisper-1 language=ja prompt=議事録 name=rec_0001.wav bytes=%zu sha256=%s\n",
             wav.size(), sha256Hex(wav).c_str());
    check(txt.compare(0, strlen(expect), expect) == 0, "相手が受け取ったWAVが手元と同じ(長さ・SHA-256)・値が届く");
    if(txt.compare(0, strlen(expect), expect) != 0) printf("       実測=%s       期待=%s", txt.c_str(), expect);
    check(txt.find("今日は晴れです。") != std::string::npos, "文章が .txt に入る");

    //キーが違う → 401。前の .txt は壊れない
    TranscribeClient bad;
    check(bad.configure(cfg, "sk-wrong"), "違うキーでも設定はできる");
    check(bad.start("/recordings/rec_0001.wav"), "送り始める");
    runToEnd(bad);
    check(bad.state() == S::Failed && strstr(bad.message(), "APIキーが違います") != nullptr
          && strstr(bad.message(), "Incorrect API key") != nullptr, "401の理由が分かる");
    printf("       理由: %s\n", bad.message());
    check(HostSd::files["/recordings/rec_0001.txt"] == txt, "失敗しても前の .txt は壊れない");

    //500(whisper.cpp の形)
    TranscribeClient::Config cfg500 = cfg;
    cfg500.prompt.assign("fail500");
    cfg500.language.assign("");
    TranscribeClient e;
    check(e.configure(cfg500, key) && e.start("/recordings/rec_0001.wav"), "送り始める(500)");
    runToEnd(e);
    check(e.state() == S::Failed && strstr(e.message(), "HTTP 500: failed to process audio") != nullptr, "500の理由が分かる");

    //途中でやめる
    HostSd::files["/recordings/rec_0003.wav"] = makeWav(3 * 1000 * 1000, 9);
    TranscribeClient k;
    check(k.configure(cfg, key) && k.start("/recordings/rec_0003.wav"), "大きな録音を送り始める");
    runToEnd(k, [&]{ return k.state() == S::Sending && k.progressPercent() >= 5; });
    check(k.busy(), "送っている途中");
    k.cancel();
    check(k.state() == S::Ready && !k.busy(), "やめられる");
    check(HostSd::files.count("/recordings/rec_0003.txt") == 0 && HostSd::files.count("/recordings/rec_0003.txt.part") == 0,
          "やめたら何も残さない");
    HostSd::files.erase("/recordings/rec_0003.wav");
}

static void earlyReject(const std::string& api, const char* key){
    printf("\n--- 送り終える前に断られる(%s) ---\n", api.c_str());
    HostSd::files["/recordings/rec_0004.wav"] = makeWav(2 * 1000 * 1000, 11);
    TranscribeClient::Config cfg;
    cfg.api.assign(api.c_str());
    TranscribeClient c;
    check(c.configure(cfg, key) && c.start("/recordings/rec_0004.wav"), "送り始める");
    const Run r = runToEnd(c);
    check(r.finished && c.state() == S::Failed && strstr(c.message(), "大きすぎます") != nullptr
          && strstr(c.message(), "413") != nullptr, "413の理由が分かる");
    printf("       理由: %s\n", c.message());
    check(r.max_percent < 100, "全部は送らずにやめる");
    check(HostSd::files.count("/recordings/rec_0004.txt") == 0, ".txt は作らない");
    HostSd::files.erase("/recordings/rec_0004.wav");
}

static void fromFiles(const std::string& api, const char* key){
    printf("\n--- 設定ファイルから ---\n");
    HostSd::files[PICO_Path::FILE::CFG::SYS_TRANSCRIBE_CFG] = "api = " + api + "\nlanguage =\nmodel = gpt-4o-mini-transcribe\n";
    HostSd::files[PICO_Path::FILE::CFG::SYS_TRANSCRIBE_KEY] = std::string(key) + "\n";
    HostSd::files["/recordings/rec_0005.wav"] = makeWav(1000, 3);
    TranscribeClient c;
    check(c.loadConfig(), "読める");
    check(HostSd::files[PICO_Path::FILE::CFG::SYS_TRANSCRIBE_KEY].find(key) == std::string::npos, "平文のキーは暗号化して書き直す");
    check(c.start("/recordings/rec_0005.wav"), "送り始める");
    runToEnd(c);
    const std::string txt = HostSd::files["/recordings/rec_0005.txt"];
    check(c.state() == S::Done && txt.find("model=gpt-4o-mini-transcribe language=(none) prompt=(none)") == 0,
          "暗号化し直したキーで送れ、空の language は送らない");
}

int main(int argc, char** argv){
    if(argc < 6){
        fprintf(stderr, "使い方: %s <http port> <https port> <ca.pem> <key> <上限を小さくした http port>\n", argv[0]);
        return 2;
    }
    std::ifstream ca_in(argv[3]);
    std::stringstream ca;
    ca << ca_in.rdbuf();
    OSData::SD_usable = true;
    HostSd::files[PICO_Path::FILE::TLS_EXTRA_CA_PEM] = ca.str();

    const char* key = argv[4];
    scenario("HTTP", std::string("http://127.0.0.1:") + argv[1] + "/v1", key);
    scenario("HTTPS", std::string("https://localhost:") + argv[2] + "/v1/", key);
    earlyReject(std::string("http://127.0.0.1:") + argv[5] + "/v1", key);
    fromFiles(std::string("http://127.0.0.1:") + argv[1] + "/v1", key);

    printf("\n%s (失敗 %d件)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
