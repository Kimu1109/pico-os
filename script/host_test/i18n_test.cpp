// 表示言語(I18nFunctions)のテスト(日本語の版。ホストテストは既定の日本語の版で作る)。
//
// ハッシュが script/i18n.py と同じ計算か、TR()/Tr() が原文をそのまま返すか、言語コードを押さえる。
// 英語の版(PICO_LANG_EN=1)の引き当ては i18n_en_test。
#include "system/I18n_Functions.hpp"
#include "config/Lang_Config.hpp"
#include <cstdio>
#include <cstring>

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

int main(){
    static_assert(PICO_LANG_JA == 1 && PICO_LANG_EN == 0, "ホストテストは日本語の版で作る");
    static_assert(I18nFunctions::Current() == Lang::Ja, "このビルドの言語は日本語");

    // --- ハッシュ(FNV-1a 32bit。script/i18n.py の fnv1a と同じ値) ---
    check(I18nFunctions::Hash("") == 2166136261u, "空文字のハッシュはFNVの初期値");
    check(I18nFunctions::Hash("a") == 0xe40c292cu, "\"a\" のハッシュ");
    static_assert(I18nFunctions::Hash("設定") != 0, "TR() のためにコンパイル時に数えられる");

    // --- 日本語の版は原文のまま(表を持たない) ---
    eqStr(TR("設定"), "設定", "TR() は原文");
    char buf[64];
    snprintf(buf, sizeof(buf), TR("バックグラウンド: %d件"), 3);
    eqStr(buf, "バックグラウンド: 3件", "書式つきの TR()");
    const char* src = "時計";
    check(I18nFunctions::Tr(src) == src, "Tr() は渡したポインタをそのまま返す");
    check(I18nFunctions::Tr(nullptr) == nullptr, "Tr(nullptr) は nullptr");

    // --- 言語コード ---
    eqStr(I18nFunctions::Code(Lang::Ja), "ja", "Code(Ja)");
    eqStr(I18nFunctions::Code(Lang::En), "en", "Code(En)");

    printf("\n%s (失敗 %d)\n", failures == 0 ? "すべて成功" : "失敗あり", failures);
    return failures == 0 ? 0 : 1;
}
