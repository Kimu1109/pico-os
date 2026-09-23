// カレンダーの取得(CalendarSync)をHTTPS越しに確かめる結合テスト。run_net.sh から呼ぶ。
//
// **本物のソケットとTLS(OpenSSL、pc/compat/WiFiClientSecure_PC.h)を使う。**
// 相手は script/host_test/tls_test_server.py(使い捨ての証明書で立てたHTTPSサーバ)。
//
// 見ること:
//   - https の取得元から .ics を取ってきて /calendar/<名前>.ics に置く(chunked転送を解く)
//   - webcal:// を https:// として扱う
//   - ETagを覚え、2回目は条件付きGETで304になる(ファイルに触らない)
//   - HTMLや404では手元の .ics を壊さない
//   - 信頼していない証明書・名前の違う証明書では繋がらない(手元の .ics はそのまま)
//
// 使い方: calendar_sync_test <port> <テスト用CAのPEMファイル>
#include "calendar/Calendar_Sync.hpp"
#include "functions/Log_Functions.hpp"
#include "storage/SD_Path.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

// ---- モック ----
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
static void eq_int(long a, long e, const char* label){
    const bool ok = (a == e);
    printf("%s %-46s 実測=%ld 期待=%ld\n", ok ? "[ OK ]" : "[FAIL]", label, a, e);
    if(!ok) failures++;
}

static void runSync(CalendarSync& sync){
    check(sync.begin(), "取得を始められる");
    //millis()はスタブで進まないので、回数で打ち切る(1回1msで最大10秒)
    for(int i = 0; i < 10000 && sync.state() == CalendarSync::State::Running; i++){
        sync.update();
        usleep(1000);
    }
    check(sync.state() == CalendarSync::State::Done, "全件の取得が終わる");
}

static bool has(const char* path){ return HostSd::files.count(path) > 0; }
static const std::string& file(const char* path){ return HostSd::files[path]; }

int main(int argc, char** argv){
    if(argc < 3){
        fprintf(stderr, "使い方: %s <port> <ca.pem>\n", argv[0]);
        return 2;
    }
    const std::string port = argv[1];
    std::ifstream ca_in(argv[2]);
    std::stringstream ca;
    ca << ca_in.rdbuf();

    OSData::SD_usable = true;
    HostSd::files[PICO_Path::FILE::TLS_EXTRA_CA_PEM] = ca.str();

    const std::string base = "https://localhost:" + port;
    HostSd::files[PICO_Path::FILE::CALENDAR_SOURCES] =
        "# テスト用の取得元\n"
        "good = " + base + "/cal.ics\n"
        "web = webcal://localhost:" + port + "/cal.ics\n"
        "html = " + base + "/html\n"
        "missing = " + base + "/missing\n"
        "日本語 = " + base + "/cal.ics\n"      // 名前がファイル名に使えないので数えない
        "壊れた行\n";

    eq_int(CalendarSync::CountSources(), 4, "書式の正しい取得元だけを数える");

    // 手元に古い .ics がある状態から始める(HTML/404で壊さないことを見るため)
    HostSd::files["/calendar/html.ics"] = "BEGIN:VCALENDAR\r\nOLD\r\nEND:VCALENDAR\r\n";

    CalendarSync sync;

    printf("---- 1回目: 取ってくる ----\n");
    runSync(sync);
    eq_int(sync.updatedCount(), 2, "https と webcal の2件を更新する");
    eq_int(sync.failedCount(), 2, "HTML と 404 の2件は失敗する");
    check(has("/calendar/good.ics") && file("/calendar/good.ics").find("HTTPSで取ってきた予定") != std::string::npos,
          "good.ics に中身が入る(chunked転送を解いてある)");
    check(file("/calendar/good.ics").rfind("BEGIN:VCALENDAR", 0) == 0, "サイズ行が混ざっていない");
    check(has("/calendar/web.ics"), "webcal:// を https:// として取れる");
    check(has("/calendar/good.etag") && file("/calendar/good.etag") == "v1\n", "ETagを覚える");
    check(file("/calendar/html.ics").find("OLD") != std::string::npos, "HTMLを返されても手元の .ics は壊さない");
    check(!has("/calendar/missing.ics"), "404なら何も置かない");
    check(!has("/calendar/good.ics.part") && !has("/calendar/html.ics.part"), "書きかけのファイルを残さない");

    printf("---- 2回目: 変わっていなければ304 ----\n");
    HostSd::files["/calendar/good.ics"] += "MARK";   // 触られたら消える印
    runSync(sync);
    eq_int(sync.updatedCount(), 0, "304なので差し替えない");
    check(file("/calendar/good.ics").find("MARK") != std::string::npos, "手元のファイルに触らない");

    printf("---- 信頼していない証明書 ----\n");
    HostSd::files.erase(PICO_Path::FILE::TLS_EXTRA_CA_PEM);
    runSync(sync);
    eq_int(sync.updatedCount(), 0, "テスト用CAを外すと繋がらない");
    eq_int(sync.failedCount(), 4, "全件失敗する");
    check(strstr(sync.lastError(), "証明書") != nullptr, "理由に「証明書」が出る");
    check(file("/calendar/good.ics").find("MARK") != std::string::npos, "繋がらなくても手元の .ics はそのまま");

    printf("---- 名前の違う証明書 ----\n");
    HostSd::files[PICO_Path::FILE::TLS_EXTRA_CA_PEM] = ca.str();
    HostSd::files[PICO_Path::FILE::CALENDAR_SOURCES] = "ip = https://127.0.0.1:" + port + "/cal.ics\n";
    runSync(sync);
    eq_int(sync.failedCount(), 1, "証明書の名前(localhost)と違うホスト名では繋がらない");
    check(!has("/calendar/ip.ics"), "何も置かない");

    printf("\n%s (失敗 %d件)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
