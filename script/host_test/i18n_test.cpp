// 表示言語(I18nFunctions)のテスト。
//
// ハッシュが script/i18n.py と同じ計算か、訳の表が二分探索できる並びか、言語ごとの引き当てと
// 訳の無い原文の扱い、/sys/user.cfg の language・PCの上書き・ブラウザの言語の優先順位を押さえる。
#include "system/I18n_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "storage/SD_Path.hpp"
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
static void eqStr(const char* actual, const char* expected, const char* label){
    const bool ok = actual && strcmp(actual, expected) == 0;
    printf("%s %s\n", ok ? "[ OK ]" : "[FAIL]", label);
    if(!ok){
        printf("       実測: [%s]\n", actual ? actual : "(null)");
        printf("       期待: [%s]\n", expected);
        failures++;
    }
}

using I18nFunctions::Lang;

static void setUserCfg(const std::string& body){ HostSd::files[PICO_Path::FILE::CFG::SYS_USER_CFG] = body; }
static void resetOverrides(){
    I18nFunctions::override_code = nullptr;
    I18nFunctions::default_code = nullptr;
}

int main(){
    // --- ハッシュ(FNV-1a 32bit。script/i18n.py の fnv1a と同じ値) ---
    check(I18nFunctions::Hash("") == 2166136261u, "空文字のハッシュはFNVの初期値");
    check(I18nFunctions::Hash("a") == 0xe40c292cu, "\"a\" のハッシュ");
    static_assert(I18nFunctions::Hash("設定") != 0, "TR() のためにコンパイル時に数えられる");

    // --- 表は hash の昇順で重複が無い(Lookup() の二分探索の前提) ---
    bool sorted = true;
    for(size_t i = 1; i < I18nFunctions::kTableEnCount; i++){
        if(I18nFunctions::kTableEn[i - 1].hash >= I18nFunctions::kTableEn[i].hash) sorted = false;
    }
    check(sorted, "英語の表は hash の昇順で重複が無い");
    check(I18nFunctions::kTableEnCount > 0, "英語の表が空でない");

    // --- 日本語は原文のまま(表を見ない) ---
    I18nFunctions::SetLanguage(Lang::Ja, false);
    const char* src = "設定";
    eqStr(TR("設定"), "設定", "日本語: TR() は原文");
    check(I18nFunctions::Tr(src) == src, "日本語: Tr() は渡したポインタをそのまま返す");

    // --- 英語 ---
    I18nFunctions::SetLanguage(Lang::En, false);
    check(I18nFunctions::Current() == Lang::En, "SetLanguage(En) で英語になる");
    eqStr(TR("設定"), "Settings", "英語: TR(\"設定\")");
    eqStr(TR("キャンセル"), "Cancel", "英語: TR(\"キャンセル\")");
    eqStr(I18nFunctions::Tr("時計"), "Clock", "英語: 実行時の Tr()(アプリの登録名)");
    eqStr(I18nFunctions::Tr("訳の無い文字列"), "訳の無い文字列", "英語: 訳の無い原文は日本語のまま");
    eqStr(I18nFunctions::Tr("MyLuaApp"), "MyLuaApp", "英語: Luaアプリの名前等はそのまま");
    check(I18nFunctions::Tr(nullptr) == nullptr, "Tr(nullptr) は nullptr");
    char buf[64];
    snprintf(buf, sizeof(buf), TR("バックグラウンド: %d件"), 3);
    eqStr(buf, "Background: 3", "英語: 書式つきの訳");

    // --- 言語コード ---
    Lang l = Lang::Ja;
    check(I18nFunctions::FromCode("en", l) && l == Lang::En, "FromCode(\"en\")");
    check(I18nFunctions::FromCode("ja", l) && l == Lang::Ja, "FromCode(\"ja\")");
    check(I18nFunctions::FromCode("en-US", l) && l == Lang::En, "FromCode(\"en-US\")");
    check(I18nFunctions::FromCode("JA_jp", l) && l == Lang::Ja, "FromCode(\"JA_jp\")(大文字・_ 区切り)");
    check(!I18nFunctions::FromCode("fr", l), "FromCode(\"fr\") は知らない言語");
    check(!I18nFunctions::FromCode("english", l), "FromCode(\"english\") は読まない");
    check(!I18nFunctions::FromCode("", l) && !I18nFunctions::FromCode(nullptr, l), "空・nullptr");
    eqStr(I18nFunctions::Code(Lang::En), "en", "Code(En)");
    eqStr(I18nFunctions::NativeName(Lang::En), "English", "NativeName(En)");

    // --- 起動時の決め方 ---
    resetOverrides();
    HostSd::files.erase(PICO_Path::FILE::CFG::SYS_USER_CFG);
    I18nFunctions::Setup();
    check(I18nFunctions::Current() == Lang::Ja, "user.cfg が無ければ日本語");

    setUserCfg("run-test = false\nlanguage = en\n");
    I18nFunctions::Setup();
    check(I18nFunctions::Current() == Lang::En, "user.cfg の language = en");

    setUserCfg("language = xx\n");
    I18nFunctions::Setup();
    check(I18nFunctions::Current() == Lang::Ja, "読めない language は日本語");

    I18nFunctions::default_code = "fr-FR";
    setUserCfg("run-test = false\n");
    I18nFunctions::Setup();
    check(I18nFunctions::Current() == Lang::En, "ブラウザの言語が知らない言語なら英語");
    I18nFunctions::default_code = "ja-JP";
    I18nFunctions::Setup();
    check(I18nFunctions::Current() == Lang::Ja, "ブラウザの言語が ja-JP なら日本語");
    setUserCfg("language = en\n");
    I18nFunctions::Setup();
    check(I18nFunctions::Current() == Lang::En, "user.cfg はブラウザの言語より優先");

    I18nFunctions::override_code = "ja";
    I18nFunctions::Setup();
    check(I18nFunctions::Current() == Lang::Ja, "--lang / ?lang= は user.cfg より優先");
    resetOverrides();

    // --- 保存 ---
    setUserCfg("run-test = true\n");
    I18nFunctions::SetLanguage(Lang::En, true);
    const std::string body = HostSd::files[PICO_Path::FILE::CFG::SYS_USER_CFG];
    check(body.find("run-test = true") != std::string::npos, "保存しても他のキーは残る");
    check(body.find("language") != std::string::npos && body.find("en") != std::string::npos, "language = en を書く");
    I18nFunctions::SetLanguage(Lang::Ja, false);
    I18nFunctions::Setup();
    check(I18nFunctions::Current() == Lang::En, "書いた値を次の起動で読む");

    printf("\n%s (失敗 %d)\n", failures == 0 ? "すべて成功" : "失敗あり", failures);
    return failures == 0 ? 0 : 1;
}
