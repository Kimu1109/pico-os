// ファイルアプリのコピー/移動/名前の変更の下請け(storage/SD_IO)のテスト。
//
// stubs/SdFat.h はパス->内容のフラットなmapでディレクトリの実体が無いので、
// フォルダの再帰コピーはPCビルド(pc/compat/SdFat.hは実ファイルシステム)で確かめる。
// ここではファイルのコピー・名前の重なりの回避・パスの判定を見る。
#include "storage/SD_IO.hpp"
#include "system/Log_Functions.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstring>
#include <string>

void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

static int ticks = 0;
static void Tick(){ ticks++; }

int main(){
    // --- isSameOrInside ---
    check(PICO_IO::isSameOrInside("/a", "/a"), "同じパス");
    check(PICO_IO::isSameOrInside("/a/b/c", "/a"), "子孫");
    check(!PICO_IO::isSameOrInside("/ab", "/a"), "名前の頭が同じだけの兄弟は中ではない");
    check(!PICO_IO::isSameOrInside("/a", "/a/b"), "親は中ではない");
    check(PICO_IO::isSameOrInside("/x", "/"), "ルートの中");

    // --- isValidName ---
    check(PICO_IO::isValidName("memo.txt"), "普通の名前");
    check(PICO_IO::isValidName("日本語 名前"), "日本語と空白");
    check(!PICO_IO::isValidName(""), "空");
    check(!PICO_IO::isValidName("."), ".");
    check(!PICO_IO::isValidName(".."), "..");
    check(!PICO_IO::isValidName("a/b"), "区切り");
    check(!PICO_IO::isValidName("a:b"), "コロン");
    check(!PICO_IO::isValidName("a?"), "?");
    check(!PICO_IO::isValidName("a."), "末尾の.");
    check(!PICO_IO::isValidName("a "), "末尾の空白");
    check(!PICO_IO::isValidName("a\nb"), "改行");

    // --- copyRecursive(ファイル) ---
    HostSd::files.clear();
    std::string big;
    for(int i = 0; i < 3000; i++) big.push_back((char)(i * 7));
    HostSd::files["/src.bin"] = big;
    ticks = 0;
    check(PICO_IO::copyRecursive("/src.bin", "/dst.bin", &Tick), "ファイルをコピーできる");
    check(HostSd::files.count("/dst.bin") && HostSd::files["/dst.bin"] == big, "中身が同じ(512Bの区切りをまたぐ)");
    check(HostSd::files["/src.bin"] == big, "元は残る");
    check(ticks >= 6, "読み書きのたびにtickが呼ばれる");
    check(!PICO_IO::copyRecursive("/src.bin", "/dst.bin"), "行き先が既にあれば断る");
    check(!PICO_IO::copyRecursive("/none.bin", "/x.bin"), "元が無ければ失敗");
    check(HostSd::files.count("/x.bin") == 0, "失敗したら行き先を残さない");
    check(!PICO_IO::copyRecursive("/a", "/a/b"), "自分の中へは断る");

    // --- uniquePath ---
    FixedString<PICO_PATH_LEN> out;
    HostSd::files.clear();
    check(PICO_IO::uniquePath(out, "/d", "memo.txt") && out == "/d/memo.txt", "重ならなければそのまま");
    HostSd::files["/d/memo.txt"] = "x";
    check(PICO_IO::uniquePath(out, "/d", "memo.txt") && out == "/d/memo (2).txt", "重なれば (2)");
    HostSd::files["/d/memo (2).txt"] = "x";
    check(PICO_IO::uniquePath(out, "/d", "memo.txt") && out == "/d/memo (3).txt", "次は (3)");
    HostSd::files["/d/folder"] = "";
    check(PICO_IO::uniquePath(out, "/d", "folder") && out == "/d/folder (2)", "拡張子なし");
    HostSd::files["/.hidden"] = "";
    check(PICO_IO::uniquePath(out, "/", ".hidden") && out == "/.hidden (2)", "先頭の.は拡張子ではない");
    check(PICO_IO::uniquePath(out, "/", "a.tar.gz") && out == "/a.tar.gz", "最後の.だけを拡張子として見る(重ならない)");
    HostSd::files["/a.tar.gz"] = "";
    check(PICO_IO::uniquePath(out, "/", "a.tar.gz") && out == "/a.tar (2).gz", "最後の.の手前に挟む");

    printf("\n%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
