// アプリストアの入れ替えの手順(apps/store/App_Install)と、ストアの「まとめて更新」の選び方(apps/store/App_Store)を
// 通信なしで確かめる。run.sh から呼ぶ。通信を通した一通りは run_net.sh の app_store_net_test。
//
//   - 更新すると前の版のファイルが /sys/store/prev/<id>/ へ移って残る(同じ版の入れ直しでは残さない)。アプリが書いたファイルは残る
//   - 前の版へ戻す: ファイルが戻り、新しいほうの版を見送る(.hold)。もう一度入れると見送りは解ける。削除で全部忘れる
//   - 本体の版の条件(min_os): 通し番号が足りなければ断る。0(PC・手元のビルド)は見ない
//   - AppStore(保存した一覧を読んだもの): 見送り・権限が増える・本体が古いものは、まとめて更新にも更新の数にも入れない
//   - StoreWatch::Fingerprint: 同じ顔ぶれなら同じ値、違えば違う値、空は0
//
// 入れたファイルの中身(SHA-256)はここでは確かめない(呼び出し側の AppStore / AppPackage の仕事)。
#include "apps/store/App_Install.hpp"
#include "apps/store/App_Store.hpp"
#include "apps/store/Store_Watch.hpp"
#include "apps/store/App_Repo.hpp"
#include "apps/ssh/Ssh_Sha256.hpp"
#include "services/App_Functions.hpp"
#include "gui/core/Scene_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "gui/core/Scene.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

// ---- 差し替え ----
void LogFunctions::Log(LogType, const char* fmt, ...){
    if(!getenv("STORE_TEST_VERBOSE")) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}
void SceneFunctions::Push(Scene* next){ delete next; }
Scene* MakeLuaAppScene(const AppEntry&){ return nullptr; }

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

static bool Has(const std::string& path){ return HostSd::files.count(path) > 0; }
static std::string Get(const std::string& path){ return Has(path) ? HostSd::files[path] : std::string("(無い)"); }

static std::string Hex(const uint8_t* p, size_t n){
    char buf[65];
    AppRepo::HexEncode(p, n, buf);
    return std::string(buf, n * 2);
}

using Files = std::vector<std::pair<std::string, std::string>>;

// 置き場(/sys/store/staging/)へ1つの版を置き、その版の Entry を作る(取ってきて確かめ終えたのと同じ状態)
static AppRepo::Entry Stage(const char* id, const char* version, const char* dir, Files files, uint8_t perms = 0){
    std::string tsv;
    uint32_t total = 0;
    for(auto& f : files){
        HostSd::files[std::string("/sys/store/staging/app/") + f.first] = f.second;
        uint8_t d[32];
        Sha256 sha;
        sha.update(f.second.data(), f.second.size());
        sha.finish(d);
        tsv += f.first + "\t" + std::to_string(f.second.size()) + "\t" + Hex(d, 32) + "\n";
        total += (uint32_t)f.second.size();
    }
    HostSd::files["/sys/store/staging/files.tsv"] = tsv;
    AppRepo::Entry e;
    e.id.assign(id);
    e.version.assign(version);
    e.dir.assign(dir);
    e.name.assign(dir);
    e.bytes = total;
    e.perms = perms;
    memset(e.author_key, 0x11, 32);
    return e;
}

static std::string Cfg(const char* id, const char* version, const char* extra = ""){
    return std::string("name=アプリ\nid=") + id + "\nversion=" + version + "\n" + extra;
}

static const char* Install(const AppRepo::Entry& e){
    static AppInstall::Work w;
    int count = 0;
    if(const char* err = AppInstall::CheckFilesList(e, w, count)) return err;
    if(const char* err = AppInstall::CheckStagedConfig(e)) return err;
    return AppInstall::Commit(e, false, w);
}

static uint32_t g_os_serial = 0;
static uint32_t FakeSerial(){ return g_os_serial; }

