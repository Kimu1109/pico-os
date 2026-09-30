// SSHクライアントの小道具(src/ssh/Ssh_Sha256.hpp / Ssh_Util)のホストテスト。
// ソケットを使わない部分だけ(本物のsshd相手の往復は run_net.sh の ssh_net_test)。
//
//   - SHA-256 がFIPS 180-4の例と一致する(1バイトずつ食わせても同じ)
//   - Base64 の往復
//   - OpenSSH形式の秘密鍵を読める / パスフレーズ付き・RSA・壊れた鍵を断る
//   - ホスト鍵の指紋が ssh-keygen -l と同じ文字列になる
//   - known_hosts の照合(22番は名前だけ・それ以外は[host]:port・カンマ区切り・ハッシュ化された行は無視)
#include "ssh/Ssh_Sha256.hpp"
#include "ssh/Ssh_Util.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstring>
#include <string>

static int failures = 0;
#define CHECK(cond) do { \
    if(!(cond)){ printf("  [NG] %s:%d %s\n", __FILE__, __LINE__, #cond); failures++; } \
    else { printf("  [OK] %s\n", #cond); } \
} while(0)

static std::string Hex(const uint8_t* p, size_t n){
    static const char* d = "0123456789abcdef";
    std::string s;
    for(size_t i = 0; i < n; i++){ s.push_back(d[p[i] >> 4]); s.push_back(d[p[i] & 15]); }
    return s;
}

static std::string Sha(const char* msg, bool bytewise = false){
    uint8_t out[32];
    if(bytewise){
        Sha256 s;
        for(size_t i = 0; msg[i]; i++) s.update(msg + i, 1);
        s.finish(out);
    }else{
        Sha256::Hash(msg, strlen(msg), out);
    }
    return Hex(out, 32);
}

// テスト専用に作った使い捨ての鍵(どこにも登録していない)。ssh-keygen -t ed25519 -C test
static const char kTestKey[] =
    "-----BEGIN OPENSSH PRIVATE KEY-----\n"
    "b3BlbnNzaC1rZXktdjEAAAAABG5vbmUAAAAEbm9uZQAAAAAAAAABAAAAMwAAAAtzc2gtZW\n"
    "QyNTUxOQAAACDrypCWfF4tSsuwdskXRrsmZd4dUwkGHLvINWPmatYn3gAAAIi8qeKovKni\n"
    "qAAAAAtzc2gtZWQyNTUxOQAAACDrypCWfF4tSsuwdskXRrsmZd4dUwkGHLvINWPmatYn3g\n"
    "AAAECsQcUWIJKzdUeBCpflrdkXmw9Tb1E/VpvJ+q9LMptmievKkJZ8Xi1Ky7B2yRdGuyZl\n"
    "3h1TCQYcu8g1Y+Zq1ifeAAAABHRlc3QB\n"
    "-----END OPENSSH PRIVATE KEY-----\n";
static const char kTestPub[] = "AAAAC3NzaC1lZDI1NTE5AAAAIOvKkJZ8Xi1Ky7B2yRdGuyZl3h1TCQYcu8g1Y+Zq1ife";
static const char kTestFingerprint[] = "SHA256:pP2xp5ZA02716aucErOB1bMtTzqJBMzAI440hJzYVHU";

// パスフレーズ付きの鍵の頭の部分(ciphername = aes256-ctr)
static const char kEncryptedKey[] =
    "-----BEGIN OPENSSH PRIVATE KEY-----\n"
    "b3BlbnNzaC1rZXktdjEAAAAACmFlczI1Ni1jdHIAAAAGYmNyeXB0AAAAGAAAABAgPDWH9S\n"
    "kX44UXvNLii98gAAAAGAAAAAEAAAAzAAAAC3NzaC1lZDI1NTE5AAAAIKuU2W2NHn9VUk80\n"
    "-----END OPENSSH PRIVATE KEY-----\n";

int main(){
    printf("===== SHA-256 =====\n");
    CHECK(Sha("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(Sha("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(Sha("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    CHECK(Sha("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", true) ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    {
        // 100万個の'a'
        Sha256 s;
        char block[1000];
        memset(block, 'a', sizeof(block));
        for(int i = 0; i < 1000; i++) s.update(block, sizeof(block));
        uint8_t out[32];
        s.finish(out);
        CHECK(Hex(out, 32) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    }

    printf("===== Base64 =====\n");
    {
        char out[16];
        CHECK(SshUtil::Base64Encode((const uint8_t*)"Man", 3, out, sizeof(out)) == 4 && strcmp(out, "TWFu") == 0);
        CHECK(SshUtil::Base64Encode((const uint8_t*)"Ma", 2, out, sizeof(out)) == 4 && strcmp(out, "TWE=") == 0);
        CHECK(SshUtil::Base64Encode((const uint8_t*)"M", 1, out, sizeof(out)) == 4 && strcmp(out, "TQ==") == 0);
        CHECK(SshUtil::Base64Encode((const uint8_t*)"Man", 3, out, 4) == 0); //終端の分が足りない
        uint8_t bin[16];
        CHECK(SshUtil::Base64Decode("TWE=", 4, bin, sizeof(bin)) == 2 && memcmp(bin, "Ma", 2) == 0);
        CHECK(SshUtil::Base64Decode("TW\nFu", 5, bin, sizeof(bin)) == 3 && memcmp(bin, "Man", 3) == 0);
        CHECK(SshUtil::Base64Decode("T*Fu", 4, bin, sizeof(bin)) == -1);
        CHECK(SshUtil::Base64Decode("TWFuTWFu", 8, bin, 3) == -1); //溢れる
    }

    printf("===== 秘密鍵 =====\n");
    uint8_t secret[64], pub[32];
    CHECK(SshUtil::ParsePrivateKey(kTestKey, strlen(kTestKey), secret, pub) == SshUtil::KeyResult::Ok);
    {
        uint8_t blob[64];
        const int n = SshUtil::Base64Decode(kTestPub, strlen(kTestPub), blob, sizeof(blob));
        uint8_t want[SshUtil::kEd25519BlobBytes];
        SshUtil::Ed25519Blob(pub, want);
        CHECK(n == (int)SshUtil::kEd25519BlobBytes && memcmp(blob, want, sizeof(want)) == 0);
        CHECK(memcmp(secret + 32, pub, 32) == 0);
        char fp[80];
        SshUtil::Fingerprint(pub, fp, sizeof(fp));
        printf("  %s\n", fp);
        CHECK(strcmp(fp, kTestFingerprint) == 0);
    }
    CHECK(SshUtil::ParsePrivateKey(kEncryptedKey, strlen(kEncryptedKey), secret, pub) == SshUtil::KeyResult::Encrypted);
    {
        const char* rsa = "-----BEGIN RSA PRIVATE KEY-----\nMIIE\n-----END RSA PRIVATE KEY-----\n";
        CHECK(SshUtil::ParsePrivateKey(rsa, strlen(rsa), secret, pub) == SshUtil::KeyResult::Unsupported);
        CHECK(SshUtil::ParsePrivateKey("hello", 5, secret, pub) == SshUtil::KeyResult::Broken);
        //終わりの行が無い
        std::string cut(kTestKey, strlen(kTestKey) - 40);
        CHECK(SshUtil::ParsePrivateKey(cut.c_str(), cut.size(), secret, pub) == SshUtil::KeyResult::Broken);
        //中身の途中を壊す(checkint の不一致)
        std::string bad(kTestKey);
        bad[strlen("-----BEGIN OPENSSH PRIVATE KEY-----\n") + 200] ^= 1;
        CHECK(SshUtil::ParsePrivateKey(bad.c_str(), bad.size(), secret, pub) != SshUtil::KeyResult::Ok);
    }

    printf("===== 鍵ファイル・known_hosts(SD) =====\n");
    OSData::SD_usable = true;
    HostSd::files["/sys/ssh/id_ed25519"] = kTestKey;
    CHECK(SshUtil::LoadPrivateKey("/sys/ssh/id_ed25519", secret, pub) == SshUtil::KeyResult::Ok);
    CHECK(SshUtil::LoadPrivateKey("/sys/ssh/missing", secret, pub) == SshUtil::KeyResult::NotFound);

    const char* kh = "/sys/ssh/known_hosts";
    HostSd::files[kh] =
        "# コメント\n"
        "|1|abc=|def= ssh-ed25519 AAAA\n"
        "alpha,beta ssh-ed25519 " + std::string(kTestPub) + "\n"
        "gamma ssh-rsa AAAAB3NzaC1yc2E\n"
        "[gamma]:2200 ssh-ed25519 " + std::string(kTestPub) + " comment\n";
    CHECK(SshUtil::CheckKnownHost(kh, "alpha", 22, pub) == SshUtil::HostStatus::Match);
    CHECK(SshUtil::CheckKnownHost(kh, "beta", 22, pub) == SshUtil::HostStatus::Match);
    CHECK(SshUtil::CheckKnownHost(kh, "alpha", 2200, pub) == SshUtil::HostStatus::Unknown);
    CHECK(SshUtil::CheckKnownHost(kh, "gamma", 22, pub) == SshUtil::HostStatus::Unknown); //RSAの行は比べない
    CHECK(SshUtil::CheckKnownHost(kh, "gamma", 2200, pub) == SshUtil::HostStatus::Match);
    CHECK(SshUtil::CheckKnownHost(kh, "alph", 22, pub) == SshUtil::HostStatus::Unknown);
    {
        uint8_t other[32];
        memcpy(other, pub, 32);
        other[31] ^= 0x80;
        CHECK(SshUtil::CheckKnownHost(kh, "beta", 22, other) == SshUtil::HostStatus::Mismatch);
        CHECK(SshUtil::AddKnownHost(kh, "delta", 22, other));
        CHECK(SshUtil::CheckKnownHost(kh, "delta", 22, other) == SshUtil::HostStatus::Match);
        CHECK(HostSd::files[kh].find("\ndelta ssh-ed25519 ") != std::string::npos);
    }
    CHECK(SshUtil::CheckKnownHost("/sys/ssh/none", "alpha", 22, pub) == SshUtil::HostStatus::Unknown);

    OSData::SD_usable = false;
    CHECK(SshUtil::LoadPrivateKey("/sys/ssh/id_ed25519", secret, pub) == SshUtil::KeyResult::NotFound);
    CHECK(!SshUtil::AddKnownHost(kh, "x", 22, pub));

    printf("\n%s (%d件の失敗)\n", failures == 0 ? "全て成功" : "失敗あり", failures);
    return failures == 0 ? 0 : 1;
}
