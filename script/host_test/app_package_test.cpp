// SDに置いたアプリのパッケージ(.papp。apps/store/App_Package)を入れる係のテスト。
//
// パッケージは app_package_tool_test.py が script/build_app_repo.py で作り、このテストへディレクトリを渡す
// (Pythonが書いたものを本物の App_Package が読むので、両方の形が揃っていることも確かめる)。
// SDは stubs/ のメモリ上のもの(HostSd::dirs_enabled でフォルダの走査も見る)。
//
// 使い方: app_package_test <パッケージを置いたディレクトリ>
#include "apps/store/App_Package.hpp"
#include "apps/store/App_Install.hpp"
#include "services/App_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "system/Notification_Functions.hpp"
#include "gui/core/GFX_Functions.hpp"
#include "gui/core/Keyboard_Functions.hpp"
#include "OS_Data.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

// ---- モック(lua_app_scanner_test.cpp と同じ) ----
void LogFunctions::Log(LogType, const char* fmt, ...){
    if(!getenv("PACKAGE_TEST_VERBOSE")) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}
void PICO_GFX::MarkDirty(const Rect&){}
void PICO_GFX::Setup(){}
void PICO_GFX::FlushDirty(){}
void PICO_GFX::DrawDialogBackground(){}
void KeyboardFunctions::HideAll(){}
void KeyboardFunctions::Setup(){}
void KeyboardFunctions::RegisterInputTarget(ITextInputTarget*){}
void KeyboardFunctions::UnregisterInputTarget(ITextInputTarget*){}
void KeyboardFunctions::Show(ITextInputTarget*, KeyboardFunctions::Layout, bool){}
void KeyboardFunctions::OnPanelShown(KeyboardPanel*){}
void KeyboardFunctions::OnPanelHidden(KeyboardPanel*){}
void KeyboardFunctions::OnPanelResized(KeyboardPanel*){}
void KeyboardFunctions::OnPanelChanged(KeyboardPanel*, bool){}

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

static std::string g_dir;

