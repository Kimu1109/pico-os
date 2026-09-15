// キャッシュ層(PICO_DocCache)のテスト。
//
// 一番確かめたいのは「通信が途中で切れた半端なファイルを、正常なキャッシュとして
// 残さない」こと。次点で、目録(index.tsv)の書き換えが他の行を壊さないこと。
//
// SDはstubs/SdFat.hのパス->内容のmapなので、HostSd::filesを直接覗いて
// 「何が残ったか」をそのまま検査できる。
#include "storage/Doc_Cache.hpp"
#include "net/Manifest.hpp"
#include "storage/SD_Path.hpp"
#include "functions/Log_Functions.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstring>
#include <string>

// ---- モック ----
void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}

static int failures = 0;

static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

static void eq_str(const char* actual, const char* expected, const char* label){
    const bool ok = (strcmp(actual, expected) == 0);
    printf("%s %-46s 実測=%-34s 期待=%s\n", ok ? "[ OK ]" : "[FAIL]", label, actual, expected);
    if(!ok) failures++;
}

static void eq_u32(uint32_t actual, uint32_t expected, const char* label){
    const bool ok = (actual == expected);
    printf("%s %-46s 実測=%lu 期待=%lu\n", ok ? "[ OK ]" : "[FAIL]", label,
           (unsigned long)actual, (unsigned long)expected);
    if(!ok) failures++;
}

// SD(スタブ)の中身を直接見るための補助
static bool sdHas(const char* path){ return HostSd::files.count(path) > 0; }
static std::string sdGet(const char* path){
    auto it = HostSd::files.find(path);
    return (it == HostSd::files.end()) ? std::string() : it->second;
}
static std::string indexText(){ return sdGet(PICO_Path::FILE::CACHE_INDEX_TSV); }
static int countLines(const std::string& s){
    int n = 0;
    for(char c : s) if(c == '\n') n++;
    return n;
}
// 残っている一時ファイル(.part / .tmp)の数
static int leftoverTempFiles(){
    int n = 0;
    for(const auto& kv : HostSd::files){
        if(kv.first.size() >= 5 && kv.first.compare(kv.first.size() - 5, 5, ".part") == 0) n++;
        if(kv.first.size() >= 4 && kv.first.compare(kv.first.size() - 4, 4, ".tmp") == 0) n++;
    }
    return n;
}

static void reset(){ HostSd::files.clear(); }

static const char* kHost = "192.168.1.10:8080";
static const char* kSafeHost = "192.168.1.10_8080";

