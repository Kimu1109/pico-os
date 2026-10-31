// アプリストアの通信とインストール(apps/store/App_Store)を、本物のHTTPで確かめる(run_net.sh)。
//
// 配布物は app_store_fixture.py が <dir>/variants/ に作る。http.server が <dir>/www を配り、
// <dir>/www/apps はその時々の variant へのシンボリックリンク(テストが張り替える)。
// 同じURLの中身が時間とともに変わる(更新・古い一覧へ戻される)ところまで確かめられる。
//
// SDは stubs/ のメモリ上のもの(ディレクトリの実体が無いので、削除の再帰はここでは確かめない)。
//
// 使い方: app_store_net_test <port> <dir>
#include "apps/store/App_Store.hpp"
#include "apps/store/App_Repo.hpp"
#include "apps/store/App_Repo_Keys.hpp"
#include "services/App_Functions.hpp"
#include "gui/core/Scene_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "gui/core/Scene.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <chrono>
#include <unistd.h>

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

static std::string g_dir;

static void Use(const char* variant){
    const std::string link = g_dir + "/www/apps";
    unlink(link.c_str());
    const std::string target = g_dir + "/variants/" + variant;
    if(symlink(target.c_str(), link.c_str()) != 0) perror("symlink");
}

static std::string ReadHostFile(const std::string& path){
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static bool Run(AppStore& s){
    for(int i = 0; i < 20000 && s.busy(); i++){
        s.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if(getenv("STORE_TEST_VERBOSE")) fprintf(stderr, "  -> %s\n", s.message());
    return s.state() == AppStore::State::Done;
}

static int Find(AppStore& s, const char* id){
    for(int i = 0; i < s.count(); i++) if(s.entry(i)->id == id) return i;
    return -1;
}

static bool Has(const char* path){ return HostSd::files.count(path) > 0; }

// SDのスタブは removeRecursive でディレクトリを辿れないので、本物のSDで消えたのと同じにする
static void EraseDir(const std::string& prefix){
    for(auto it = HostSd::files.begin(); it != HostSd::files.end();){
        if(it->first.compare(0, prefix.size(), prefix) == 0) it = HostSd::files.erase(it);
        else ++it;
    }
}

int main(int argc, char** argv){
    if(argc < 3){
        fprintf(stderr, "usage: %s <port> <dir>\n", argv[0]);
        return 2;
    }
    setvbuf(stdout, nullptr, _IONBF, 0); //落ちたときもそこまでの結果が見えるように
    const int port = atoi(argv[1]);
    g_dir = argv[2];
    OSData::SD_usable = true;

    char url[128];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/apps", port);
    uint8_t pub[32];
    const std::string pubhex = ReadHostFile(g_dir + "/pubkey.txt");
    AppRepo::HexDecode(pubhex.c_str(), 64, pub, 32);

    const std::string pubhex2 = ReadHostFile(g_dir + "/pubkey2.txt");

    // store.cfg から url と key を読む
    HostSd::files["/sys/store.cfg"] = std::string("url = ") + url + "\nkey = " + pubhex + "\nkey = zz\nkey = " + pubhex2 + "\n";

    const std::string tet_src = g_dir + "/variants/good/tetris/2.0.0/";
    const std::string tet_v2 = g_dir + "/variants/v2/tetris/2.1.0/";

    // ---- 1. 一覧の取得とインストール ----
    {
        Use("good");
        AppStore s;
        s.loadConfig();
        check(strcmp(s.baseUrl(), (std::string(url) + "/").c_str()) == 0, "config: urlの末尾に/を足す");
        check(!s.loadCachedCatalog(), "cache: まだ無い");
        check(s.beginRefresh() && Run(s), "refresh: 一覧を取れる");
        check(s.count() == 2 && s.catalogSerial() == 100, "refresh: 2件・serial 100");
        check(Has("/sys/store/catalog.tsv") && !Has("/sys/store/catalog.part"), "refresh: 確かめた一覧を保存");
        const int t = Find(s, "tetris");
        check(t >= 0 && s.installed(t) == AppStore::Installed::No, "refresh: tetrisはまだ入っていない");

        //手で入れた(idの無い)リバーシ
        HostSd::files["/lua/apps/リバーシ/main.lua"] = "-- old\n";
        HostSd::files["/lua/apps/リバーシ/app.cfg"] = "name=リバーシ\nversion=1.0\n";
        HostSd::files["/lua/apps/リバーシ/store.json"] = "{\"score\":1}";
        AppStore s2;
        s2.loadConfig();
        check(s2.loadCachedCatalog() && s2.count() == 2, "cache: 保存した一覧を読める");
        const int r = Find(s2, "reversi");
        check(s2.installed(r) == AppStore::Installed::Foreign, "installed: idの無い同名のアプリはForeign");
        check(!s2.beginInstall(r, false) && s2.state() == AppStore::State::Failed, "install: Foreignは確認なしでは入れない");

        check(s2.beginInstall(t) && Run(s2), "install: tetris");
        check(HostSd::files["/lua/apps/テトリス/main.lua"] == ReadHostFile(tet_src + "main.lua"), "install: main.luaの中身");
        check(HostSd::files["/lua/apps/テトリス/blocks.pimg"] == ReadHostFile(tet_src + "blocks.pimg"), "install: 画像の中身");
        check(Has("/lua/apps/テトリス/bgm.mml"), "install: bgm.mml");
        check(Has("/sys/store/installed/tetris.tsv"), "install: ファイル一覧を残す");
        check(HostSd::files["/sys/store/installed/tetris.key"] == pubhex + "\n", "install: 作者の鍵を覚える");
        check(s2.entry(t)->author == "tester", "install: 作者名");
        check(!Has("/sys/store/staging/files.tsv") && !Has("/sys/store/staging/app/main.lua"), "install: 置き場は空");
        check(AppFunctions::FindByArg("/lua/apps/テトリス/main.lua") >= 0, "install: ランチャへ登録");
        check(s2.installed(t) == AppStore::Installed::Same, "install: 同じ版が入っている");
        check(s2.entry(t) && s2.progressDone() == s2.progressTotal() && s2.progressTotal() == s2.entry(t)->bytes, "install: 進み具合");

        check(s2.beginInstall(r, true) && Run(s2), "install: 確認の後ならForeignへ上書きできる");
        check(HostSd::files["/lua/apps/リバーシ/store.json"] == "{\"score\":1}", "install: アプリが書いたファイルは残る");
        check(s2.installed(r) == AppStore::Installed::Same, "install: リバーシも入った");
        check(AppFunctions::FindByArg("/lua/apps/リバーシ/main.lua") >= 0, "install: リバーシも登録");
        s2.close();
    }

    const int apps_before = AppFunctions::Count();

    // ---- 2. 信頼できない一覧は受け付けない(手元の一覧はそのまま) ----
    {
        AppStore s;
        s.loadConfig();
        Use("untrusted");
        check(s.beginRefresh() && !Run(s) && strstr(s.message(), "信頼") != nullptr, "refresh: 知らない鍵の署名は断る");
        Use("badsig");
        check(s.beginRefresh() && !Run(s) && strstr(s.message(), "署名") != nullptr, "refresh: 壊れた署名は断る");
        AppStore s2;
        s2.loadConfig();
        check(s2.loadCachedCatalog() && s2.catalogSerial() == 100, "refresh: 断ったときは前の一覧のまま");
        check(!Has("/sys/store/catalog.part"), "refresh: 半端なファイルを残さない");

        AppStore nokey; //store.cfgの鍵を読まなければ、テストの鍵は信頼しない
        nokey.setBaseUrl(url);
        Use("good");
        bool builtin = false; //焼き込みの鍵(全部0の目印は数えない)
        for(int i = 0; i < AppRepoKeys::kBuiltinCount; i++){
            builtin = builtin || AppRepo::IsBuiltinKey(AppRepoKeys::kBuiltin[i].pub);
        }
        if(builtin){
            check(nokey.beginRefresh() && !Run(nokey) && strstr(nokey.message(), "信頼") != nullptr,
                  "refresh: store.cfgの鍵が無ければ、焼き込みの鍵だけを信頼する(テストの鍵の署名は断る)");
        }else{
            check(!nokey.beginRefresh() && strstr(nokey.message(), "鍵") != nullptr, "refresh: 鍵を設定していなければ通信せずに断る");
        }
    }

    // ---- 3. 更新 ----
    {
        HostSd::files["/lua/apps/テトリス/hiscore.txt"] = "12345";
        //v1で入れたもの(作者の鍵を覚えていない)も更新でき、そのとき鍵を覚える
        HostSd::files.erase("/sys/store/installed/tetris.key");
        Use("v2");
        AppStore s;
        s.loadConfig();
        check(s.beginRefresh() && Run(s) && s.catalogSerial() == 200, "update: 新しい一覧");
        const int t = Find(s, "tetris");
        check(s.installed(t) == AppStore::Installed::Update && strcmp(s.installedVersion(t), "2.0.0") == 0, "update: 古い版が入っている");
        check(s.batchUpdatable(t) && s.batchCount() >= 1, "update: まとめて更新に入る");
        check(s.beginUpdateAll() && s.busy() && Run(s), "update: まとめて更新で入れ直せる");
        check(!s.batchRunning() && strstr(s.message(), "更新しました") != nullptr, "update: まとめて更新の結果の1行");
        check(!Has("/lua/apps/テトリス/bgm.mml"), "update: 新しい版に無いファイルは消える");
        check(HostSd::files["/lua/apps/テトリス/extra/data.txt"] == "v2 data\n", "update: 増えたファイル(サブディレクトリ)");
        check(HostSd::files["/lua/apps/テトリス/app.cfg"] == ReadHostFile(tet_v2 + "app.cfg"), "update: app.cfgも新しい");
        check(HostSd::files["/lua/apps/テトリス/hiscore.txt"] == "12345", "update: アプリが書いたファイルは残る");
        check(s.installed(t) == AppStore::Installed::Same, "update: 同じ版になった");
        check(AppFunctions::Count() == apps_before, "update: 登録は増えない(差し替え)");
        check(HostSd::files["/sys/store/installed/tetris.key"] == pubhex + "\n", "update: 鍵を覚えていなければ覚える");
        check(s.batchCount() == 0, "update: 入れたあとはまとめて更新に残らない");

        //前の版へ戻す → 見送り → 1つずつの[更新]で入れ直す
        FixedString<PICO_STR_S> prev;
        check(s.previousVersion(t, prev) && prev == "2.0.0", "prev: 前の版 2.0.0 が残っている");
        check(Has("/sys/store/prev/tetris/app/bgm.mml"), "prev: 前の版にだけあったファイルも残っている");
        check(s.rollback(t) && s.state() == AppStore::State::Done, "prev: 前の版へ戻せる");
        check(Has("/lua/apps/テトリス/bgm.mml") && !Has("/lua/apps/テトリス/extra/data.txt")
              && HostSd::files["/lua/apps/テトリス/main.lua"] == ReadHostFile(tet_src + "main.lua"), "prev: 2.0.0 のファイルに戻る");
        check(HostSd::files["/lua/apps/テトリス/hiscore.txt"] == "12345", "prev: アプリが書いたファイルは残る");
        check(s.installed(t) == AppStore::Installed::Update && s.held(t) && !s.batchUpdatable(t),
              "prev: 2.1.0 は見送り(まとめて更新に入れない)");
        check(s.beginInstall(t) && Run(s) && s.installed(t) == AppStore::Installed::Same && !s.held(t),
              "prev: 1つずつ入れれば見送りは解ける");
        s.close();
    }

    // ---- 4. 古い一覧へ戻されない ----
    {
        Use("old");
        AppStore s;
        s.loadConfig();
        check(s.beginRefresh() && !Run(s) && strstr(s.message(), "古い") != nullptr, "rollback: 古い番号の一覧は断る");
    }

    // ---- 5. 中身が一覧と合わない ----
    {
        Use("tampered");
        AppStore s;
        s.loadConfig();
        check(s.beginRefresh() && Run(s), "tampered: 一覧そのものは正しい");
        const int t = Find(s, "tetris");
        const std::string before = HostSd::files["/lua/apps/テトリス/main.lua"];
        check(s.beginInstall(t) && !Run(s) && strstr(s.message(), "main.lua") != nullptr, "tampered: 書き換えたファイルで止まる");
        check(HostSd::files["/lua/apps/テトリス/main.lua"] == before, "tampered: 入っているものは変わらない");
    }

    // ---- 6. 一覧より強い権限 ----
    {
        Use("perms");
        AppStore s;
        s.loadConfig();
        check(s.beginRefresh() && Run(s), "perms: 一覧を取れる");
        const int t = Find(s, "tetris");
        check(s.beginInstall(t) && !Run(s) && strstr(s.message(), "権限") != nullptr, "perms: 一覧に無い権限は付けない");
        check(s.installed(t) == AppStore::Installed::Update, "perms: 入っているのは前の版のまま");

        // ---- 7. 削除 ----
        check(AppFunctions::FindByArg("/lua/apps/テトリス/main.lua") >= 0, "remove: 前は登録されている");
        s.remove(t); //SDのスタブはディレクトリを辿れないので、消せたかはPCビルドで確かめる
        check(AppFunctions::FindByArg("/lua/apps/テトリス/main.lua") < 0, "remove: 登録から外れる");
        check(!Has("/sys/store/installed/tetris.tsv"), "remove: ファイル一覧も消える");
        check(!Has("/sys/store/installed/tetris.key"), "remove: 作者の鍵も忘れる");
        EraseDir("/lua/apps/テトリス/");
        s.close();
    }

    // ---- 8. 取得できない ----
    {
        AppStore s;
        s.loadConfig();
        char bad[128];
        snprintf(bad, sizeof(bad), "http://127.0.0.1:%d/nothing/", port);
        s.setBaseUrl(bad);
        check(s.beginRefresh() && !Run(s) && strstr(s.message(), "404") != nullptr, "404: 理由を出す");
        check(s.beginRefresh() && (s.cancel(), s.state() == AppStore::State::Idle), "cancel: 取りやめられる");
    }

    // ---- 9. 作者の署名 ----
    {
        Use("badfsig");
        AppStore s;
        s.loadConfig();
        check(s.beginRefresh() && Run(s), "files.sig: 一覧を取れる");
        const int t = Find(s, "tetris");
        check(s.installed(t) == AppStore::Installed::No, "files.sig: tetrisは入っていない");
        check(s.beginInstall(t) && !Run(s) && strstr(s.message(), "作者の署名が合いません") != nullptr, "files.sig: 壊れた署名は断る");
        check(!Has("/lua/apps/テトリス/app.cfg") && !Has("/sys/store/installed/tetris.key"), "files.sig: 何も入らない");

        Use("wrongauth");
        check(s.beginRefresh() && Run(s), "wrongauth: 一覧を取れる");
        check(s.beginInstall(t) && !Run(s) && strstr(s.message(), "鍵が一覧と合いません") != nullptr,
              "wrongauth: 一覧の author_key と違う鍵の署名は断る");
        s.close();
    }

    // ---- 10. 作者の鍵が変わった ----
    {
        Use("rekey");
        AppStore s;
        s.loadConfig();
        check(s.beginRefresh() && Run(s), "rekey: 一覧を取れる");
        const int r = Find(s, "reversi");
        check(s.installed(r) == AppStore::Installed::KeyChanged, "rekey: 入れたときと鍵が違えばKeyChanged");
        check(!s.beginInstall(r) && strstr(s.message(), "作者の鍵が変わりました") != nullptr, "rekey: 更新できない");
        check(HostSd::files["/lua/apps/リバーシ/app.cfg"].find("2.1.0") == std::string::npos, "rekey: 入っているのは前の版のまま");
        s.remove(r);
        check(!Has("/sys/store/installed/reversi.key"), "rekey: 削除すると覚えた鍵も消える");
        EraseDir("/lua/apps/リバーシ/");
        check(s.beginInstall(r) && Run(s) && s.installed(r) == AppStore::Installed::Same, "rekey: 削除の後なら新しい鍵で入る");
        const std::string other = HostSd::files["/sys/store/installed/reversi.key"];
        check(other.size() == 65 && other != pubhex + "\n", "rekey: 新しい鍵を覚える");

        // ---- 11. 古い版へ戻さない ----
        Use("downgrade");
        check(s.beginRefresh() && Run(s), "downgrade: 一覧を取れる");
        check(s.installed(r) == AppStore::Installed::Newer, "downgrade: 入っている方が新しい");
        check(!s.beginInstall(r) && strstr(s.message(), "古い版") != nullptr, "downgrade: 古い版は入れない");
        check(HostSd::files["/lua/apps/リバーシ/app.cfg"].find("2.1.0") != std::string::npos, "downgrade: 2.1.0のまま");

        // ---- 12. 取り下げ・危険 ----
        Use("yanked");
        check(s.beginRefresh() && Run(s), "yanked: 一覧を取れる");
        const int t = Find(s, "tetris");
        check(t >= 0 && s.entry(t)->status == AppRepo::Status::Yanked && !s.installable(t), "yanked: 入れられない印");
        check(!s.beginInstall(t) && strstr(s.message(), "取り下げ") != nullptr, "yanked: 入れない");
        check(s.installed(r) == AppStore::Installed::Same && s.blockedInstalled(r), "blocked: 入っている危険なアプリが分かる");
        check(!s.blockedInstalled(t), "blocked: 入っていなければ数えない");
        s.close();
    }

    // ---- 13. serial はカタログ鍵ごと ----
    {
        AppStore s;
        s.loadConfig();
        Use("newcat");
        check(s.beginRefresh() && Run(s) && s.catalogSerial() == 10, "serial: 別の鍵の一覧は番号を数え直す");
        Use("newcatold");
        check(s.beginRefresh() && !Run(s) && strstr(s.message(), "古い") != nullptr, "serial: 同じ鍵で古い番号は断る");
        s.close();
    }

    AppFunctions::Clear();
    printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
