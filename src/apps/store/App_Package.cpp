#include "apps/store/App_Package.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

#include "OS_Data.hpp"
#include "apps/store/App_Install.hpp"
#include "apps/store/App_Repo.hpp"
#include "apps/ssh/Ssh_Sha256.hpp"
#include "devtools/CrashDump_Functions.hpp"
#include "storage/SD_IO.hpp"
#include "storage/SD_Path.hpp"
#include "system/Log_Functions.hpp"
#include "system/Notification_Functions.hpp"

namespace {
    using namespace PICO_Path::FILE;

    // 1つのパッケージを入れる間だけ new する作業場所(カタログの1件・files.tsv の行・写すときの塊。約2.5KB)
    struct Ctx {
        AppRepo::Entry entry;
        AppInstall::Work work;
        uint8_t chunk[1024];
    };

    // InstallPending() の結果(PostPendingNotice() で通知する)
    int g_ok = 0;
    int g_failed = 0;
    FixedString<PICO_STR_M> g_ok_name;     // 入れたアプリの表示名(最後のもの)
    FixedString<PICO_STR_LL> g_fail_text;  // 入れられなかったもの(最後のもの)

    bool HasExtension(const char* name){
        const size_t n = strlen(name);
        const size_t e = strlen(AppPackage::kExtension);
        return n > e && strcasecmp(name + n - e, AppPackage::kExtension) == 0;
    }

    // 1行読む(改行は除く)。長すぎる行・ファイルの終わりはfalse
    bool ReadLine(FsFile& f, char* buf, int cap){
        const int n = f.fgets(buf, cap);
        if(n <= 0 || buf[n - 1] != '\n') return false;
        buf[n - 1] = '\0';
        if(n >= 2 && buf[n - 2] == '\r') buf[n - 2] = '\0';
        return true;
    }

    // パッケージから n バイトを dst へ写し、SHA-256を取る
    bool CopyOut(FsFile& in, const char* dst, uint32_t n, uint8_t digest[32], Ctx& c){
        if(!AppInstall::MakeParent(dst)) return false;
        FsFile out = OSData::SD.open(dst, O_WRONLY | O_CREAT | O_TRUNC);
        if(!out) return false;
        Sha256 sha;
        bool ok = true;
        while(ok && n > 0){
            const size_t want = n < sizeof(c.chunk) ? n : sizeof(c.chunk);
            const int got = in.read(c.chunk, want);
            if(got != (int)want || out.write(c.chunk, want) != want){
                ok = false;
                break;
            }
            sha.update(c.chunk, want);
            n -= (uint32_t)want;
            //4MiBまで写すので、ウォッチドッグ(debug.cfg)が動いていても止まらないように
            CrashDumpFunctions::Feed();
        }
        out.close();
        sha.finish(digest);
        return ok;
    }

