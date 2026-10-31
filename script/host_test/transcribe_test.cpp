// 録音の文字起こし(apps/recorder/Transcribe_Proto・Transcribe_Client)の通信しない部分を確かめる。
//
// 見ること:
//   - multipart の前後(model / language / prompt / response_format=text / file の見出し)。空の language・prompt は送らない
//   - 区切り・値の検査(改行や " を含む値は断る。入りきらなければ失敗して空にする)
//   - 失敗の返事から理由を拾う(OpenAI/Groq・whisper.cpp・detail・JSONでない本文・1バイトずつ)
//   - HTTPの状態番号ごとの説明、rec_0001.wav → rec_0001.txt
//   - 設定: OpenAI(既定の送り先)はキーが要る/手元のサーバならキー無しでよい/キーに使えない文字
//   - /sys/transcribe.cfg と /sys/transcribe_key.cfg(平文の1行なら暗号化して書き直す。長いキーは行に分ける)
//   - 送る本文(head + WAV + tail)を半端な大きさで読んでも崩れない・読み直せる
//   - 始められない場合(無い・空・25MBを超える・WAVでない)と、やめたとき
#include "apps/recorder/Transcribe_Proto.hpp"
#include "apps/recorder/Transcribe_Client.hpp"
#include "storage/SD_Path.hpp"
#include "system/Log_Functions.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstring>
#include <string>

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
    const bool ok = strcmp(a, e) == 0;
    printf("%s %-46s 実測=\"%s\"\n", ok ? "[ OK ]" : "[FAIL]", label, a);
    if(!ok){
        printf("       期待=\"%s\"\n", e);
        failures++;
    }
}

static void testMultipart(){
    printf("\n--- multipart ---\n");
    char b1[40], b2[40];
    Transcribe::MakeBoundary(b1, sizeof(b1), 1);
    Transcribe::MakeBoundary(b2, sizeof(b2), 2);
    check(strlen(b1) == 31 && strncmp(b1, "picoos-", 7) == 0, "区切りは picoos- + 24文字");
    bool alnum = true;
    for(const char* p = b1 + 7; *p; p++) alnum = alnum && ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'z'));
    check(alnum, "区切りは英小文字と数字だけ");
    check(strcmp(b1, b2) != 0, "種が違えば区切りも違う");

    Transcribe::Fields f;
    f.model = "whisper-1";
    f.language = "ja";
    f.prompt = "pico-os, RP2350";
    Transcribe::Head head;
    check(Transcribe::BuildHead(head, "BND", f, "rec_0001.wav"), "前の部分を作れる");
    eq_str(head.c_str(),
        "--BND\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\nwhisper-1\r\n"
        "--BND\r\nContent-Disposition: form-data; name=\"language\"\r\n\r\nja\r\n"
        "--BND\r\nContent-Disposition: form-data; name=\"prompt\"\r\n\r\npico-os, RP2350\r\n"
        "--BND\r\nContent-Disposition: form-data; name=\"response_format\"\r\n\r\ntext\r\n"
        "--BND\r\nContent-Disposition: form-data; name=\"file\"; filename=\"rec_0001.wav\"\r\n"
        "Content-Type: audio/wav\r\n\r\n",
        "前の部分の形");
    Transcribe::Tail tail;
    check(Transcribe::BuildTail(tail, "BND"), "後ろを作れる");
    eq_str(tail.c_str(), "\r\n--BND--\r\n", "後ろの形");

    f.language = "";
    f.prompt = "";
    check(Transcribe::BuildHead(head, "BND", f, "a.wav")
          && strstr(head.c_str(), "language") == nullptr && strstr(head.c_str(), "prompt") == nullptr,
          "空の language・prompt は送らない");

    f.prompt = "改行\r\n--BND";
    check(!Transcribe::BuildHead(head, "BND", f, "a.wav") && head.empty(), "改行を含む値は断る");
    f.prompt = "\"引用\"";
    check(!Transcribe::BuildHead(head, "BND", f, "a.wav"), "\" を含む値は断る");
    f.prompt = "";
    check(!Transcribe::BuildHead(head, "BND", f, "a\".wav"), "\" を含むファイル名は断る");
    f.model = "";
    check(!Transcribe::BuildHead(head, "BND", f, "a.wav"), "model が空なら断る");
    std::string long_prompt(1200, 'x');
    f.model = "whisper-1";
    f.prompt = long_prompt.c_str();
    check(!Transcribe::BuildHead(head, "BND", f, "a.wav") && head.empty(), "入りきらなければ失敗して空にする");
}

