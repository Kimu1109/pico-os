// 英語の版(config/Lang_Config.hpp の PICO_LANG_EN=1)の I18nFunctions のテスト。
//
// ホストテストのライブラリは日本語の版なので、このテストだけ PICO_LANG_EN=1 で作り、
// I18n_Functions.cpp と訳の表をその定義で自分に入れる(ライブラリの日本語の版より先にリンクされる。CMakeLists.txt)。
// 英語の版で原文のリテラルがフラッシュに残らないことは、実機ビルド(-Os)の firmware.bin を見て確かめる(docs/design/i18n.md)。
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
    static_assert(PICO_LANG_EN == 1 && PICO_LANG_JA == 0, "このテストは英語の版の定義で作る");
    static_assert(I18nFunctions::Current() == Lang::En, "このビルドの言語は英語");

    // --- 表は hash の昇順で重複が無い(二分探索の前提)。番兵は数に入らない ---
    bool sorted = true;
    for(size_t i = 1; i < I18nFunctions::kTableEnCount; i++){
        if(I18nFunctions::kTableEn[i - 1].hash >= I18nFunctions::kTableEn[i].hash) sorted = false;
    }
    check(sorted, "表は hash の昇順で重複が無い");
    check(I18nFunctions::kTableEnCount > 0, "表が空でない");
    check(I18nFunctions::kTableEn[I18nFunctions::kTableEnCount].text == nullptr, "番兵は数に入らない");

    // --- 引き当て ---
    eqStr(TR("設定"), "Settings", "TR(\"設定\")");
    eqStr(TR("キャンセル"), "Cancel", "TR(\"キャンセル\")");
    char buf[64];
    snprintf(buf, sizeof(buf), TR("バックグラウンド: %d件"), 3);
    eqStr(buf, "Background: 3", "書式つきの訳");
    eqStr(I18nFunctions::Tr("時計"), "Clock", "Tr()(アプリの登録名)");
    eqStr(I18nFunctions::Tr("MyLuaApp"), "MyLuaApp", "Tr(): 表に無いもの(Luaアプリの名前)はそのまま");
    check(I18nFunctions::Tr(nullptr) == nullptr, "Tr(nullptr) は nullptr");
    eqStr(I18nFunctions::LookupHash(0x12345678u), "", "表に無いハッシュは空文字(表が古いときだけ)");
    eqStr(I18nFunctions::Code(I18nFunctions::Current()), "en", "Code(Current())");

    printf("\n%s (失敗 %d)\n", failures == 0 ? "すべて成功" : "失敗あり", failures);
    return failures == 0 ? 0 : 1;
}