    const char* InstallFrom(FsFile& f, Ctx& c, FixedString<PICO_STR_LL>& message){
        AppRepo::Entry& e = c.entry;
        AppInstall::Work& w = c.work;

        //1行目: "pico-os-app<TAB>1<TAB><files.tsv のバイト数>"
        if(!ReadLine(f, w.line, sizeof(w.line))) return "パッケージではありません";
        char* tab1 = strchr(w.line, '\t');
        char* tab2 = tab1 ? strchr(tab1 + 1, '\t') : nullptr;
        if(!tab2) return "パッケージではありません";
        *tab1 = '\0';
        *tab2 = '\0';
        if(strcmp(w.line, AppPackage::kMagic) != 0) return "パッケージではありません";
        if(atoi(tab1 + 1) != AppPackage::kFormat) return "知らない形式のパッケージです(本体の更新が要ります)";
        char* end = nullptr;
        const unsigned long files_bytes = strtoul(tab2 + 1, &end, 10);
        if(end == tab2 + 1 || *end != '\0' || files_bytes == 0 || files_bytes > AppRepo::kMaxFilesBytes){
            return "パッケージの見出しが壊れています";
        }

        //2行目: catalog.tsv の1行と同じ形
        bool skip = false;
        if(!ReadLine(f, w.line, sizeof(w.line))) return "パッケージの見出しが壊れています";
        if(!AppRepo::ParseCatalogLine(w.line, e, skip)) return "パッケージのアプリの情報を読めません";
        if(e.status != AppRepo::Status::Normal){
            return e.status == AppRepo::Status::Blocked ? "危険と判断されたアプリです" : "取り下げられたアプリです";
        }

        //3行目: files.sig(作者の鍵の署名)
        uint8_t pub[32], sig[64];
        if(!ReadLine(f, w.line, sizeof(w.line)) || !AppRepo::ParseSignature(w.line, pub, sig)) return "作者の署名を読めません";
        if(memcmp(pub, e.author_key, 32) != 0) return "作者の署名の鍵がアプリの情報と合いません";

        //入っているものと比べる(ファイルを写す前に断れるものは断る)
        switch(AppInstall::Check(e)){
            case AppInstall::Installed::Conflict: return "同じ名前の別のアプリが入っています";
            case AppInstall::Installed::KeyChanged: return "作者の鍵が変わりました(アプリを削除してから置き直してください)";
            case AppInstall::Installed::Newer: return "入っている版より古い版は入れられません";
            default: break;
        }

        char m[PICO_STR_LL];
        snprintf(m, sizeof(m), "%s %s を入れています", e.name.c_str(), e.version.c_str());
        message.assign(m);
        LOG_SYS_MSG("AppPackage: %s", m);

        //files.tsv → 署名
        PICO_IO::removeRecursive(STORE_STAGING); //前回の取り残し
        if(!OSData::SD.exists(STORE_DIR)) OSData::SD.mkdir(STORE_DIR);
        uint8_t digest[32];
        if(!CopyOut(f, STORE_STAGING_FILES, (uint32_t)files_bytes, digest, c)) return "パッケージが途中で切れています";
        if(memcmp(digest, e.files_sha256, 32) != 0) return "ファイル一覧(files.tsv)がアプリの情報と合いません";
        if(!AppRepo::VerifyFiles(e.id.c_str(), e.version.c_str(), e.files_sha256, pub, sig)) return "作者の署名が合いません";
        int count = 0;
        if(const char* err = AppInstall::CheckFilesList(e, w, count)) return err;

        //各ファイル(files.tsv の順に続けて入っている)
        for(int i = 0; i < count; i++){
            if(!AppInstall::ReadFileLine(STORE_STAGING_FILES, i, w)) return "ファイル一覧を読み直せません";
            FixedString<PICO_PATH_LEN> dst;
            if(!AppInstall::JoinRel(dst, STORE_STAGING_APP, w.file.path.c_str())) return "パスが長すぎます";
            if(!CopyOut(f, dst.c_str(), w.file.bytes, digest, c)) return "パッケージが途中で切れています";
            if(memcmp(digest, w.file.sha256, 32) != 0){
                snprintf(m, sizeof(m), "%s の中身が一覧と合いません", w.file.path.c_str());
                message.assign(m);
                return message.c_str();
            }
        }
        if(f.read(c.chunk, 1) > 0) return "パッケージの後ろに余分なデータがあります";

        if(const char* err = AppInstall::CheckStagedConfig(e)) return err;
        //SDに置いた=利用者が自分で選んだので、手で入れた同じ名前のアプリ(idが無い)には上書きする
        return AppInstall::Commit(e, true, w);
    }

    void PostOne(const char* title, const char* body, const char* app){
        NotificationFunctions::Content c;
        NotificationFunctions::Sanitize(c.title, title);
        NotificationFunctions::Sanitize(c.body, body);
        if(app) NotificationFunctions::Sanitize(c.app, app);
        c.sound = false;
        NotificationFunctions::Post(c);
    }
}