static std::string parseError(const std::string& body, bool bytewise){
    Transcribe::ErrorParser p;
    p.begin();
    if(bytewise){
        for(char c : body) p.feed(&c, 1);
    }else{
        p.feed(body.data(), body.size());
    }
    return p.message();
}

static void testErrors(){
    printf("\n--- 失敗の理由 ---\n");
    const std::string openai = "{\n  \"error\": {\n    \"message\": \"Incorrect API key provided: sk-xx***\",\n"
                               "    \"type\": \"invalid_request_error\", \"param\": null, \"code\": \"invalid_api_key\"\n  }\n}\n";
    eq_str(parseError(openai, false).c_str(), "Incorrect API key provided: sk-xx***", "OpenAI/Groq の形");
    eq_str(parseError(openai, true).c_str(), "Incorrect API key provided: sk-xx***", "1バイトずつでも同じ");
    eq_str(parseError("{\"error\":\"failed to read WAV\"}", false).c_str(), "failed to read WAV", "whisper.cpp の形");
    eq_str(parseError("{\"detail\":\"Not authenticated\"}", false).c_str(), "Not authenticated", "detail の形");
    eq_str(parseError("{\"message\":\"x\",\"error\":{\"message\":\"y\"}}", false).c_str(), "y", "error.message を優先する");
    eq_str(parseError("<html>\r\n<body>Bad Gateway</body>", false).c_str(), "<html>", "JSONでなければ1行目");
    eq_str(parseError("{\"ok\":false}", false).c_str(), "", "理由の無いJSONなら空");

    char msg[160];
    Transcribe::DescribeFailure(msg, sizeof(msg), 401, "bad key");
    eq_str(msg, "APIキーが違います(HTTP 401: bad key)", "401の説明");
    Transcribe::DescribeFailure(msg, sizeof(msg), 503, "");
    eq_str(msg, "相手のサーバの不調です(HTTP 503)", "5xxの説明(理由なし)");

    char out[64];
    check(TranscribeClient::TextPathFor("/recordings/rec_0001.wav", out, sizeof(out)), ".wav から .txt を作れる");
    eq_str(out, "/recordings/rec_0001.txt", ".txt のパス");
    check(TranscribeClient::TextPathFor("/x/A.WAV", out, sizeof(out)), "大文字の .WAV も");
    check(!TranscribeClient::TextPathFor("/x/a.mp3", out, sizeof(out)), ".wav でなければ作らない");
    check(!TranscribeClient::TextPathFor("/recordings/rec_0001.wav", out, 10), "入りきらなければ作らない");
}

