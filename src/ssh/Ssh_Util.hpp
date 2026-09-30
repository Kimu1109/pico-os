#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// SSHクライアントの小道具(乱数・Base64・鍵ファイル・known_hosts)。
namespace SshUtil {
    // 暗号用の乱数。実機はRP2350のハードウェア乱数(pico_rand)、PC/Webは /dev/urandom
    void Random(uint8_t* out, size_t n);

    // Base64(改行なし)。outCapが足りなければ0を返す。終端の'\0'を付ける
    size_t Base64Encode(const uint8_t* in, size_t n, char* out, size_t outCap);
    // 空白・改行は読み飛ばす。不正な文字があれば-1
    int Base64Decode(const char* in, size_t n, uint8_t* out, size_t outCap);

    // ssh-ed25519 の公開鍵の塊(string "ssh-ed25519" + string pub)。51バイト
    constexpr size_t kEd25519BlobBytes = 4 + 11 + 4 + 32;
    void Ed25519Blob(const uint8_t pub[32], uint8_t out[kEd25519BlobBytes]);

    // "SHA256:xxxx"(OpenSSHの ssh-keygen -l と同じ書き方)。outは64バイト以上
    void Fingerprint(const uint8_t pub[32], char* out, size_t outCap);

    // ---- 秘密鍵(OpenSSHの形式 "-----BEGIN OPENSSH PRIVATE KEY-----") ----
    enum class KeyResult { Ok, NotFound, Broken, Encrypted, Unsupported };
    // secret は Monocypher の crypto_ed25519_sign が取る64バイト(種32 + 公開鍵32)
    KeyResult ParsePrivateKey(const char* text, size_t len, uint8_t secret[64], uint8_t pub[32]);
    // 鍵ファイルを読む。**平文のOpenSSH形式なら、読めた時点で暗号化して同じ場所へ書き直す**
    // (PCで作った鍵をSDへそのまま置けばよく、次からは暗号化された形で残る)。
    // 書き直した結果は *rewrite へ(書き直しに失敗しても鍵は使える。平文のまま残る)
    enum class KeyRewrite { None, Encrypted, Failed };
    KeyResult LoadPrivateKey(const char* path, uint8_t secret[64], uint8_t pub[32], KeyRewrite* rewrite = nullptr);

    // ---- 鍵ファイルの暗号化(Wi-Fiのパスワードと同じ PICO_Secret。util/Secret_Cipher.hpp) ----
    // 中身は "enc1:" + 16進の1行。暗号化するのは "pico-ssh-ed25519:" + 秘密鍵64バイトの16進
    // (公開鍵は秘密鍵の後半32バイトなので別に持たない)。
    // **守れるのは「SDだけを落とした/見られた」場合だけ**(鍵はファームウェアに焼かれた固定値。
    // Secret_Cipher.hpp の冒頭の注意を参照)。
    constexpr const char* kKeyPurpose = "ssh-id-ed25519";
    // 暗号化した1行(終端の'\0'込み。outは kEncryptedKeyCap 以上)
    constexpr size_t kEncryptedKeyCap = 5 + (17 + 128) * 2 + 1;
    bool EncryptKey(const uint8_t secret[64], char* out, size_t outCap);
    // text が暗号化された鍵ファイルなら読んで Ok、"enc1:" で始まらなければ NotFound(=暗号化されていない)
    KeyResult DecryptKey(const char* text, size_t len, uint8_t secret[64], uint8_t pub[32]);
    // 鍵ファイルの中身が暗号化されたものか(先頭の空白を飛ばして "enc1:" で始まるか)
    bool IsEncryptedKeyText(const char* text, size_t len);
    // ParsePrivateKey の下請け(Base64を解いた後の "openssh-key-v1" の中身を読む)
    KeyResult ParseDecoded(const uint8_t* raw, int n, uint8_t secret[64], uint8_t pub[32]);

    // ---- known_hosts(OpenSSHと同じ書き方。22番以外は "[host]:port") ----
    enum class HostStatus { Unknown, Match, Mismatch };
    HostStatus CheckKnownHost(const char* path, const char* host, uint16_t port, const uint8_t pub[32]);
    bool AddKnownHost(const char* path, const char* host, uint16_t port, const uint8_t pub[32]);
}
