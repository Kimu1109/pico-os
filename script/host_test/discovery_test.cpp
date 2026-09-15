// サーバ情報(/.well-known/pico-os)の解釈のテスト。
//
// 一番効くのは**前方互換**のほう。PROTOCOL.mdで「クライアントは知らないキーを
// 無視する」と約束しているので、サーバが将来キーを足しても古いpico-osが
// 壊れないことをここで固定する。
//
// もう1つは「searchの行が無い = 検索非対応」という判定。これを取り違えると、
// 対応していないサーバで検索ボタンが押せてしまう。
#include "net/Discovery.hpp"
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
static void eq_str(const char* a, const char* e, const char* label){
    const bool ok = (strcmp(a, e) == 0);
    printf("%s %-46s 実測=%-24s 期待=%s\n", ok ? "[ OK ]" : "[FAIL]", label, a, e);
    if(!ok) failures++;
}
static void eq_int(long a, long e, const char* label){
    const bool ok = (a == e);
    printf("%s %-46s 実測=%ld 期待=%ld\n", ok ? "[ OK ]" : "[FAIL]", label, a, e);
    if(!ok) failures++;
}

static bool parse(const std::string& tsv, ServerInfo& out){
    HostSd::files["/info.tsv"] = tsv;
    return Discovery::ParseFile("/info.tsv", out);
}

int main(){
    // ---- 全部そろっている場合 ----
    {
        ServerInfo info;
        check(parse(
            "version\t1\n"
            "name\tきむのドキュメント\n"
            "home\t/docs/index.md\n"
            "search\t/v1/search\n"
            "manifest\t/v1/manifest\n", info), "解釈できる");

        eq_int(info.version, 1, "version");
        eq_str(info.name.c_str(), "きむのドキュメント", "name");
        eq_str(info.home.c_str(), "/docs/index.md", "home");
        eq_str(info.search.c_str(), "/v1/search", "search");
        eq_str(info.manifest.c_str(), "/v1/manifest", "manifest");
        check(info.hasSearch(), "検索対応と判定できる");
    }

    // ---- 前方互換: 知らないキーは無視する ----
    // サーバが将来キーを足しても古いpico-osが壊れないための約束(PROTOCOL.md)
    {
        ServerInfo info;
        parse(
            "version\t2\n"
            "feed\t/v1/feed\n"          // まだ知らないキー
            "comment\t/v1/comment\n"    // まだ知らないキー
            "name\t未来のサーバ\n", info);

        eq_int(info.version, 2, "知らないキーがあっても他を読める(version)");
        eq_str(info.name.c_str(), "未来のサーバ", "知らないキーがあっても他を読める(name)");
        check(!info.hasSearch(), "知らないキーをsearchと取り違えない");
    }

    // ---- searchの行が無ければ検索非対応 ----
    {
        ServerInfo info;
        parse("version\t1\nname\t静的サーバ\n", info);
        check(!info.hasSearch(), "searchの行が無ければ検索非対応");
        eq_str(info.search.c_str(), "", "searchは空のまま");
    }

    // ---- コメント・空行・壊れた行 ----
    {
        ServerInfo info;
        parse(
            "# これはコメント\n"
            "\n"
            "区切りの無い行\n"
            "version\t1\n"
            "\n"
            "search\t/s\n", info);

        eq_int(info.version, 1, "コメントと空行を挟んでも読める");
        eq_str(info.search.c_str(), "/s", "区切りの無い行を飛ばして読み進める");
    }

    // ---- 値の前の空白は落とす ----
    {
        ServerInfo info;
        parse("name\t   そろえてある名前\n", info);
        eq_str(info.name.c_str(), "そろえてある名前", "値の前の空白を落とす");
    }

    // ---- CRLFでも読める ----
    {
        ServerInfo info;
        parse("version\t1\r\nsearch\t/v1/search\r\n", info);
        eq_int(info.version, 1, "CRLFでも読める(version)");
        eq_str(info.search.c_str(), "/v1/search", "CRLFでも読める(search)");
    }

    // ---- 読み直すと前の内容が残らない ----
    {
        ServerInfo info;
        parse("search\t/old\nname\t古いサーバ\n", info);
        parse("version\t1\n", info);
        check(!info.hasSearch(), "読み直すと前のsearchが残らない");
        eq_str(info.name.c_str(), "", "読み直すと前のnameが残らない");
    }

    // ---- 無い場合 ----
    {
        ServerInfo info;
        HostSd::files.clear();
        check(!Discovery::ParseFile("/none.tsv", info), "ファイルが無ければfalse");
        check(!Discovery::ParseFile(nullptr, info), "nullptrならfalse");
    }

    // ---- clear() ----
    {
        ServerInfo info;
        parse("version\t1\nsearch\t/s\n", info);
        info.host.assign("example.test");
        info.checked = true;

        info.clear();
        check(!info.checked && info.host.empty() && !info.hasSearch() && info.version == 0,
              "clear()で全部戻る");
    }

    printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
