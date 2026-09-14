// パスの正規化と相対解決(PICO_IO::normalize / PICO_IO::resolve)のテスト。
//
// Markdownブラウザがリンクを辿るときの土台。ここがずれると、"../" を含むリンクが
// 別の文書を開いたり、SDのルート外を指したりする。純粋な文字列処理なので
// SDもネットワークも要らず、ホスト側で全部確かめられる。
#include "storage/SD_IO.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"
#include <cstdio>
#include <cstring>

static int failures = 0;

static void eq_str(const char* actual, const char* expected, const char* label){
    const bool ok = (strcmp(actual, expected) == 0);
    printf("%s %-44s 実測=%-26s 期待=%s\n", ok ? "[ OK ]" : "[FAIL]", label, actual, expected);
    if(!ok) failures++;
}

static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

static void norm(const char* input, const char* expected, const char* label){
    FixedString<PICO_PATH_LEN> out;
    const bool ok = PICO_IO::normalize(out, input);
    if(!ok){
        printf("[FAIL] %-44s normalize()が失敗しました (入力=%s)\n", label, input);
        failures++;
        return;
    }
    eq_str(out.c_str(), expected, label);
}

static void res(const char* base, const char* ref, const char* expected, const char* label){
    FixedString<PICO_PATH_LEN> out;
    const bool ok = PICO_IO::resolve(out, base, ref);
    if(!ok){
        printf("[FAIL] %-44s resolve()が失敗しました (base=%s ref=%s)\n", label, base, ref);
        failures++;
        return;
    }
    eq_str(out.c_str(), expected, label);
}

int main(){
    printf("---- normalize ----\n");
    norm("/docs/pico/intro.md", "/docs/pico/intro.md", "既に正規形ならそのまま");
    norm("docs/pico/intro.md",  "/docs/pico/intro.md", "先頭のスラッシュを補う");
    norm("/docs//pico///intro.md", "/docs/pico/intro.md", "連続するスラッシュをまとめる");
    norm("/docs/./pico/intro.md", "/docs/pico/intro.md", "\".\"を捨てる");
    norm("/docs/pico/../intro.md", "/docs/intro.md", "\"..\"で1つ戻る");
    norm("/a/b/c/../../d.md", "/a/d.md", "\"..\"が連続しても戻れる");
    norm("/docs/pico/", "/docs/pico", "末尾のスラッシュを落とす");
    norm("/", "/", "ルートはルートのまま");
    norm("", "/", "空文字はルート");
    norm("///", "/", "スラッシュだけならルート");

    // ルート外へ出させない(SDのルートより上は存在しないため)
    norm("/../etc/passwd", "/etc/passwd", "ルートを超える\"..\"は捨てる");
    norm("../../../a.md", "/a.md", "先頭の\"..\"が何個あってもルート止まり");
    norm("/a/../../b.md", "/b.md", "途中でルートを超えても止まる");

    printf("\n---- resolve ----\n");
    res("/docs/pico/intro.md", "gpio.md", "/docs/pico/gpio.md", "同じディレクトリの文書");
    res("/docs/pico/intro.md", "./gpio.md", "/docs/pico/gpio.md", "\"./\"付き");
    res("/docs/pico/intro.md", "../setup.md", "/docs/setup.md", "1つ上のディレクトリ");
    res("/docs/pico/intro.md", "../../index.md", "/index.md", "2つ上のディレクトリ");
    res("/docs/pico/intro.md", "sub/detail.md", "/docs/pico/sub/detail.md", "下の階層へ");
    res("/docs/pico/intro.md", "/index.md", "/index.md", "\"/\"始まりはルート基準");
    res("/index.md", "docs/a.md", "/docs/a.md", "ルート直下の文書からの相対");

    // 解決結果がルート外へ出ないこと(リンク先が悪意/誤りでもSDの外は指さない)
    res("/docs/a.md", "../../../../etc/passwd", "/etc/passwd", "上へ辿りすぎてもルート止まり");

    printf("\n---- 異常系 ----\n");
    {
        FixedString<PICO_PATH_LEN> out;
        check(!PICO_IO::resolve(out, "/docs/a.md", ""), "空の参照は失敗する");
        check(!PICO_IO::resolve(out, "/docs/a.md", nullptr), "nullptrの参照は失敗する");
    }
    {
        //PICO_PATH_LEN(255B)へ収まらない参照は失敗すること。
        //黙って切り詰めると別のファイルを指してしまう
        char longRef[PICO_PATH_LEN * 2];
        memset(longRef, 'a', sizeof(longRef) - 1);
        longRef[sizeof(longRef) - 1] = '\0';

        FixedString<PICO_PATH_LEN> out;
        check(!PICO_IO::resolve(out, "/docs/a.md", longRef), "長すぎる参照は失敗する");
    }
    {
        //セグメントが多すぎる場合も失敗すること(固定長配列の上限)
        FixedString<PICO_PATH_LEN> deep;
        for(int i = 0; i < 40; i++) deep.append("/a");

        FixedString<PICO_PATH_LEN> out;
        check(!PICO_IO::normalize(out, deep.c_str()), "セグメントが多すぎる場合は失敗する");
    }

    printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