static void testRollback(){
    const std::string dir = "/lua/apps/ゲーム";
    const std::string prev = "/sys/store/prev/game";

    // ---- 1.0.0 を入れる ----
    auto v1 = Stage("game", "1.0.0", "ゲーム", {{"app.cfg", Cfg("game", "1.0.0")}, {"main.lua", "-- v1\n"}, {"old.lua", "-- old\n"}});
    check(Install(v1) == nullptr, "1.0.0 を入れる");
    check(Get(dir + "/main.lua") == "-- v1\n" && Has(dir + "/old.lua"), "1.0.0 のファイルが入る");
    FixedString<PICO_STR_S> ver;
    check(!AppInstall::PreviousVersion("game", ver), "初めて入れたときは前の版が無い");
    HostSd::files[dir + "/save.dat"] = "SAVE";  // アプリが書いたファイル

    // ---- 1.1.0 へ更新: 前の版が残る ----
    auto v2 = Stage("game", "1.1.0", "ゲーム", {{"app.cfg", Cfg("game", "1.1.0")}, {"main.lua", "-- v2\n"}, {"new.lua", "-- new\n"}});
    check(AppInstall::Check(v2) == AppInstall::Installed::Update, "1.1.0 は更新");
    check(Install(v2) == nullptr, "1.1.0 へ更新する");
    check(Get(dir + "/main.lua") == "-- v2\n" && Has(dir + "/new.lua") && !Has(dir + "/old.lua"),
          "更新: 新しい版のファイル・前の版にだけあったファイルは消える");
    check(Get(dir + "/save.dat") == "SAVE", "更新: アプリが書いたファイルは残る");
    check(Get(prev + "/app/main.lua") == "-- v1\n" && Has(prev + "/app/old.lua") && Has(prev + "/files.tsv"),
          "更新: 前の版のファイルと一覧が /sys/store/prev/game/ に残る");
    check(!Has(prev + "/app/save.dat"), "更新: アプリが書いたファイルは前の版の置き場へ移さない");
    check(AppInstall::PreviousVersion("game", ver) && ver == "1.0.0", "前の版は 1.0.0");

    // ---- 同じ版の入れ直しでは前の版を替えない ----
    auto v2b = Stage("game", "1.1.0", "ゲーム", {{"app.cfg", Cfg("game", "1.1.0")}, {"main.lua", "-- v2\n"}, {"new.lua", "-- new\n"}});
    check(Install(v2b) == nullptr, "1.1.0 を入れ直す");
    check(AppInstall::PreviousVersion("game", ver) && ver == "1.0.0" && Get(prev + "/app/main.lua") == "-- v1\n",
          "入れ直し: 前の版は 1.0.0 のまま");

    // ---- 前の版へ戻す ----
    static AppInstall::Work w;
    FixedString<PICO_STR_S> restored;
    check(AppInstall::Rollback("game", dir.c_str(), w, &restored) == nullptr && restored == "1.0.0", "1.0.0 へ戻す");
    check(Get(dir + "/main.lua") == "-- v1\n" && Has(dir + "/old.lua") && !Has(dir + "/new.lua"),
          "戻す: 前の版のファイルが戻り、新しい版にだけあったファイルは消える");
    check(Get(dir + "/save.dat") == "SAVE", "戻す: アプリが書いたファイルは残る");
    check(!AppInstall::PreviousVersion("game", ver) && !Has(prev + "/files.tsv") && !Has(prev + "/app/main.lua"),
          "戻す: 前の版の置き場は空になる(戻せるのは1回)");
    check(AppInstall::HeldVersion("game", ver) && ver == "1.1.0", "戻す: 1.1.0 を見送る");
    check(AppInstall::Check(v2) == AppInstall::Installed::Update, "戻す: 1.1.0 はまた更新として見える");
    check(AppInstall::Rollback("game", dir.c_str(), w) != nullptr, "戻す: 前の版が無ければ断る");

    // 戻したあとの一覧は 1.0.0 のもの(次の更新で 1.0.0 のファイルが前の版として残る)
    check(Get("/sys/store/installed/game.tsv").find("old.lua") != std::string::npos, "戻す: 入っている一覧も 1.0.0 のもの");

    // ---- もう一度更新すると見送りは解ける ----
    auto v2c = Stage("game", "1.1.0", "ゲーム", {{"app.cfg", Cfg("game", "1.1.0")}, {"main.lua", "-- v2\n"}, {"new.lua", "-- new\n"}});
    check(Install(v2c) == nullptr && !AppInstall::HeldVersion("game", ver), "更新し直すと見送りは解ける");
    check(AppInstall::PreviousVersion("game", ver) && ver == "1.0.0", "更新し直すと前の版がまた残る");

    // ---- さらに更新すると、前の版は1つ前のものに替わる(前の前の版は残さない) ----
    auto v3 = Stage("game", "1.2.0", "ゲーム", {{"app.cfg", Cfg("game", "1.2.0")}, {"main.lua", "-- v3\n"}});
    check(Install(v3) == nullptr, "1.2.0 へ更新する");
    check(AppInstall::PreviousVersion("game", ver) && ver == "1.1.0" && Get(prev + "/app/main.lua") == "-- v2\n"
          && Has(prev + "/app/new.lua") && !Has(prev + "/app/old.lua"),
          "前の版は 1.1.0 に替わり、1.0.0 のファイルは残らない");

    // ---- 削除で全部忘れる ----
    AppInstall::Forget("game");
    check(!AppInstall::PreviousVersion("game", ver) && !Has(prev + "/files.tsv") && !Has("/sys/store/installed/game.tsv"),
          "削除: 前の版・入れた一覧を忘れる");
}