static void testConfig(){
    printf("\n--- 設定 ---\n");
    using S = TranscribeClient::State;
    TranscribeClient::Config cfg;
    {
        TranscribeClient c;
        check(!c.configure(cfg, "") && c.state() == S::NotConfigured, "既定(OpenAI)はキーが要る");
        check(strstr(c.message(), "APIキーがありません") != nullptr, "キーが無いことを案内する");
        eq_str(c.config().model.c_str(), TranscribeClient::kDefaultModel, "model の既定");
    }
    {
        TranscribeClient c;
        cfg.api.assign("http://192.168.1.10:8080/v1/");
        check(c.configure(cfg, nullptr) && c.state() == S::Ready, "手元のサーバならキー無しでよい");
    }
    {
        TranscribeClient c;
        check(!c.configure(cfg, "sk-abc def"), "キーに空白があれば断る");
        check(!c.configure(cfg, "sk-abc\r\nX-Evil: 1"), "キーに改行があれば断る");
        check(c.configure(cfg, "sk-proj-AbC_123.xyz"), "英数字と - _ . のキーは通る");
    }
    {
        TranscribeClient c;
        cfg.api.assign("not a url");
        check(!c.configure(cfg, "sk-1") && strstr(c.message(), "URL") != nullptr, "api が URL でなければ断る");
    }

    //ファイルから読む。平文の1行のキーは暗号化して書き直す
    OSData::SD_usable = true;
    HostSd::files.clear();
    std::string key = "sk-proj-";
    for(int i = 0; i < 300; i++) key += (char)('A' + i % 26);   // 本物(約164文字)より長くして、行に分かれるのを見る
    HostSd::files[PICO_Path::FILE::CFG::SYS_TRANSCRIBE_CFG] =
        "# テスト\napi = https://api.groq.com/openai/v1\nmodel = whisper-large-v3-turbo\nlanguage =\nprompt = 議事録\n";
    HostSd::files[PICO_Path::FILE::CFG::SYS_TRANSCRIBE_KEY] = key + "\n";
    {
        TranscribeClient c;
        check(c.loadConfig() && c.state() == S::Ready, "設定ファイルから読める");
        eq_str(c.config().api.c_str(), "https://api.groq.com/openai/v1", "api");
        eq_str(c.config().model.c_str(), "whisper-large-v3-turbo", "model");
        eq_str(c.config().language.c_str(), "", "language を空にすると自動判定");
        eq_str(c.config().prompt.c_str(), "議事録", "prompt");
        const std::string saved = HostSd::files[PICO_Path::FILE::CFG::SYS_TRANSCRIBE_KEY];
        check(saved.find("sk-proj-") == std::string::npos && saved.find("enc") != std::string::npos, "平文のキーは暗号化して書き直す");
        int lines = 0;
        for(char ch : saved) if(ch == '\n') lines++;
        check(lines == 1 + 2, "255Bを超えるキーは2行に分ける(見出し+2行)");
    }
    {
        TranscribeClient c;
        check(c.loadConfig() && c.state() == S::Ready, "暗号化したキーを読み戻せる");
    }
    HostSd::files.erase(PICO_Path::FILE::CFG::SYS_TRANSCRIBE_CFG);
    HostSd::files.erase(PICO_Path::FILE::CFG::SYS_TRANSCRIBE_KEY);
    {
        TranscribeClient c;
        check(!c.loadConfig() && c.state() == S::NotConfigured, "どちらも無ければ送れない");
        eq_str(c.config().language.c_str(), "ja", "language の既定は ja");
    }
    check(TranscribeClient::SaveKey("gsk_abc123") && HostSd::files.count(PICO_Path::FILE::CFG::SYS_TRANSCRIBE_KEY) == 1,
          "画面から入れたキーを保存できる");
    check(!TranscribeClient::SaveKey("bad key"), "使えない文字のキーは保存しない");

    //画面から入れる送り先(ホスト)
    {
        char out[PICO_STR_LL];
        check(TranscribeClient::NormalizeApi("192.168.1.10:8080", out, sizeof(out)), "ホスト:ポートだけでも通る");
        eq_str(out, "https://192.168.1.10:8080/v1", "scheme が無ければ https、パスが無ければ /v1");
        check(TranscribeClient::NormalizeApi("  http://pc.local:8080/  ", out, sizeof(out)), "前後の空白と末尾の / は落とす");
        eq_str(out, "http://pc.local:8080/v1", "http:// は書いたとおり");
        check(TranscribeClient::NormalizeApi("https://api.groq.com/openai/v1/", out, sizeof(out)), "パスがあればそのまま");
        eq_str(out, "https://api.groq.com/openai/v1", "末尾の / だけ落とす");
        check(!TranscribeClient::NormalizeApi("", out, sizeof(out)), "空は断る");
        check(!TranscribeClient::NormalizeApi("   ", out, sizeof(out)), "空白だけは断る");
        check(!TranscribeClient::NormalizeApi("a b", out, sizeof(out)), "中に空白があれば断る");
        check(!TranscribeClient::NormalizeApi("host\r\nX: 1", out, sizeof(out)), "改行があれば断る");
        check(!TranscribeClient::NormalizeApi("http://h\"x", out, sizeof(out)), "\" があれば断る");
        check(!TranscribeClient::NormalizeApi("192.168.1.10", out, 8), "入りきらなければ断る");
        std::string longhost = "http://";
        longhost.append(300, 'a');
        check(!TranscribeClient::NormalizeApi(longhost.c_str(), out, sizeof(out)), "長すぎれば断る");
    }
    HostSd::files.erase(PICO_Path::FILE::CFG::SYS_TRANSCRIBE_CFG);
    check(TranscribeClient::SaveApi("192.168.1.10:8080"), "送り先を保存できる");
    {
        TranscribeClient c;
        check(c.loadConfig() && c.state() == S::Ready, "手元のサーバならキー無しで読める");
        eq_str(c.config().api.c_str(), "https://192.168.1.10:8080/v1", "保存した api を読み戻せる");
    }
    check(!TranscribeClient::SaveApi("bad host") , "使えない送り先は保存しない");
    check(TranscribeClient::SaveApi("") , "空なら既定へ戻す");
    {
        TranscribeClient c;
        c.loadConfig();
        eq_str(c.config().api.c_str(), TranscribeClient::kDefaultApi, "既定(OpenAI)へ戻る");
    }
    OSData::SD_usable = false;
    {
        TranscribeClient c;
        check(!c.loadConfig() && strstr(c.message(), "SD") != nullptr, "SDが無ければ案内する");
    }
    OSData::SD_usable = true;
}