int main(){
    // ---- ホスト名の正規化 ----
    {
        FixedString<PICO_STR_M> out;

        check(PICO_DocCache::SanitizeHost(out, kHost), "ホスト正規化: 成功する");
        eq_str(out.c_str(), kSafeHost, "ホスト正規化: ':' が '_' になる(FATで使えないため)");

        PICO_DocCache::SanitizeHost(out, "Docs.Example.COM");
        eq_str(out.c_str(), "docs.example.com", "ホスト正規化: 小文字へそろえる");

        PICO_DocCache::SanitizeHost(out, "a/b*c?d");
        eq_str(out.c_str(), "a_b_c_d", "ホスト正規化: FATで使えない文字を潰す");

        check(!PICO_DocCache::SanitizeHost(out, ""), "ホスト正規化: 空文字は失敗する");
        check(!PICO_DocCache::SanitizeHost(out, nullptr), "ホスト正規化: nullptrは失敗する");

        char longHost[PICO_STR_M * 2];
        memset(longHost, 'h', sizeof(longHost) - 1);
        longHost[sizeof(longHost) - 1] = '\0';
        check(!PICO_DocCache::SanitizeHost(out, longHost),
              "ホスト正規化: 長すぎる場合は切らずに失敗する");
    }

    // ---- キャッシュ上のパス ----
    {
        FixedString<PICO_PATH_LEN> out;

        check(PICO_DocCache::PathFor(out, kHost, "/docs/pico/intro.md"), "パス生成: 成功する");
        eq_str(out.c_str(), "/cache/192.168.1.10_8080/docs/pico/intro.md",
               "パス生成: サーバ上のパスをミラーする");

        PICO_DocCache::PathFor(out, kHost, "docs/./pico/../intro.md");
        eq_str(out.c_str(), "/cache/192.168.1.10_8080/docs/intro.md",
               "パス生成: \".\"と\"..\"を畳んでから使う");

        check(!PICO_DocCache::PathFor(out, kHost, "/"),
              "パス生成: ディレクトリそのものは拒否する");
    }

    // ---- 正常な書き込み ----
    {
        reset();
        PICO_DocCache::Writer w;
        check(w.begin(kHost, "/docs/intro.md"), "書き込み: begin()できる");
        //日本語のバイト数を手で数えると間違えるのでstrlenに任せる
        const char* part1 = "# はじめに\n";
        const char* part2 = "本文\n";
        check(w.write(part1, strlen(part1)), "書き込み: write()できる");
        check(w.write(part2, strlen(part2)), "書き込み: 続けて追記できる");

        //commit前は本体がまだ無く、一時ファイルだけがある
        check(!sdHas("/cache/192.168.1.10_8080/docs/intro.md"),
              "書き込み: commit前は本体が存在しない");
        check(sdHas("/cache/192.168.1.10_8080/docs/intro.md.part"),
              "書き込み: commit前は一時ファイルへ書かれている");

        check(w.commit("etag-abc", 1000), "書き込み: commit()できる");

        check(sdHas("/cache/192.168.1.10_8080/docs/intro.md"),
              "書き込み: commit後に本体ができる");
        eq_str(sdGet("/cache/192.168.1.10_8080/docs/intro.md").c_str(),
               "# はじめに\n本文\n", "書き込み: 内容が一致する");
        eq_u32((uint32_t)leftoverTempFiles(), 0, "書き込み: 一時ファイルが残らない");

        //目録
        PICO_DocCache::Entry e;
        check(PICO_DocCache::Lookup(kHost, "/docs/intro.md", e), "目録: 引ける");
        eq_str(e.host.c_str(), kSafeHost, "目録: hostは正規化済みで入る");
        eq_str(e.path.c_str(), "/docs/intro.md", "目録: pathは正規化済みで入る");
        eq_str(e.validator.c_str(), "etag-abc", "目録: validatorが入る");
        eq_u32(e.fetched_epoch, 1000, "目録: 取得時刻が入る");
        eq_u32(e.size, (uint32_t)(strlen(part1) + strlen(part2)), "目録: バイト数が入る");

        check(PICO_DocCache::Exists(kHost, "/docs/intro.md"), "Exists: 本体があればtrue");
        check(!PICO_DocCache::Exists(kHost, "/docs/none.md"), "Exists: 無ければfalse");
        check(!PICO_DocCache::Lookup(kHost, "/docs/none.md", e), "目録: 無い文書はfalse");
        check(!PICO_DocCache::Lookup("other.example", "/docs/intro.md", e),
              "目録: ホストが違えばfalse");
    }

    // ---- 途中で切れた場合(このモジュールの存在理由) ----
    {
        reset();
        //まず正常なキャッシュを1件作っておく
        {
            PICO_DocCache::Writer w;
            w.begin(kHost, "/docs/intro.md");
            const char* body = "古い内容\n";
            w.write(body, strlen(body));
            w.commit("etag-old", 1000);
        }
        const std::string before = sdGet("/cache/192.168.1.10_8080/docs/intro.md");

        //同じ文書の取得中に接続が切れた、という状況(commitせずにWriterを捨てる)
        {
            PICO_DocCache::Writer w;
            w.begin(kHost, "/docs/intro.md");
            const char* partial = "途中まで";
            w.write(partial, strlen(partial));
            //ここでスコープを抜ける = デストラクタがabort()する
        }

        eq_str(sdGet("/cache/192.168.1.10_8080/docs/intro.md").c_str(), before.c_str(),
               "中断: 既存のキャッシュが壊れない");
        eq_u32((uint32_t)leftoverTempFiles(), 0, "中断: 一時ファイルが残らない");

        PICO_DocCache::Entry e;
        PICO_DocCache::Lookup(kHost, "/docs/intro.md", e);
        eq_str(e.validator.c_str(), "etag-old", "中断: 目録も元のまま");
    }

    // ---- 上限を超えた場合 ----
    {
        reset();
        PICO_DocCache::Writer w;
        check(w.begin(kHost, "/big.bin", 16), "上限: 上限16Bで開始する");
        check(w.write("0123456789", 10), "上限: 範囲内は書ける");
        check(!w.write("0123456789", 10), "上限: 超えるとwrite()が失敗する");
        check(w.hasFailed(), "上限: 失敗が記録される");
        check(!w.commit("etag", 1), "上限: commit()も失敗する");

        check(!sdHas("/cache/192.168.1.10_8080/big.bin"), "上限: 本体を作らない");
        eq_u32((uint32_t)leftoverTempFiles(), 0, "上限: 一時ファイルが残らない");

        PICO_DocCache::Entry e;
        check(!PICO_DocCache::Lookup(kHost, "/big.bin", e), "上限: 目録にも載らない");
    }

    // ---- 上書き ----
    {
        reset();
        {
            PICO_DocCache::Writer w;
            w.begin(kHost, "/a.md");
            w.write("v1", 2);
            w.commit("etag-1", 100);
        }
        {
            PICO_DocCache::Writer w;
            w.begin(kHost, "/a.md");
            w.write("v2-longer", 9);
            w.commit("etag-2", 200);
        }

        eq_str(sdGet("/cache/192.168.1.10_8080/a.md").c_str(), "v2-longer",
               "上書き: 本体が新しい内容になる");
        eq_u32((uint32_t)countLines(indexText()), 1, "上書き: 目録の行が増えない");

        PICO_DocCache::Entry e;
        PICO_DocCache::Lookup(kHost, "/a.md", e);
        eq_str(e.validator.c_str(), "etag-2", "上書き: validatorが新しくなる");
        eq_u32(e.size, 9, "上書き: バイト数が新しくなる");
    }

    // ---- 目録の書き換えが他の行を壊さないこと ----
    {
        reset();
        //3ホスト分を仕込む
        const char* hosts[] = { "a.example", "b.example", "c.example" };
        for(const char* h : hosts){
            PICO_DocCache::Writer w;
            w.begin(h, "/doc.md");
            w.write("x", 1);
            w.commit("v-first", 1);
        }
        eq_u32((uint32_t)countLines(indexText()), 3, "目録: 3件そろう");

        //真ん中の1件だけ差し替える
        {
            PICO_DocCache::Writer w;
            w.begin("b.example", "/doc.md");
            w.write("yy", 2);
            w.commit("v-second", 2);
        }
        eq_u32((uint32_t)countLines(indexText()), 3, "目録: 差し替えても件数が変わらない");

        PICO_DocCache::Entry e;
        PICO_DocCache::Lookup("a.example", "/doc.md", e);
        eq_str(e.validator.c_str(), "v-first", "目録: 前の行が巻き添えにならない");
        PICO_DocCache::Lookup("c.example", "/doc.md", e);
        eq_str(e.validator.c_str(), "v-first", "目録: 後ろの行が巻き添えにならない");
        PICO_DocCache::Lookup("b.example", "/doc.md", e);
        eq_str(e.validator.c_str(), "v-second", "目録: 対象の行だけ変わる");

        //1件消す
        check(PICO_DocCache::Remove("b.example", "/doc.md"), "削除: 成功する");
        check(!sdHas("/cache/b.example/doc.md"), "削除: 本体が消える");
        eq_u32((uint32_t)countLines(indexText()), 2, "削除: 目録が1行減る");
        check(!PICO_DocCache::Lookup("b.example", "/doc.md", e), "削除: 目録から引けなくなる");
        check(PICO_DocCache::Lookup("a.example", "/doc.md", e), "削除: 他の行は残る");
        eq_u32((uint32_t)leftoverTempFiles(), 0, "削除: 一時ファイルが残らない");
    }

    // ---- 目録にコメント行や手書きの行が混ざっていても壊さない ----
    {
        reset();
        HostSd::files[PICO_Path::FILE::CACHE_INDEX_TSV] =
            "# 手で書いたコメント\n"
            "keep.example\t/keep.md\tv-keep\t5\t10\n";

        PICO_DocCache::Writer w;
        w.begin(kHost, "/new.md");
        w.write("n", 1);
        w.commit("v-new", 7);

        const std::string idx = indexText();
        check(idx.find("# 手で書いたコメント") != std::string::npos,
              "目録: コメント行が残る");
        check(idx.find("keep.example\t/keep.md\tv-keep") != std::string::npos,
              "目録: 既存の行が残る");

        PICO_DocCache::Entry e;
        check(PICO_DocCache::Lookup("keep.example", "/keep.md", e), "目録: 既存の行を引ける");
        eq_u32(e.size, 10, "目録: 既存の行の値が保たれる");
        check(PICO_DocCache::Lookup(kHost, "/new.md", e), "目録: 新しい行も引ける");
    }

    // ---- ホストが違えば別のキャッシュになる ----
    {
        reset();
        PICO_DocCache::Writer w1;
        w1.begin("a.example", "/same.md");
        w1.write("A", 1);
        w1.commit("va", 1);

        PICO_DocCache::Writer w2;
        w2.begin("b.example", "/same.md");
        w2.write("B", 1);
        w2.commit("vb", 1);

        eq_str(sdGet("/cache/a.example/same.md").c_str(), "A", "複数サーバ: 別々に保存される");
        eq_str(sdGet("/cache/b.example/same.md").c_str(), "B", "複数サーバ: 互いに上書きしない");
    }

    // ---- マニフェストの引き当て(PROTOCOL.md「4. マニフェスト」) ----
    // ここを間違えると「古い内容を最新と信じて出す」という一番まずい壊れ方をする。
    // 特に、部分一致や切り詰めで別の文書に当たらないことを見ている
    {
        reset();
        HostSd::files["/cache/m.example/v1/manifest"] =
            "/a.md\tv-a\n"
            "/docs/intro.md\tv-intro\tおまけの列\n"
            "/docs/intro.md.bak\tv-bak\n"
            "/z.md\tv-z\n";

        const char* kFile = "/cache/m.example/v1/manifest";
        FixedString<PICO_STR_M> v;

        check(Manifest::VersionOf(kFile, "/a.md", v), "マニフェスト: 先頭の行を引ける");
        eq_str(v.c_str(), "v-a", "マニフェスト: 先頭の行の検証子");

        check(Manifest::VersionOf(kFile, "/z.md", v), "マニフェスト: 末尾の行を引ける");
        eq_str(v.c_str(), "v-z", "マニフェスト: 末尾の行の検証子");

        //3列目以降(将来の拡張)は読み飛ばす
        check(Manifest::VersionOf(kFile, "/docs/intro.md", v), "マニフェスト: 途中の行を引ける");
        eq_str(v.c_str(), "v-intro", "マニフェスト: 知らない列は読み飛ばす");

        //前方一致する別の文書に当たらないこと
        check(Manifest::VersionOf(kFile, "/docs/intro.md.bak", v), "マニフェスト: 似た名前も正しく引ける");
        eq_str(v.c_str(), "v-bak", "マニフェスト: 前方一致で取り違えない");

        check(!Manifest::VersionOf(kFile, "/b.md", v), "マニフェスト: 載っていなければ失敗する");
        eq_str(v.c_str(), "", "マニフェスト: 失敗時は空になる");

        check(!Manifest::VersionOf(kFile, "", v), "マニフェスト: 空のパスは失敗する");
        check(!Manifest::VersionOf("/cache/m.example/none", "/a.md", v),
              "マニフェスト: ファイルが無ければ失敗する");

        //検証子が収まらない行は「分からなかった」扱い。
        //切り詰めて比べると別物を同じと見なしてしまう
        std::string longVersion(PICO_STR_M + 8, 'x');
        HostSd::files["/cache/m.example/long"] = std::string("/a.md\t") + longVersion + "\n";
        check(!Manifest::VersionOf("/cache/m.example/long", "/a.md", v),
              "マニフェスト: 収まらない検証子は切り詰めずに諦める");

        //versionの無い行は突き合わせようがないので飛ばす
        HostSd::files["/cache/m.example/broken"] = "/a.md\n/b.md\tv-b\n";
        check(!Manifest::VersionOf("/cache/m.example/broken", "/a.md", v),
              "マニフェスト: 検証子の無い行は使わない");
        check(Manifest::VersionOf("/cache/m.example/broken", "/b.md", v),
              "マニフェスト: 壊れた行の後ろも読める");
    }

    // 注記: Clear()はディレクトリを再帰的に消すためisDir()が要るが、
    // このスタブはパス->内容のフラットなmapでディレクトリの実体が無いため
    // ここでは検証できない。PCビルド(pc/compat/SdFat.h は実ファイルシステム)側で確認すること。

    printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
