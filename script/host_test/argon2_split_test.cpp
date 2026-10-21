// Argon2のブロックごとに確保する版(crypto/Argon2_Split.hpp)。
//   - Monocypherの crypto_argon2() とビット単位で同じ結果になる(ブロック数・パス数・レーン数・種類・
//     鍵とADの有無を振る。4の倍数でないブロック数の切り捨ても含む)。違えば既存の暗号文が読めなくなる
//   - パスワード付きの SecretAead が64KiBの連続した塊を要求しない(1回の確保が4KiB以下。実機で一度でもヒープが散らかると、
//     再起動まで pico.encrypt/decrypt のパスワード付きが失敗し続けた不具合の再発防止)
//   - SecretAead のパスワード付きの往復と、パスワード違いで AuthFailed
#include "crypto/Argon2_Split.hpp"
#include "crypto/Secret_Aead.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// ASanの確保のフック(ヘッダが入っていない環境もあるので自分で宣言する。ライブラリ側には必ずある)
#if defined(__SANITIZE_ADDRESS__)
#define HAVE_MALLOC_HOOKS 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define HAVE_MALLOC_HOOKS 1
#endif
#endif
#if defined(HAVE_MALLOC_HOOKS)
extern "C" int __sanitizer_install_malloc_and_free_hooks(
    void (*malloc_hook)(const volatile void*, size_t), void (*free_hook)(const volatile void*));
#endif

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

#if defined(HAVE_MALLOC_HOOKS)
static volatile bool watching = false;
static volatile size_t largest_alloc = 0;
extern "C" void MallocHook(const volatile void*, size_t size){
    if(watching && size > largest_alloc) largest_alloc = size;
}
extern "C" void FreeHook(const volatile void*){}
#endif

static std::vector<uint8_t> Bytes(const char* s){ return std::vector<uint8_t>(s, s + strlen(s)); }

// 同じ入力で Monocypher と Argon2Split を比べる
static bool Same(uint32_t algorithm, uint32_t blocks, uint32_t passes, uint32_t lanes,
                 uint32_t hash_size, bool with_extras){
    const std::vector<uint8_t> pass = Bytes("correct horse battery staple");
    const std::vector<uint8_t> salt = Bytes("0123456789abcdef");
    const std::vector<uint8_t> key = Bytes("secret-key");
    const std::vector<uint8_t> ad = Bytes("associated");
    crypto_argon2_config cfg;
    cfg.algorithm = algorithm;
    cfg.nb_blocks = blocks;
    cfg.nb_passes = passes;
    cfg.nb_lanes = lanes;
    crypto_argon2_inputs in;
    in.pass = pass.data();
    in.pass_size = (uint32_t)pass.size();
    in.salt = salt.data();
    in.salt_size = (uint32_t)salt.size();
    crypto_argon2_extras ex = crypto_argon2_no_extras;
    if(with_extras){
        ex.key = key.data();
        ex.key_size = (uint32_t)key.size();
        ex.ad = ad.data();
        ex.ad_size = (uint32_t)ad.size();
    }
    std::vector<uint8_t> expected(hash_size), actual(hash_size, 0xAA);
    std::vector<uint64_t> work((size_t)blocks * 128);
    crypto_argon2(expected.data(), hash_size, work.data(), cfg, in, ex);
    if(!Argon2Split::Hash(actual.data(), hash_size, cfg, in, ex)) return false;
    return expected == actual;
}

int main(){
    // ---- Monocypherと同じ結果 ----
    check(Same(CRYPTO_ARGON2_ID, 64, 3, 1, 32, false), "Argon2id 64ブロック・3パス(pico.encryptの既定)");
    check(Same(CRYPTO_ARGON2_ID, 8, 1, 1, 32, false), "Argon2id 8ブロック・1パス(復号で受け付ける下限)");
    check(Same(CRYPTO_ARGON2_ID, 256, 2, 1, 32, false), "Argon2id 256ブロック(復号で受け付ける上限。index_blockの更新を跨ぐ)");
    check(Same(CRYPTO_ARGON2_ID, 66, 3, 1, 32, false), "Argon2id 4の倍数でないブロック数は切り捨て");
    check(Same(CRYPTO_ARGON2_I, 32, 3, 1, 32, false), "Argon2i");
    check(Same(CRYPTO_ARGON2_D, 32, 3, 1, 32, false), "Argon2d");
    check(Same(CRYPTO_ARGON2_ID, 32, 2, 2, 32, false), "2レーン");
    check(Same(CRYPTO_ARGON2_ID, 16, 1, 1, 100, true), "鍵とADあり・64バイトを超える出力");
    check(Same(CRYPTO_ARGON2_ID, 16, 1, 1, 16, false), "短い出力");

    // ---- 大きな連続した塊を要求しない ----
#if defined(HAVE_MALLOC_HOOKS)
    if(__sanitizer_install_malloc_and_free_hooks(MallocHook, FreeHook)){
        const char* plain = "secret";
        std::string enc, dec;
        enc.reserve(256);
        dec.reserve(256);
        largest_alloc = 0;
        watching = true;
        const bool ok =
            SecretAead::Encrypt((const uint8_t*)plain, strlen(plain), "app", "pw", enc) == SecretAead::Result::Ok &&
            SecretAead::Decrypt(enc.data(), enc.size(), "app", "pw", dec) == SecretAead::Result::Ok;
        watching = false;
        printf("       1回の確保の最大: %zuB\n", (size_t)largest_alloc);
        // 4KiBはPCの乱数(/dev/urandomをfopenしたときのstdioのバッファ)。実機には無い
        check(ok && largest_alloc <= 4096, "パスワード付きの暗号化/復号でも1回の確保は4KiB以下(Argon2は64KiB)");
    } else {
        printf("[SKIP] 確保のフックを入れられない(サニタイザ無し)\n");
    }
#else
    printf("[SKIP] 確保のフックが無い(サニタイザ無し)\n");
#endif

    // ---- SecretAead のパスワード付き ----
    {
        const char* plain = "パスワードで守る秘密";
        std::string enc, dec;
        check(SecretAead::Encrypt((const uint8_t*)plain, strlen(plain), "app", "demo-password", enc)
              == SecretAead::Result::Ok, "パスワード付きで暗号化できる");
        check(SecretAead::Decrypt(enc.data(), enc.size(), "app", "demo-password", dec)
              == SecretAead::Result::Ok && dec == plain, "同じパスワードで戻る");
        check(SecretAead::Decrypt(enc.data(), enc.size(), "app", "wrong", dec)
              == SecretAead::Result::AuthFailed, "違うパスワードは AuthFailed");
        // 繰り返しても失敗し始めない(以前は一度失敗すると再起動まで失敗し続けた)
        bool all = true;
        for(int i = 0; i < 20; i++){
            std::string e, d;
            all = all && SecretAead::Encrypt((const uint8_t*)plain, strlen(plain), "app", "pw", e) == SecretAead::Result::Ok;
            all = all && SecretAead::Decrypt(e.data(), e.size(), "app", "pw", d) == SecretAead::Result::Ok && d == plain;
        }
        check(all, "20回続けて暗号化と復号ができる");
    }

    printf("\n%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
