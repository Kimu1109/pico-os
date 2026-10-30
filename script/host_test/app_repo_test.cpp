// アプリストアの配布物の読み取り(apps/store/App_Repo)。docs/APP_REPO.md の取り決めを固定する。
// 通信とインストールは run_net.sh の app_store_net_test。
#include "apps/store/App_Repo.hpp"
#include "apps/ssh/Ssh_Sha256.hpp"
#include "monocypher-ed25519.h"

#include <cstdio>
#include <cstring>
#include <string>

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

// 書き換えてよい写しを作って読む(本物は1行ずつのバッファをその場で書き換える)
static bool CatLine(const char* line, AppRepo::Entry& e, bool& skip){
    std::string s(line);
    return AppRepo::ParseCatalogLine(&s[0], e, skip);
}
static bool FilesLine(const char* line, AppRepo::FileEntry& f, bool& skip){
    std::string s(line);
    return AppRepo::ParseFilesLine(&s[0], f, skip);
}

static const char* kHash = "6600098b337a694c04a9507cb47efade200ea048993d8fd188b417736d6c9039";
// 作者の公開鍵の例(RFC 8032 のテストの鍵)
static const char* kAuthor = "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a";

int main(){
    // ---- 名前 ----
    check(AppRepo::ValidId("tetris") && AppRepo::ValidId("zombie-td") && AppRepo::ValidId("a_1"), "id: 使える");
    check(!AppRepo::ValidId("") && !AppRepo::ValidId("Tetris") && !AppRepo::ValidId("a/b")
          && !AppRepo::ValidId("../x") && !AppRepo::ValidId("aaaaaaaaaaaaaaaaaaaaaaaa"), "id: 使えない");
    check(AppRepo::ValidVersion("1") && AppRepo::ValidVersion("2.0.10"), "version: 使える");
    check(!AppRepo::ValidVersion("") && !AppRepo::ValidVersion("1.") && !AppRepo::ValidVersion(".1")
          && !AppRepo::ValidVersion("1..2") && !AppRepo::ValidVersion("1.0-beta") && !AppRepo::ValidVersion("../1"), "version: 使えない");
    check(AppRepo::ValidDirName("テトリス") && AppRepo::ValidDirName("ゾンビTD"), "dir: 日本語は使える");
    check(!AppRepo::ValidDirName("..") && !AppRepo::ValidDirName(".x") && !AppRepo::ValidDirName("a/b")
          && !AppRepo::ValidDirName("a\\b") && !AppRepo::ValidDirName("a:b") && !AppRepo::ValidDirName("a.")
          && !AppRepo::ValidDirName("a ") && !AppRepo::ValidDirName("a\nb") && !AppRepo::ValidDirName(""), "dir: 使えない");
    check(AppRepo::ValidRelPath("main.lua") && AppRepo::ValidRelPath("img/a-b_c.pimg") && AppRepo::ValidRelPath("a/b/c/d.lua"), "path: 使える");
    check(!AppRepo::ValidRelPath("/main.lua") && !AppRepo::ValidRelPath("../x") && !AppRepo::ValidRelPath("a/../x")
          && !AppRepo::ValidRelPath("a//b") && !AppRepo::ValidRelPath("a/") && !AppRepo::ValidRelPath(".hidden")
          && !AppRepo::ValidRelPath("a/./b") && !AppRepo::ValidRelPath("a/b/c/d/e") && !AppRepo::ValidRelPath("日本.lua")
          && !AppRepo::ValidRelPath("a b") && !AppRepo::ValidRelPath("a\\b") && !AppRepo::ValidRelPath(""), "path: 使えない");
    {
        std::string longp(96, 'a');
        check(!AppRepo::ValidRelPath(longp.c_str()) && AppRepo::ValidRelPath(longp.substr(0, 95).c_str()), "path: 95文字まで");
    }

    // ---- 版 ----
    check(AppRepo::CompareVersion("1.0.0", "1.0.0") == 0 && AppRepo::CompareVersion("1", "1.0") == 0, "version: 同じ");
    check(AppRepo::CompareVersion("1.9", "1.10") < 0 && AppRepo::CompareVersion("2.0", "1.99.9") > 0
          && AppRepo::CompareVersion("1.0", "1.0.1") < 0, "version: 数として比べる");

    // ---- 権限 ----
    uint8_t perms = 0xFF;
    check(AppRepo::ParsePerms("-", perms) && perms == 0, "perms: -");
    check(AppRepo::ParsePerms("network,notify,sd", perms) && perms == (AppRepo::PermNetwork | AppRepo::PermNotify | AppRepo::PermSd), "perms: 全部");
    check(!AppRepo::ParsePerms("network,camera", perms), "perms: 知らない権限は断る");
    check(AppRepo::ParsePerms("background,network", perms) && perms == (AppRepo::PermBackground | AppRepo::PermNetwork), "perms: background");
    check(AppRepo::ParsePerms("microphone", perms) && perms == AppRepo::PermMicrophone, "perms: microphone");
    {
        FixedString<PICO_STR_L> t;
        AppRepo::PermsText(AppRepo::PermMicrophone | AppRepo::PermNetwork, t);
        check(strcmp(t.c_str(), "ネットワーク・マイク") == 0, "perms: マイクの表示");
    }
    FixedString<PICO_STR_L> pt;
    AppRepo::PermsText(AppRepo::PermNetwork | AppRepo::PermNotify, pt);
    check(pt == "ネットワーク・通知", "perms: 表示");
    AppRepo::PermsText(AppRepo::PermNetwork | AppRepo::PermNotify | AppRepo::PermSd | AppRepo::PermBackground, pt);
    check(pt == "ネットワーク・通知・SD全体・バックグラウンド", "perms: 4つとも切れずに表示");
    AppRepo::PermsText(0, pt);
    check(pt == "なし", "perms: なしの表示");

    // ---- catalog.tsv ----
    {
        uint32_t serial = 0;
        std::string h = "pico-os-apps\t2\t1791704372\r\n";
        check(AppRepo::ParseCatalogHeader(&h[0], serial) && serial == 1791704372u, "catalog: 1行目");
        h = "pico-os-apps\t2\t5\tfuture\n";
        check(AppRepo::ParseCatalogHeader(&h[0], serial) && serial == 5, "catalog: 1行目の後ろの列は無視");
        h = "pico-os-apps\t1\t5\n";
        check(!AppRepo::ParseCatalogHeader(&h[0], serial), "catalog: 形式1(作者の署名が無い)は読まない");
        h = "pico-os-apps\t3\t5\n";
        check(!AppRepo::ParseCatalogHeader(&h[0], serial), "catalog: 知らない形式の番号は断る");
        h = "something\t1\t5\n";
        check(!AppRepo::ParseCatalogHeader(&h[0], serial), "catalog: 見出しが違う");
        h = "pico-os-apps\t2\t99999999999\n";
        check(!AppRepo::ParseCatalogHeader(&h[0], serial), "catalog: serialが32bitを超える");

        AppRepo::Entry e;
        bool skip = false;
        const std::string tail = std::string("\t") + kAuthor + "\tkimu1109\t-";
        std::string line = std::string("tetris\t2.0.0\tテトリス\tテトリス\t21143\t") + kHash + "\tnetwork\t説明です" + tail + "\t-\textra\n";
        check(CatLine(line.c_str(), e, skip), "catalog: 1件読める(13列目以降は無視)");
        check(e.id == "tetris" && e.version == "2.0.0" && e.dir == "テトリス" && e.name == "テトリス"
              && e.bytes == 21143 && e.perms == AppRepo::PermNetwork && e.description == "説明です", "catalog: 各列");
        check(e.files_sha256[0] == 0x66 && e.files_sha256[31] == 0x39, "catalog: SHA-256");
        check(e.author_key[0] == 0xd7 && e.author_key[31] == 0x1a && e.author == "kimu1109"
              && e.status == AppRepo::Status::Normal, "catalog: 作者の鍵・作者名・status");
        line = std::string("tetris\t2.0.0\tテトリス\tテトリス\t21143\t") + kHash + "\t-\t説明" + tail + "\n";
        check(CatLine(line.c_str(), e, skip) && e.status == AppRepo::Status::Normal && e.min_os == 0,
              "catalog: 後ろの列が無くても読める(min_os の列が無ければ条件なし)");
        line = std::string("tetris\t2.0.0\tテトリス\tテトリス\t21143\t") + kHash + "\t-\t説明" + tail + "\t1234\n";
        check(CatLine(line.c_str(), e, skip) && e.min_os == 1234, "catalog: min_os(12列目)");
        check(!AppRepo::OsTooOld(e, 1234) && !AppRepo::OsTooOld(e, 2000) && AppRepo::OsTooOld(e, 1233),
              "min_os: 本体の通し番号が足りなければ動かせない");
        check(!AppRepo::OsTooOld(e, 0), "min_os: 通し番号0(PC・手元のビルド)は条件を見ない");
        line = std::string("tetris\t2.0.0\tテトリス\tテトリス\t21143\t") + kHash + "\t-\t説明" + tail + "\t-\n";
        check(CatLine(line.c_str(), e, skip) && e.min_os == 0, "catalog: min_os が - なら条件なし");
        line = std::string("tetris\t2.0.0\tテトリス\tテトリス\t21143\t") + kHash + "\t-\t説明" + tail + "\t1.2\n";
        check(!CatLine(line.c_str(), e, skip) && !skip, "catalog: 読めない min_os の行は読まない");
        line = std::string("tetris\t2.0.0\tテトリス\tテトリス\t21143\t") + kHash + "\tnetwork\t説明です\n";
        check(!CatLine(line.c_str(), e, skip) && !skip, "catalog: 形式1の8列の行は断る");
        line = std::string("tetris\t2\tテトリス\tx\t1\t") + kHash + "\t-\t\t" + std::string(64, '0') + "\ta\t-\n";
        check(!CatLine(line.c_str(), e, skip), "catalog: 全部0の作者の鍵は断る");
        line = std::string("tetris\t2\tテトリス\tx\t1\t") + kHash + "\t-\t\tabcd\ta\t-\n";
        check(!CatLine(line.c_str(), e, skip), "catalog: 作者の鍵が64桁でない");
        check(AppRepo::ParseStatus("-") == AppRepo::Status::Normal && AppRepo::ParseStatus("yanked") == AppRepo::Status::Yanked
              && AppRepo::ParseStatus("blocked") == AppRepo::Status::Blocked, "status: 3つ");
        check(AppRepo::ParseStatus("future") == AppRepo::Status::Yanked && AppRepo::ParseStatus("") == AppRepo::Status::Yanked,
              "status: 知らない値は取り下げと同じ");
        check(!CatLine("", e, skip) && skip, "catalog: 空行は飛ばす");
        check(!CatLine("# comment\n", e, skip) && skip, "catalog: #は飛ばす");
        const char* bad[] = {
            "tetris\t2.0.0\tテトリス\tテトリス\t21143\n",                                   // 列が足りない(形式1の行も)
            "Tetris\t2.0.0\tテトリス\tx\t1\t%s\t-\t\n",                                      // id
            "tetris\tv2\tテトリス\tx\t1\t%s\t-\t\n",                                         // version
            "tetris\t2\t../x\tx\t1\t%s\t-\t\n",                                              // dir
            "tetris\t2\tテトリス\t\t1\t%s\t-\t\n",                                            // name が空
            "tetris\t2\tテトリス\tx\t-1\t%s\t-\t\n",                                          // bytes
            "tetris\t2\tテトリス\tx\t9999999\t%s\t-\t\n",                                     // 4MiB超え
            "tetris\t2\tテトリス\tx\t1\tabcd\t-\t\n",                                         // hash
            "tetris\t2\tテトリス\tx\t1\t%s\troot\t\n",                                        // 知らない権限
        };
        bool all = true;
        for(const char* fmt : bad){
            char buf[640];
            snprintf(buf, sizeof(buf), fmt, kHash);
            //作者の鍵などの後ろの3列を足す(壊した列だけで断ることを見るため)
            std::string b(buf);
            if(!b.empty() && b.back() == '\n') b.pop_back();
            b += tail + "\n";
            snprintf(buf, sizeof(buf), "%s", b.c_str());
            if(CatLine(buf, e, skip) || skip) all = false;
        }
        check(all, "catalog: 壊れた行は断る");
    }

    // ---- files.tsv ----
    {
        AppRepo::FileEntry f;
        bool skip = false;
        std::string line = std::string("img/a.pimg\t659\t") + kHash + "\n";
        check(FilesLine(line.c_str(), f, skip) && f.path == "img/a.pimg" && f.bytes == 659 && f.sha256[0] == 0x66, "files: 1行");
        line = std::string("../a\t1\t") + kHash;
        check(!FilesLine(line.c_str(), f, skip), "files: 外へ出るパスは断る");
        line = std::string("a.lua\t2000000\t") + kHash;
        check(!FilesLine(line.c_str(), f, skip), "files: 1MiB超えは断る");
        check(!FilesLine("a.lua\t1\n", f, skip) && !skip, "files: 列が足りない");
    }

    // ---- 署名 ----
    {
        uint8_t seed[32];
        for(int i = 0; i < 32; i++) seed[i] = (uint8_t)(i * 7 + 1);
        uint8_t sk[64], pk[32];
        uint8_t seed_copy[32];
        memcpy(seed_copy, seed, 32);
        crypto_ed25519_key_pair(sk, pk, seed_copy);

        const char* catalog = "pico-os-apps\t2\t1\n";
        uint8_t digest[32];
        Sha256::Hash(catalog, strlen(catalog), digest);
        uint8_t msg[AppRepo::kSignMessageBytes];
        memcpy(msg, AppRepo::kSignContext, 16);
        memcpy(msg + 16, digest, 32);
        uint8_t sig[64];
        crypto_ed25519_sign(sig, sk, msg, sizeof(msg));

        char pkhex[65], sighex[129];
        AppRepo::HexEncode(pk, 32, pkhex);
        AppRepo::HexEncode(sig, 64, sighex);
        std::string text = std::string("ed25519 ") + pkhex + "\t" + sighex + "\n";
        uint8_t pk2[32], sig2[64];
        check(AppRepo::ParseSignature(text.c_str(), pk2, sig2) && memcmp(pk2, pk, 32) == 0 && memcmp(sig2, sig, 64) == 0, "sig: 読める");
        check(AppRepo::VerifyCatalog(digest, pk2, sig2), "sig: 正しい署名");
        uint8_t other[32];
        memcpy(other, digest, 32);
        other[0] ^= 1;
        check(!AppRepo::VerifyCatalog(other, pk2, sig2), "sig: 中身が違えば合わない");
        sig2[10] ^= 0x40;
        check(!AppRepo::VerifyCatalog(digest, pk2, sig2), "sig: 署名が壊れていれば合わない");
        uint8_t zero[32] = {};
        check(!AppRepo::VerifyCatalog(digest, zero, sig), "sig: 全部0の鍵は使わない");
        check(!AppRepo::IsBuiltinKey(zero), "sig: 全部0の焼き込み鍵(空の目印)は信頼しない");
        check(!AppRepo::ParseSignature("rsa abc def", pk2, sig2), "sig: ed25519以外");
        check(!AppRepo::ParseSignature((std::string("ed25519 ") + pkhex).c_str(), pk2, sig2), "sig: 署名が無い");
        check(!AppRepo::ParseSignature((text + "x").c_str(), pk2, sig2), "sig: 後ろにゴミ");

        // script/build_app_repo.py(Pythonで書いたEd25519)が作った署名を確かめられること。
        // 下の値は RFC 8032 のテストの鍵(seed 9d61b1…)で、catalog "pico-os-apps\t1\t1\n" を
        // build_app_repo.sign_catalog() で署名したもの
        uint8_t py_pk[32], py_sig[64];
        const char* py = "ed25519 d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a "
            "6889229ae32050f908596d255a372ce4490074634bc31b28f09f1a77967d76fc7c1606e410d69ce9cd6876c5faf383edc1aa7bd4861e583e408a74292fdadb00\n";
        check(AppRepo::ParseSignature(py, py_pk, py_sig) && AppRepo::VerifyCatalog(digest, py_pk, py_sig),
              "sig: Pythonの署名をMonocypherで確かめられる");

        // files.sig: 同じ鍵で、files.tsv "main.lua\t3\t<sha256("abc")>\n" を tetris 2.0.0 として
        // build_app_repo.sign_files() で署名したもの
        uint8_t files_sha[32];
        AppRepo::HexDecode("06eb9bcf53217ab7548b37d1d9212d445e3688f13303e39345f1fa5feb485e4c", 64, files_sha, 32);
        const char* pyf = "ed25519 d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a "
            "8dbea9d904caaec99220c026d8949b17831bfbcfd65df8691d43c1529e0c92f3da315ea5cb72675baf94f80826ff71af0c7528e81581c7db30b0bb45d513f709\n";
        check(AppRepo::ParseSignature(pyf, py_pk, py_sig) && AppRepo::VerifyFiles("tetris", "2.0.0", files_sha, py_pk, py_sig),
              "files.sig: Pythonの署名を確かめられる");
        check(!AppRepo::VerifyFiles("reversi", "2.0.0", files_sha, py_pk, py_sig), "files.sig: 別のidへ付け替えられない");
        check(!AppRepo::VerifyFiles("tetris", "1.0.0", files_sha, py_pk, py_sig), "files.sig: 別の版へ付け替えられない");
        check(!AppRepo::VerifyCatalog(files_sha, py_pk, py_sig), "files.sig: カタログの署名としては通らない");
        uint8_t other_sha[32];
        memcpy(other_sha, files_sha, 32);
        other_sha[3] ^= 1;
        check(!AppRepo::VerifyFiles("tetris", "2.0.0", other_sha, py_pk, py_sig), "files.sig: files.tsv が違えば合わない");
    }

    printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