static std::string makeWav(size_t data_bytes){
    std::string w = "RIFF";
    w.append(40, '\0');
    for(size_t i = 0; i < data_bytes; i++) w += (char)((i * 7 + 3) & 0xFF);
    return w;
}

static void testBody(){
    printf("\n--- 送る本文 ---\n");
    using S = TranscribeClient::State;
    OSData::SD_usable = true;
    HostSd::files.clear();
    const std::string wav = makeWav(5000);
    HostSd::files["/recordings/rec_0002.wav"] = wav;

    TranscribeClient::Config cfg;
    cfg.api.assign("http://127.0.0.1:1/v1");
    cfg.prompt.assign("テスト");
    TranscribeClient c;
    check(c.configure(cfg, "sk-1"), "設定できる");
    check(c.start("/recordings/rec_0002.wav") && c.state() == S::Sending, "送り始められる");
    eq_str(c.textPath(), "/recordings/rec_0002.txt", "文章の置き場所");

    //半端な大きさで読み切る
    std::string got;
    uint8_t buf[333];
    for(;;){
        const int n = c.read(buf, sizeof(buf));
        if(n <= 0) break;
        got.append((const char*)buf, (size_t)n);
    }
    check(got.size() == c.size(), "size() の分だけ読める");
    const size_t file_at = got.find("Content-Type: audio/wav\r\n\r\n");
    check(file_at != std::string::npos && got.compare(file_at + 27, wav.size(), wav) == 0, "WAVがそのまま入る");
    check(got.find("name=\"prompt\"\r\n\r\nテスト\r\n") != std::string::npos, "prompt が入る");
    const size_t tail_at = file_at + 27 + wav.size();
    check(got.compare(tail_at, 4, "\r\n--") == 0 && got.substr(got.size() - 4) == "--\r\n", "後ろの区切りで終わる");
    const std::string boundary = got.substr(2, got.find("\r\n") - 2);
    check(got.find("--" + boundary + "--") == tail_at + 2, "終わりの区切りが前と同じ");

    check(c.rewind(), "読み直せる");
    std::string again;
    for(;;){
        const int n = c.read(buf, 100);
        if(n <= 0) break;
        again.append((const char*)buf, (size_t)n);
    }
    check(again == got, "読み直しても同じ");

    check(!c.start("/recordings/rec_0002.wav"), "送っている間はもう1本始めない");
    c.cancel();
    check(c.state() == S::Ready && !c.busy(), "やめると送れる状態へ戻る");
    check(HostSd::files.count("/recordings/rec_0002.txt.part") == 0, "書きかけを残さない");

    check(!c.start("/recordings/none.wav") && c.state() == S::Failed && strstr(c.message(), "開けません"), "無いファイル");
    HostSd::files["/recordings/empty.wav"] = makeWav(0);
    check(!c.start("/recordings/empty.wav") && strstr(c.message(), "空") != nullptr, "空の録音");
    check(!c.start("/recordings/a.mp3") && strstr(c.message(), "WAV") != nullptr, "WAVでない");
    HostSd::files["/recordings/big.wav"] = std::string(TranscribeClient::kMaxUploadBytes + 1, 'x');
    check(!c.start("/recordings/big.wav") && strstr(c.message(), "大きすぎます") != nullptr, "25MBを超える");
    HostSd::files.erase("/recordings/big.wav");

    TranscribeClient none;
    check(!none.start("/recordings/rec_0002.wav") && none.state() == S::NotConfigured, "設定が無ければ始めない");
}

int main(){
    testMultipart();
    testErrors();
    testConfig();
    testBody();
    printf("\n%s (失敗 %d件)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
