// 表示言語を1つだけ載せた版(config/Lang_Config.hpp の PICO_LANG_JA / PICO_LANG_EN)のテスト。
//
// 同じファイルを2回作る(CMakeLists.txt): i18n_ja_only_test(PICO_LANG_EN=0)と i18n_en_only_test(PICO_LANG_JA=0)。
// どちらも I18n の .cpp をその定義で自分に入れる(ライブラリの既定の版より先にリンクされる)。
// 起動時の言語・載せていない言語を選ばれたとき・TR() と Tr() の返す文字列を押さえる。
// 英語だけの版で原文のリテラルがフラッシュに残らないことは、実機ビルド(-Os)の .elf を見て確かめる(docs/design/i18n.md)。
#include "system/I18n_Functions.hpp"
#include "config/Lang_Config.hpp"
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

int main(){
    static_assert(PICO_LANG_JA + PICO_LANG_EN == 1, "このテストは言語を1つだけ載せた定義で作る");
    static_assert(I18nFunctions::kAvailableCount == 1, "載せた言語の数");

#if PICO_LANG_JA
    // ---- 日本語だけ ----
    printf("--- 日本語だけの版 ---\n");
    static_assert(I18nFunctions::kDefaultLang == Lang::Ja, "既定は日本語");
    check(I18nFunctions::Available(Lang::Ja) && !I18nFunctions::Available(Lang::En), "Available()");
    check(I18nFunctions::Current() == Lang::Ja, "起動前から日本語");

    I18nFunctions::SetLanguage(Lang::En, false);
    check(I18nFunctions::Current() == Lang::Ja, "SetLanguage(En) は効かない");
    eqStr(TR("設定"), "設定", "TR() は原文");
    eqStr(I18nFunctions::Tr("時計"), "時計", "Tr() は原文");

    setUserCfg("language = en\n");
    I18nFunctions::Setup();
    check(I18nFunctions::Current() == Lang::Ja, "user.cfg の language = en でも日本語で立ち上がる");
    I18nFunctions::override_code = "en";
    I18nFunctions::Setup();
    check(I18nFunctions::Current() == Lang::Ja, "--lang en でも日本語");
    I18nFunctions::override_code = nullptr;
    I18nFunctions::default_code = "fr-FR";
    setUserCfg("");
    I18nFunctions::Setup();
    check(I18nFunctions::Current() == Lang::Ja, "ブラウザの言語が知らない言語でも日本語");
    I18nFunctions::default_code = nullptr;
#else
    // ---- 英語だけ ----
    printf("--- 英語だけの版 ---\n");
    static_assert(I18nFunctions::kDefaultLang == Lang::En, "既定は英語");
    check(I18nFunctions::Available(Lang::En) && !I18nFunctions::Available(Lang::Ja), "Available()");
    check(I18nFunctions::Current() == Lang::En, "起動前から英語(user.cfg を読む前のログ・画面も英語)");

    eqStr(TR("設定"), "Settings", "TR() は表から引く");
    char buf[64];
    snprintf(buf, sizeof(buf), TR("バックグラウンド: %d件"), 3);
    eqStr(buf, "Background: 3", "書式つきの訳");
    eqStr(I18nFunctions::Tr("時計"), "Clock", "Tr()(アプリの登録名)");
    eqStr(I18nFunctions::Tr("MyLuaApp"), "MyLuaApp", "Tr(): 表に無いもの(Luaアプリの名前)はそのまま");
    eqStr(I18nFunctions::LookupHash(0x12345678u), "", "表に無いハッシュは空文字(表が古いときだけ)");

    // 訳の無い原文も表に入っている(script/i18n.py が日本語を外した版のために原文のまま入れる)。
    // 表の数は「訳のある原文」以上で、並びは昇順
    bool sorted = true;
    for(size_t i = 1; i < I18nFunctions::kTableEnCount; i++){
        if(I18nFunctions::kTableEn[i - 1].hash >= I18nFunctions::kTableEn[i].hash) sorted = false;
    }
    check(sorted, "表は hash の昇順で重複が無い");
    check(I18nFunctions::kTableEn[I18nFunctions::kTableEnCount].text == nullptr, "番兵は数に入らない");

    I18nFunctions::SetLanguage(Lang::Ja, false);
    check(I18nFunctions::Current() == Lang::En, "SetLanguage(Ja) は効かない");

    setUserCfg("language = ja\n");
    I18nFunctions::Setup();
    check(I18nFunctions::Current() == Lang::En, "user.cfg の language = ja でも英語で立ち上がる");
    I18nFunctions::override_code = "ja";
    I18nFunctions::Setup();
    check(I18nFunctions::Current() == Lang::En, "--lang ja でも英語");
    I18nFunctions::override_code = nullptr;
    I18nFunctions::default_code = "ja-JP";
    setUserCfg("");
    I18nFunctions::Setup();
    check(I18nFunctions::Current() == Lang::En, "ブラウザの言語が日本語でも英語");
    I18nFunctions::default_code = nullptr;
#endif

    printf("\n%s (失敗 %d)\n", failures == 0 ? "すべて成功" : "失敗あり", failures);
    return failures == 0 ? 0 : 1;
}