bool AppPackage::Install(const char* path, FixedString<PICO_STR_LL>& message, FixedString<PICO_STR_M>* name){
    message.clear();
    if(name) name->clear();
    FsFile f = OSData::SD.open(path, O_RDONLY);
    if(!f){
        message.assign("パッケージを開けません");
        return false;
    }
    Ctx* c = new Ctx();
    const char* err = InstallFrom(f, *c, message);
    f.close();
    if(err){
        //message を指していることがある(ファイル名入りの文)ので、写してから組み立て直す
        FixedString<PICO_STR_LL> tmp;
        tmp.assign(err);
        message.assign(tmp);
        PICO_IO::removeRecursive(STORE_STAGING);
        LOG_SYS_WARN("AppPackage: %s: %s", path, message.c_str());
    }else{
        char m[PICO_STR_LL];
        snprintf(m, sizeof(m), "%s %s を入れました", c->entry.name.c_str(), c->entry.version.c_str());
        message.assign(m);
        if(name) name->assign(c->entry.name);
        LOG_SYS_OK("AppPackage: %s", m);
    }
    delete c;
    return err == nullptr;
}

int AppPackage::InstallPending(){
    if(!OSData::SD_usable) return 0;
    //走査しながら消す・改名するとFATの走査がずれるので、先に名前だけ集める
    FixedString<PICO_STR_L> names[kMaxPerRun];
    int n = 0;
    {
        FsFile dir = OSData::SD.open("/", O_RDONLY);
        if(!dir) return 0;
        FsFile entry;
        char name[PICO_STR_L];
        while(n < kMaxPerRun && entry.openNext(&dir, O_RDONLY)){
            if(!entry.isDir() && entry.getName(name, sizeof(name)) && name[0] != '.' && HasExtension(name)){
                names[n++].assign(name);
            }
            entry.close();
        }
        dir.close();
    }
    int installed = 0;
    for(int i = 0; i < n; i++){
        FixedString<PICO_PATH_LEN> path;
        if(!PICO_IO::join(path, "/", names[i].c_str())) continue;
        FixedString<PICO_STR_LL> message;
        FixedString<PICO_STR_M> app_name;
        if(Install(path.c_str(), message, &app_name)){
            OSData::SD.remove(path.c_str());
            installed++;
            g_ok++;
            g_ok_name.assign(app_name);
        }else{
            //同じ失敗を起動のたびに繰り返さないように改名して残す(置き直せばまた試す)
            FixedString<PICO_PATH_LEN> err_path;
            if(err_path.assign(path) && err_path.append(".err")){
                if(OSData::SD.exists(err_path.c_str())) OSData::SD.remove(err_path.c_str());
                OSData::SD.rename(path.c_str(), err_path.c_str());
            }
            g_failed++;
            char t[PICO_STR_LL];
            snprintf(t, sizeof(t), "%s: %s", names[i].c_str(), message.c_str());
            g_fail_text.assign(t);
        }
    }
    return installed;
}

void AppPackage::PostPendingNotice(){
    if(g_ok > 0){
        char body[PICO_STR_L];
        if(g_ok == 1) snprintf(body, sizeof(body), "%s", g_ok_name.c_str());
        else snprintf(body, sizeof(body), "%s ほか%d件", g_ok_name.c_str(), g_ok - 1);
        //1件ならタップでそのアプリを開く(登録簿の名前は app.cfg の name と同じ)
        PostOne("アプリを入れました", body, g_ok == 1 ? g_ok_name.c_str() : nullptr);
    }
    if(g_failed > 0){
        PostOne(g_failed == 1 ? "アプリを入れられません" : "入れられないアプリがあります", g_fail_text.c_str(), nullptr);
    }
    g_ok = 0;
    g_failed = 0;
    g_ok_name.clear();
    g_fail_text.clear();
}
