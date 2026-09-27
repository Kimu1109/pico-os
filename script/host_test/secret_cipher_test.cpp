// Wi-Fi SSID/パスワードの暗号化保存(util/Secret_Cipher.hpp)を検証するテスト。
//
// 「SDカードだけを紛失/盗難されても network.cfg の平文が読めない」という
// 限定的な脅威モデルへの対処であることは CLAUDE.md 参照。ここでは往復・
// 用途による鍵ストリームの違い・後方互換(平文のまま読める)・壊れた
// データへの安全な失敗を確かめる。
#include "util/Secret_Cipher.hpp"
#include <cstdio>
#include <cstring>
#include <string>

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}
static void eqStr(const std::string& actual, const std::string& expected, const char* label){
    const bool ok = (actual == expected);
    printf("%s %s\n", ok ? "[ OK ]" : "[FAIL]", label);
    if(!ok){
        printf("       実測: [%s]\n", actual.c_str());
        printf("       期待: [%s]\n", expected.c_str());
        failures++;
    }
}

int main(){
    // ---- 往復: 暗号化してから復号すると元に戻る ----
    {
        char enc[256];
        check(PICO_Secret::Encrypt("wifi-password", "my-secret-pass", enc, sizeof(enc)),
              "パスワードの暗号化が成功する");
        check(strncmp(enc, "enc1:", 5) == 0, "暗号化後は enc1: 接頭辞が付く");
        check(strstr(enc, "my-secret-pass") == nullptr, "暗号文に平文がそのまま含まれない");

        char dec[256];
        check(PICO_Secret::Decrypt("wifi-password", enc, dec, sizeof(dec)), "復号が成功する");
        eqStr(dec, "my-secret-pass", "復号すると元の平文に戻る");
    }

    // ---- 用途(purpose)が違えば同じ平文でも暗号文が変わる ----
    {
        char encA[256], encB[256];
        PICO_Secret::Encrypt("wifi-ssid", "same-text", encA, sizeof(encA));
        PICO_Secret::Encrypt("wifi-password", "same-text", encB, sizeof(encB));
        check(strcmp(encA, encB) != 0, "用途が違えば同じ平文でも暗号文が異なる");

        // 取り違えた用途で復号すると、元の平文には戻らない(鍵ストリームが違うため)
        char wrong[256];
        PICO_Secret::Decrypt("wifi-ssid", encB, wrong, sizeof(wrong));
        check(strcmp(wrong, "same-text") != 0, "用途を取り違えると正しく復号できない");
    }

    // ---- 空文字列は暗号化せずそのまま(「未設定」の意味を保つ) ----
    {
        char enc[64];
        check(PICO_Secret::Encrypt("wifi-ssid", "", enc, sizeof(enc)), "空文字列の暗号化は成功扱い");
        eqStr(enc, "", "空文字列は暗号化されずそのまま");

        char dec[64];
        check(PICO_Secret::Decrypt("wifi-ssid", "", dec, sizeof(dec)), "空文字列の復号も成功扱い");
        eqStr(dec, "", "空文字列を復号しても空文字列のまま");
    }

    // ---- 後方互換: enc1:接頭辞が無い値は平文としてそのまま返る ----
    {
        char dec[64];
        check(PICO_Secret::Decrypt("wifi-ssid", "old-plain-ssid", dec, sizeof(dec)),
              "接頭辞の無い値は成功扱いで読める(後方互換)");
        eqStr(dec, "old-plain-ssid", "接頭辞の無い値はそのまま返る");
    }

    // ---- 壊れたデータへの安全な失敗 ----
    {
        char dec[64];
        check(!PICO_Secret::Decrypt("wifi-ssid", "enc1:abc", dec, sizeof(dec)),
              "16進が奇数長なら復号は失敗する");
        eqStr(dec, "", "失敗時は空文字列を書く");

        check(!PICO_Secret::Decrypt("wifi-ssid", "enc1:zz", dec, sizeof(dec)),
              "16進として無効な文字なら復号は失敗する");
    }

    // ---- 出力バッファ不足への安全な失敗 ----
    {
        char enc[256];
        PICO_Secret::Encrypt("wifi-password", "0123456789abcdef", enc, sizeof(enc));

        char tiny[4];
        check(!PICO_Secret::Decrypt("wifi-password", enc, tiny, sizeof(tiny)),
              "復号先バッファが小さすぎれば失敗する");

        char tinyOut[4];
        check(!PICO_Secret::Encrypt("wifi-password", "0123456789abcdef", tinyOut, sizeof(tinyOut)),
              "暗号化先バッファが小さすぎれば失敗する");
    }

    // ---- 平文の上限(kMaxPlainBytes)を超えると暗号化は失敗する ----
    {
        char longPlain[PICO_Secret::kMaxPlainBytes + 8];
        memset(longPlain, 'a', sizeof(longPlain) - 1);
        longPlain[sizeof(longPlain) - 1] = '\0';

        char enc[256];
        check(!PICO_Secret::Encrypt("wifi-password", longPlain, enc, sizeof(enc)),
              "上限を超える平文は暗号化を拒否する");
    }

    // ---- 上限ちょうどの長さは暗号化・復号できる ----
    {
        char maxPlain[PICO_Secret::kMaxPlainBytes + 1];
        memset(maxPlain, 'x', PICO_Secret::kMaxPlainBytes);
        maxPlain[PICO_Secret::kMaxPlainBytes] = '\0';

        char enc[256];
        check(PICO_Secret::Encrypt("wifi-password", maxPlain, enc, sizeof(enc)),
              "上限ちょうどの平文は暗号化できる");

        char dec[256];
        check(PICO_Secret::Decrypt("wifi-password", enc, dec, sizeof(dec)),
              "上限ちょうどの平文を復号できる");
        eqStr(dec, maxPlain, "上限ちょうどの平文が正しく往復する");
    }

    printf("\n%s\n", failures == 0 ? "ALL PASSED" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
