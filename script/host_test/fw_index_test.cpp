// Webのファームの版の一覧(apps/firmware/Fw_Index、docs/FIRMWARE_UPDATE.md「Webから取る」)の読み方を固定する。
//
//   ./fw_index_test                         … C++の中で組み立てた一覧で全部の経路
//   ./fw_index_test <index.tsv> <構成> [u]  … script/build_fw_index.py が作ったものを読み、残った行を1行ずつ出す
//                                             (fw_index_tool_test.py が呼ぶ。u を付けると署名なしも残す)
#include "apps/firmware/Fw_Index.hpp"

#include <cstdio>
#include <cstring>
#include <string>

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

static const char* kSha = "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff";

static std::string Row(const char* serial, const char* git, const char* channel, const char* env,
                       const char* sig, const char* bytes = "1400000", const char* sha = kSha,
                       const char* file = "pico-os-1-abc.pfw", const char* date = "2026-10-28 18:43"){
    return std::string(serial) + "\t" + git + "\t" + date + "\t" + channel + "\t" + env + "\t" + sig + "\t" +
           bytes + "\t" + sha + "\t" + file + "\n";
}

struct Parsed {
    FwIndex::Entry e[FwIndex::kMaxEntries];
    FwIndex::Result r;
    bool ok = false;
};

static void Run(Parsed& p, const std::string& text, const char* env = "rpipico2w", bool allow_unsigned = false,
                int cap = FwIndex::kMaxEntries){
    FwIndex::Filter f;
    f.env = env;
    f.allow_unsigned = allow_unsigned;
    p.ok = FwIndex::Parse(text.data(), text.size(), f, p.e, cap, p.r);
}

static int FromFile(const char* path, const char* env, bool allow_unsigned){
    FILE* f = fopen(path, "rb");
    if(!f){ printf("[FAIL] %s を開けない\n", path); return 1; }
    std::string text;
    int c;
    while((c = fgetc(f)) != EOF) text.push_back((char)c);
    fclose(f);
    Parsed p;
    Run(p, text, env, allow_unsigned);
    if(!p.ok){ printf("[FAIL] 一覧として読めない\n"); return 1; }
    for(int i = 0; i < p.r.count; i++){
        const FwIndex::Entry& e = p.e[i];
        char sha[65];
        for(int k = 0; k < 32; k++) snprintf(sha + k * 2, 3, "%02x", e.sha256[k]);
        printf("%lu\t%s\t%s\t%s\t%s\t%d\t%lu\t%s\t%s\n", (unsigned long)e.serial, e.git.c_str(), e.date.c_str(),
               e.channel == FwIndex::Channel::Stable ? "stable" : "nightly", e.env.c_str(), e.signed_flag ? 1 : 0,
               (unsigned long)e.bytes, sha, e.file.c_str());
    }
    printf("hidden_env=%d hidden_unsigned=%d bad=%d\n", p.r.hidden_env, p.r.hidden_unsigned, p.r.bad_lines);
    return 0;
}

