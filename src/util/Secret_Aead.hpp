// src/util/Secret_Aead.hpp
//
// Luaアプリ向けの認証付き暗号(AEAD)。Secret_Cipher.hpp(XTEA。Wi-Fiのパスワード等を
// 「ちょっと隠す」ための弱い難読化)とは別物で、こちらは改ざんも検出する本物の暗号を使う。
//   暗号: XChaCha20-Poly1305(Monocypher crypto_aead_lock)
//   パスワードからの鍵: Argon2id(メモリ64KiB・3パス。組み込み向けの控えめな設定)
//   乱数: 実機はRP2350のハードウェア乱数、PCは/dev/urandom
//
// ============================================================================
// 鍵の決め方と限界(正直に)
// ============================================================================
// 1. パスワードを渡したとき(version 2): 鍵はパスワードとランダムなsaltだけから作る。
//    本体のファームを吸い出されても、パスワードが強ければ読まれない。SDだけを盗まれた場合も同じ。
//    弱い点: Argon2のメモリが64KiBしかない(RAM 520KBの機械なので)ため、GPUで総当たりする相手には
//    デスクトップ向けの設定(数百MiB)ほど強くない。**長いパスフレーズを使うこと**。
// 2. パスワード無し(version 1): 鍵は「ファームに焼かれた固定鍵(Secret_Cipher.hppのkKey)」と
//    アプリのディレクトリ名から作る。守れるのは「SDだけが盗まれた/見られた」場合と、
//    「別のアプリが同じデータを復号する」ことだけ(アプリごとに鍵が違う)。ファームを吸い出せる相手、
//    またはkKeyを知っている相手には無力。forkして使うならkKeyを書き換えること。
// 3. どちらも、実行中のアプリのメモリを覗ける相手(デバッガ・同じ機械の上のクラッシュダンプ)には無力。
//
// 形式: "enc2:" + base64url( header | nonce(24) | 暗号文 | MAC(16) )
//   header = version(1) [+ version 2 のとき: Argon2のパス数(1) + ブロック数(2, LE) + salt(16)]
//   headerは認証される(AADに含まれるので、パラメータを書き換えても検出できる)
// ============================================================================

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace SecretAead {

    constexpr const char* kPrefix = "enc2:";
    constexpr size_t kMaxPlainBytes = 12 * 1024;
    // 復号を受け付ける暗号文字列の長さの上限(kMaxPlainBytesを暗号化したものより少し余裕を持たせる)
    constexpr size_t kMaxEncodedBytes = 17 * 1024;

    constexpr uint8_t kVersionDevice = 1;    // パスワード無し(固定鍵+アプリ名)
    constexpr uint8_t kVersionPassword = 2;  // パスワードからArgon2idで鍵を作る
    constexpr uint32_t kArgonBlocks = 64;    // 1ブロック1KiB = 64KiB
    constexpr uint32_t kArgonPasses = 3;
    // 復号するときの上限(壊れた/悪意のあるデータで大量のメモリや時間を使わせないため)
    constexpr uint32_t kMaxArgonBlocks = 256;
    constexpr uint32_t kMaxArgonPasses = 10;

    enum class Result : uint8_t {
        Ok,
        BadFormat,     // 接頭辞が違う・壊れている・長さが足りない
        AuthFailed,    // 鍵が違う(パスワード違い・別のアプリ)か、改ざんされている
        NeedPassword,  // パスワード付きの暗号文なのにパスワードが無い
        TooBig,
        NoMemory,
    };
    const char* ResultToStr(Result r);

    // 先頭が"enc2:"か
    bool IsEncrypted(const char* text, size_t len);

    // plainを暗号化して"enc2:..."を out へ入れる。
    //   context: パスワード無しのとき鍵に混ぜる名前(アプリのディレクトリ)。パスワード有りのときは使わない
    //   password: nullptrか空ならパスワード無し(version 1)
    Result Encrypt(const uint8_t* plain, size_t n, const char* context, const char* password, std::string& out);

    // "enc2:..."を復号して out へ入れる。contextはEncrypt()と同じものを渡す
    Result Decrypt(const char* text, size_t len, const char* context, const char* password, std::string& out);

    // ハードウェア/OSの乱数
    void Random(uint8_t* out, size_t n);

    // BLAKE2b。hash_sizeは1〜64。keyは任意(あれば鍵付きのMAC)
    void Blake2b(uint8_t* hash, size_t hash_size, const uint8_t* data, size_t n,
                 const uint8_t* key = nullptr, size_t key_size = 0);
}