static std::string ReadHostFile(const std::string& path){
    std::ifstream f(path, std::ios::binary);
    if(!f){
        fprintf(stderr, "%s がありません\n", path.c_str());
        exit(2);
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static bool Has(const std::string& path){ return HostSd::files.count(path) > 0; }
static const std::string& Sd(const std::string& path){ return HostSd::files[path]; }

// <dir>/<name>.papp をSDの /<name>.papp へ置いて入れる
static bool Put(const char* name){
    HostSd::files[std::string("/") + name + ".papp"] = ReadHostFile(g_dir + "/" + name + ".papp");
    return true;
}
static bool InstallOne(const char* name, std::string* msg = nullptr){
    Put(name);
    FixedString<PICO_STR_LL> m;
    FixedString<PICO_STR_M> app;
    const std::string path = std::string("/") + name + ".papp";
    const bool ok = AppPackage::Install(path.c_str(), m, &app);
    if(msg) *msg = m.c_str();
    if(getenv("PACKAGE_TEST_VERBOSE")) fprintf(stderr, "  %s -> %s\n", name, m.c_str());
    HostSd::files.erase(path);
    return ok;
}
static bool MsgHas(const std::string& msg, const char* part){ return msg.find(part) != std::string::npos; }

static const char* kTetris = "/lua/apps/テトリス";

int main(int argc, char** argv){
    if(argc < 2){
        fprintf(stderr, "使い方: app_package_test <dir>\n");
        return 2;
    }
    g_dir = argv[1];
    OSData::SD_usable = true;
    HostSd::dirs_enabled = true;
    AppFunctions::Clear();

    // ---- 新しく入れる ----
    std::string msg;
    check(InstallOne("tetris-2.0.0", &msg), "入れられる(tetris 2.0.0)");
    check(MsgHas(msg, "テトリス 2.0.0 を入れました"), "結果の1行に名前と版");
    check(Has(std::string(kTetris) + "/main.lua") && Has(std::string(kTetris) + "/app.cfg")
          && Has(std::string(kTetris) + "/bgm.mml"), "/lua/apps/テトリス/ へファイルが入る");
    check(Sd(std::string(kTetris) + "/main.lua") == ReadHostFile(g_dir + "/src/テトリス/main.lua"), "中身がそのまま入る");
    check(Has("/sys/store/installed/tetris.tsv") && Has("/sys/store/installed/tetris.key"),
          "入れたファイルの一覧と作者の鍵を覚える(ストアから入れたときと同じ)");
    check(Sd("/sys/store/installed/tetris.key").substr(0, 64) == ReadHostFile(g_dir + "/author_a.txt"), "覚えた鍵は作者の鍵");
    check(AppFunctions::FindByArg("/lua/apps/テトリス/main.lua") >= 0, "ランチャの登録簿へ入る");
    bool staged = false;
    for(auto& kv : HostSd::files) if(kv.first.rfind("/sys/store/staging", 0) == 0) staged = true;
    check(!staged, "置き場(/sys/store/staging)が片付く");

    // ---- 同じ版の入れ直し・更新 ----
    HostSd::files[std::string(kTetris) + "/hiscore.txt"] = "12345";
    check(InstallOne("tetris-2.0.0"), "同じ版は入れ直せる");
    check(InstallOne("tetris-2.1.0", &msg), "新しい版で更新できる");
    check(!Has(std::string(kTetris) + "/bgm.mml"), "更新: 新しい版に無いファイルは消える");
    check(Has(std::string(kTetris) + "/extra/data.txt"), "更新: 増えたサブディレクトリのファイルが入る");
    check(Has(std::string(kTetris) + "/hiscore.txt") && Sd(std::string(kTetris) + "/hiscore.txt") == "12345",
          "更新: アプリが書いたファイル(セーブ)は残る");

    // ---- 断るもの ----
    check(!InstallOne("tetris-2.0.0", &msg) && MsgHas(msg, "古い版"), "入っている版より古い版は断る");
    check(!InstallOne("tetris-otherkey", &msg) && MsgHas(msg, "作者の鍵が変わりました"), "覚えた鍵と違う作者の鍵は断る");
    check(!InstallOne("tetris-tampered", &msg) && MsgHas(msg, "main.lua の中身が一覧と合いません"), "書き換えたファイルは断る");
    check(Sd(std::string(kTetris) + "/main.lua") == ReadHostFile(g_dir + "/v2/テトリス/main.lua"),
          "断ったときは入っているものに触らない");
    check(!InstallOne("tetris-badsig", &msg) && MsgHas(msg, "作者の署名が合いません"), "壊れた署名は断る");
    check(!InstallOne("tetris-wrongkey", &msg) && MsgHas(msg, "鍵がアプリの情報と合いません"), "情報と違う鍵の署名は断る");
    check(!InstallOne("tetris-perms", &msg) && MsgHas(msg, "権限が一覧より多く"), "app.cfgの権限が情報より多いものは断る");
    check(!InstallOne("tetris-yanked", &msg) && MsgHas(msg, "取り下げ"), "取り下げの印が付いたものは断る");
    check(!InstallOne("tetris-trailing", &msg) && MsgHas(msg, "余分なデータ"), "後ろに余分なデータがあるものは断る");
    check(!InstallOne("tetris-truncated", &msg) && MsgHas(msg, "途中で切れて"), "途中で切れたものは断る");
    check(!InstallOne("tetris-format9", &msg) && MsgHas(msg, "知らない形式"), "知らない形式の番号は断る");
    check(!InstallOne("notapackage", &msg) && MsgHas(msg, "パッケージではありません"), "パッケージでないファイルは断る");
    check(!InstallOne("tetris-filesmismatch", &msg) && MsgHas(msg, "files.tsv"), "files.tsvが情報のSHA-256と違うものは断る");

    // ---- 同じ名前のディレクトリ ----
    // id の無いアプリ(手で入れた)へは上書きする。手で置いたファイルは残る
    HostSd::files["/lua/apps/リバーシ/main.lua"] = "-- 手で置いた";
    HostSd::files["/lua/apps/リバーシ/app.cfg"] = "name=リバーシ\n";
    HostSd::files["/lua/apps/リバーシ/memo.txt"] = "手で置いたファイル";
    check(InstallOne("reversi-2.0.0"), "idの無い同じ名前のアプリへは上書きして入れる");
    check(Sd("/lua/apps/リバーシ/main.lua") != "-- 手で置いた" && Has("/lua/apps/リバーシ/memo.txt"),
          "上書き: パッケージのファイルに置き換わり、手で置いたファイルは残る");
    // 別の id のアプリがある
    HostSd::files["/lua/apps/テトリス/app.cfg"] = "name=テトリス\nid=other\nversion=1.0\n";
    check(!InstallOne("tetris-2.2.0", &msg) && MsgHas(msg, "別のアプリ"), "同じ名前に別のidのアプリがあれば断る");

    // ---- 起動時の一括(SDの直下を走査) ----
    HostSd::files.clear();
    AppFunctions::Clear();
    NotificationFunctions::ClearHistory();
    Put("tetris-2.0.0");
    HostSd::files["/BLOCKS.PAPP"] = ReadHostFile(g_dir + "/blocks-1.2.0.papp"); //拡張子の大小は問わない
    Put("tetris-tampered");
    HostSd::files["/notes.txt"] = "パッケージではないファイル";
    HostSd::files["/sub/inner.papp"] = ReadHostFile(g_dir + "/reversi-2.0.0.papp");
    const int n = AppPackage::InstallPending();
    // tetris-2.0.0 と tetris-tampered は同じ id。名前の順に tetris-2.0.0 が先に入り、tampered は中身で断る
    check(n == 2, "InstallPending: 2件入る(BLOCKS.PAPP と tetris-2.0.0)");
    check(!Has("/tetris-2.0.0.papp") && !Has("/BLOCKS.PAPP"), "InstallPending: 入れたパッケージは消す");
    check(!Has("/tetris-tampered.papp") && Has("/tetris-tampered.papp.err"), "InstallPending: 入れられなかったものは .err へ改名");
    check(Has("/notes.txt") && Has("/sub/inner.papp"), "InstallPending: 直下の .papp 以外には触らない");
    check(AppFunctions::FindByArg("/lua/apps/テトリス/main.lua") >= 0, "InstallPending: 登録簿へ入る");
    AppPackage::PostPendingNotice();
    check(NotificationFunctions::HistoryCount() == 2, "PostPendingNotice: 入れた通知と入れられない通知の2件");
    AppPackage::PostPendingNotice();
    check(NotificationFunctions::HistoryCount() == 2, "PostPendingNotice: 2回目は何も出さない");
    check(AppPackage::InstallPending() == 0, "InstallPending: .err は次の起動で試さない");

    // ---- SD無し ----
    OSData::SD_usable = false;
    check(AppPackage::InstallPending() == 0, "SD無しの間は何もしない");

    printf("\n%s (%d failures)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