int main(int argc, char** argv){
    if(argc >= 3) return FromFile(argv[1], argv[2], argc >= 4 && strcmp(argv[3], "u") == 0);

    const std::string head = std::string(FwIndex::kMagic) + "\n";

    // ---- 基本 ----
    {
        Parsed p;
        Run(p, head + Row("412", "abcdef1", "stable", "rpipico2w", "1") +
                      Row("410", "1234567", "nightly", "rpipico2w", "1", "1300000", kSha, "pico-os-410-1234567.pfw"));
        check(p.ok && p.r.count == 2 && p.r.bad_lines == 0, "2行を読める");
        check(p.e[0].serial == 412 && strcmp(p.e[0].git.c_str(), "abcdef1") == 0 &&
              p.e[0].channel == FwIndex::Channel::Stable && p.e[0].signed_flag && p.e[0].bytes == 1400000,
              "1行目の値");
        check(strcmp(p.e[0].date.c_str(), "2026-10-28 18:43") == 0 && strcmp(p.e[0].env.c_str(), "rpipico2w") == 0,
              "日時と構成");
        check(p.e[0].sha256[0] == 0x00 && p.e[0].sha256[1] == 0x11 && p.e[0].sha256[31] == 0xff, "SHA-256を16進から");
        check(p.e[1].channel == FwIndex::Channel::Nightly && strcmp(p.e[1].file.c_str(), "pico-os-410-1234567.pfw") == 0,
              "2行目は並び順のまま");
        check(strcmp(FwIndex::ChannelText(FwIndex::Channel::Stable), "安定") == 0, "種類の表示");
    }
    // ---- 1行目 ----
    {
        Parsed p;
        Run(p, "# pico-os-fw-index 2\n" + Row("1", "a", "stable", "rpipico2w", "1"));
        check(!p.ok, "形式の版が違えば読まない");
        Run(p, "<html>404</html>\n");
        check(!p.ok, "一覧でないものは読まない");
        Run(p, "");
        check(!p.ok, "空は読まない");
        Run(p, head);
        check(p.ok && p.r.count == 0, "見出しだけなら0件");
        Run(p, std::string(FwIndex::kMagic) + "\r\n" + "1\ta\t\tstable\trpipico2w\t1\t5\t" + kSha + "\tpico-os-1-a.pfw\r\n");
        check(p.ok && p.r.count == 1 && p.e[0].date.empty() && strcmp(p.e[0].file.c_str(), "pico-os-1-a.pfw") == 0,
              "CRLFでも読める・日時は空でもよい");
    }
    // ---- 残す行を選ぶ ----
    {
        const std::string text = head + Row("5", "a", "stable", "rpipico2w", "1") +
                                 Row("4", "b", "nightly", "rpipico2w_lite", "1") +
                                 Row("3", "c", "nightly", "rpipico2w", "0");
        Parsed p;
        Run(p, text);
        check(p.ok && p.r.count == 1 && p.e[0].serial == 5 && p.r.hidden_env == 1 && p.r.hidden_unsigned == 1,
              "構成違いと署名なしを外す");
        Run(p, text, "rpipico2w", true);
        check(p.r.count == 2 && p.e[1].serial == 3 && !p.e[1].signed_flag, "fw-allow-unsigned なら署名なしも残す");
        Run(p, text, "rpipico2w_lite");
        check(p.r.count == 1 && p.e[0].serial == 4 && p.r.hidden_env == 2, "別の構成の本体にはその構成のものだけ");
        Run(p, text, "rpipico2w", true, 1);
        check(p.r.count == 1 && p.r.overflow == 1, "入りきらない分は捨てて数える");
    }
    // ---- 読めない行は飛ばす ----
    {
        const std::string good = Row("9", "g", "stable", "rpipico2w", "1");
        std::string text = head + "# 注釈の行\n\n" +
            "9\tg\t2026\tstable\trpipico2w\t1\t100\n" +                                  // 列が足りない
            Row("x9", "g", "stable", "rpipico2w", "1") +                                // 通し番号が数でない
            Row("09", "g", "stable", "rpipico2w", "1") +                                // 先頭の0
            Row("4294967296", "g", "stable", "rpipico2w", "1") +                        // 32ビットを超える
            Row("9", "", "stable", "rpipico2w", "1") +                                  // gitが空
            Row("9", "0123456789abcdef0", "stable", "rpipico2w", "1") +                 // gitが17文字
            Row("9", "g", "beta", "rpipico2w", "1") +                                   // 知らない種類
            Row("9", "g", "stable", "", "1") +                                          // 構成が空
            Row("9", "g", "stable", "rpipico2w", "yes") +                               // 署名の印
            Row("9", "g", "stable", "rpipico2w", "1", "0") +                            // 大きさ0
            Row("9", "g", "stable", "rpipico2w", "1", "1000", "00ff") +                 // SHA-256が短い
            Row("9", "g", "stable", "rpipico2w", "1", "1000",
                "zz112233445566778899aabbccddeeff00112233445566778899aabbccddeeff") +   // 16進でない
            Row("9", "g", "stable", "rpipico2w", "1", "1000", kSha, "../sys/x.pfw") +   // パスを含む
            Row("9", "g", "stable", "rpipico2w", "1", "1000", kSha, "x.bin") +          // .pfw でない
            Row("9", "g", "stable", "rpipico2w", "1", "1000", kSha, ".hidden.pfw") +    // . で始まる
            Row("9", "g\x01", "stable", "rpipico2w", "1") +                             // 制御文字
            std::string("9\tg\t\tstable\trpipico2w\t1\t1000\t") + kSha + "\tpico-os-9-g.pfw" + std::string(400, 'a') + "\n" +
            good;
        Parsed p;
        Run(p, text);
        check(p.ok && p.r.count == 1 && p.e[0].serial == 9 && p.r.bad_lines == 17, "読めない行は飛ばし、読める行は残す");
        // 将来の列(10列目より後ろ)は読み飛ばす
        std::string extra = good;
        extra.pop_back();
        extra += "\tnew-column\tmore\n";
        Run(p, head + extra);
        check(p.ok && p.r.count == 1 && strcmp(p.e[0].file.c_str(), "pico-os-1-abc.pfw") == 0, "増えた列は読み飛ばす");
        // 最後の行に改行が無くても読める
        std::string last = good;
        last.pop_back();
        Run(p, head + last);
        check(p.ok && p.r.count == 1, "最後の改行が無くても読める");
    }
    // ---- ファイル名 ----
    {
        check(FwIndex::SafeFileName("pico-os-412-abcdef1.pfw"), "普通の名前");
        check(FwIndex::SafeFileName("pico-os-412-abcdef1-rpipico2w_lite.pfw"), "構成つきの名前");
        check(!FwIndex::SafeFileName("a/b.pfw") && !FwIndex::SafeFileName("a\\b.pfw"), "区切りを含む名前");
        check(!FwIndex::SafeFileName(".pfw") && !FwIndex::SafeFileName("..pfw"), "短い・.で始まる名前");
        check(!FwIndex::SafeFileName("a.pfw ") && !FwIndex::SafeFileName("a b.pfw"), "空白を含む名前");
        check(!FwIndex::SafeFileName(std::string(PICO_STR_L, 'a').append(".pfw").c_str()), "長すぎる名前");
        check(!FwIndex::SafeFileName(nullptr), "nullptr");
    }

    printf(failures ? "\n%d 件失敗\n" : "\n全部通りました\n", failures);
    return failures ? 1 : 0;
}