static void testMinOs(){
    AppRepo::Entry e;
    e.min_os = 200;
    AppInstall::os_serial_hook = &FakeSerial;
    g_os_serial = 100;
    const char* err = AppInstall::CheckOs(e);
    check(err && strstr(err, "#200") && strstr(err, "#100"), "min_os: 本体が古ければ断る(要る番号と今の番号を見せる)");
    g_os_serial = 200;
    check(AppInstall::CheckOs(e) == nullptr, "min_os: 同じ番号なら入れられる");
    g_os_serial = 0;
    check(AppInstall::CheckOs(e) == nullptr, "min_os: 通し番号0(PC・手元のビルド)は見ない");

    // パッケージや一覧から入れる前に断る(Commitまで行かない)
    g_os_serial = 100;
    auto pkg = Stage("newos", "1.0.0", "新しい", {{"app.cfg", Cfg("newos", "1.0.0")}, {"main.lua", "--\n"}});
    pkg.min_os = 150;
    check(AppInstall::CheckOs(pkg) != nullptr, "min_os: 150 が要るアプリは #100 では入れない");
    AppInstall::os_serial_hook = nullptr;
    HostSd::files.erase("/sys/store/staging/files.tsv");
}

// 保存した一覧(/sys/store/catalog.tsv)を読んだ AppStore で、まとめて更新と更新の数の選び方を見る
static void testBatchSelection(){
    const std::string key(64, '1');
    const std::string hash(64, '0');
    auto line = [&](const char* id, const char* ver, const char* dir, const char* perms, const char* min_os){
        return std::string(id) + "\t" + ver + "\t" + dir + "\t" + dir + "\t10\t" + hash + "\t" + perms + "\t説明\t" + key
             + "\tauthor\t-\t" + min_os + "\n";
    };
    HostSd::files["/sys/store/catalog.tsv"] = std::string("pico-os-apps\t2\t5\n")
        + line("plain", "2.0", "ふつう", "-", "-")          // 更新できる
        + line("grow", "2.0", "権限", "network", "-")       // 権限が増える
        + line("held", "2.0", "見送り", "-", "-")           // 2.0 を見送っている
        + line("newos", "2.0", "本体", "-", "500")          // 本体が古い
        + line("same", "1.0", "同じ", "-", "-")             // 同じ版
        + line("absent", "1.0", "無い", "-", "-");          // 入っていない
    HostSd::files["/sys/store/state.cfg"] = std::string("url = ") + AppStore::kDefaultUrl + "\nserial = 5\n";
    auto put = [&](const char* id, const char* dir, const char* ver){
        HostSd::files[std::string("/lua/apps/") + dir + "/main.lua"] = "--\n";
        HostSd::files[std::string("/lua/apps/") + dir + "/app.cfg"] = Cfg(id, ver);
    };
    put("plain", "ふつう", "1.0");
    put("grow", "権限", "1.0");
    put("held", "見送り", "1.0");
    put("newos", "本体", "1.0");
    put("same", "同じ", "1.0");
    HostSd::files["/sys/store/installed/held.hold"] = "2.0\n";

    AppInstall::os_serial_hook = &FakeSerial;
    g_os_serial = 300;
    AppStore s;
    check(s.loadCachedCatalog() && s.count() == 6, "保存した一覧を読む(6件)");
    auto idx = [&](const char* id){
        for(int i = 0; i < s.count(); i++) if(s.entry(i)->id == id) return i;
        return -1;
    };
    check(s.entry(idx("newos"))->min_os == 500 && s.needsNewerOs(idx("newos")) && !s.installable(idx("newos")),
          "本体が古いものは入れられない");
    check(s.batchUpdatable(idx("plain")), "まとめて更新: ふつうの更新は入る");
    check(s.permsGrow(idx("grow")) && !s.batchUpdatable(idx("grow")), "まとめて更新: 権限が増えるものは入れない");
    check(s.held(idx("held")) && !s.batchUpdatable(idx("held")), "まとめて更新: 見送っている版は入れない");
    check(!s.batchUpdatable(idx("newos")), "まとめて更新: 本体が古いものは入れない");
    check(!s.batchUpdatable(idx("same")) && !s.batchUpdatable(idx("absent")), "まとめて更新: 同じ版・入っていないものは入れない");
    check(s.batchCount() == 1, "まとめて更新は1件");
    check(s.updateCount() == 2, "更新の知らせは2件(権限が増えるものも数える。見送り・本体が古いものは数えない)");

    // 見送っているより新しい版が出たら、また知らせる
    HostSd::files["/sys/store/installed/held.hold"] = "1.5\n";
    AppStore s2;
    s2.loadCachedCatalog();
    int h = -1;
    for(int i = 0; i < s2.count(); i++) if(s2.entry(i)->id == "held") h = i;
    check(!s2.held(h) && s2.batchUpdatable(h), "見送った版より新しい版は見送らない");
    AppInstall::os_serial_hook = nullptr;
}

static void testFingerprint(){
    const char* ids1[] = {"a", "b"};
    const char* vers1[] = {"1.0", "2.0"};
    const char* vers2[] = {"1.0", "2.1"};
    const uint32_t f1 = StoreWatch::Fingerprint(ids1, vers1, 2);
    check(f1 != 0 && f1 == StoreWatch::Fingerprint(ids1, vers1, 2), "指紋: 同じ顔ぶれなら同じ");
    check(f1 != StoreWatch::Fingerprint(ids1, vers2, 2), "指紋: 版が変われば変わる");
    check(f1 != StoreWatch::Fingerprint(ids1, vers1, 1), "指紋: 数が変われば変わる");
    check(StoreWatch::Fingerprint(ids1, vers1, 0) == 0, "指紋: 空は0");
}

int main(){
    setvbuf(stdout, nullptr, _IONBF, 0);
    OSData::SD_usable = true;
    HostSd::dirs_enabled = true;  // 前の版の置き場を removeRecursive で片付けるため

    testRollback();
    testMinOs();
    testBatchSelection();
    testFingerprint();

    printf("%s (%d件の失敗)\n", failures == 0 ? "全部通った" : "失敗あり", failures);
    return failures == 0 ? 0 : 1;
}
