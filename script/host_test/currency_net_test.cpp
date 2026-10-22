// 電卓の通貨レートの取得(CurrencyRates::beginFetch/update)を、本物のソケットで確かめる結合テスト。
// run_net.sh が python3 -m http.server で次のファイルを配る(本物の為替のAPIへは行かない):
//   latest      … Frankfurter の形の応答
//   erapi       … open.er-api.com の形の応答
//   broken.html … JSONでない応答
// 確かめること: 取得→読み取り→SDへの控え / 404 と形式違いでは手元のレートを壊さない / 取得元(url)の差し替え
#include "apps/calculator/Calc_Currency.hpp"
#include "storage/SD_Path.hpp"
#include "system/Log_Functions.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <thread>
#include <chrono>

void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

static int port = 0;
static const char* kPath = PICO_Path::FILE::CFG::SYS_CURRENCY_CFG;

// 取得元を url へ向けて(控えのファイル経由で)取得し、終わるまで回す
static CurrencyRates::State fetch(CurrencyRates& r, const char* path){
    char url[128];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/%s", port, path);
    std::string cfg = std::string("url = ") + url + "\n";
    if(r.hasRates()){
        //今のレートも控えに残したまま url だけ差し替える
        r.save();
        std::string& f = HostSd::files[kPath];
        const size_t at = f.find("url=");
        if(at != std::string::npos) f.erase(at, f.find('\n', at) - at + 1);
        f.insert(0, cfg);
    }else{
        HostSd::files[kPath] = cfg;
    }
    r.load();
    if(!r.beginFetch()) return r.state();
    for(int i = 0; i < 2000 && r.busy(); i++){
        r.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return r.state();
}

int main(int argc, char** argv){
    port = argc > 1 ? atoi(argv[1]) : 8146;
    HostSd::files.clear();

    CurrencyRates r;
    check(fetch(r, "latest") == CurrencyRates::State::Done, "Frankfurterの形を取得できる");
    printf("       %s\n", r.message());
    check(r.count() == 4 && strcmp(r.base(), "EUR") == 0, "4通貨・基準EUR");
    double out = 0;
    check(r.convert(r.find("USD"), r.find("JPY"), 2, out) && out > 315.0 && out < 317.0, "2USD = 316.6JPY");
    check(HostSd::files[kPath].find("JPY=177.3") != std::string::npos, "取得したレートをSDへ控える");
    check(HostSd::files[kPath].find("url=http://127.0.0.1") != std::string::npos, "控えに url の指定が残る");

    check(fetch(r, "missing") == CurrencyRates::State::Failed, "404は失敗");
    printf("       %s\n", r.message());
    check(strstr(r.message(), "404") != nullptr, "失敗の理由にステータスが出る");
    check(r.count() == 4 && r.convert(r.find("USD"), r.find("JPY"), 2, out) && out > 315.0 && out < 317.0,
          "失敗しても手元のレートは壊れない");

    check(fetch(r, "broken.html") == CurrencyRates::State::Failed, "JSONでない応答は失敗");
    printf("       %s\n", r.message());
    check(r.count() == 4, "形式違いでも手元のレートは壊れない");

    check(fetch(r, "erapi") == CurrencyRates::State::Done, "open.er-apiの形を取得できる");
    check(strcmp(r.base(), "JPY") == 0 && r.count() == 3, "基準JPY・3通貨");

    printf("\n%s (%d件の失敗)\n", failures == 0 ? "すべて成功" : "失敗あり", failures);
    return failures == 0 ? 0 : 1;
}
