#include "apps/store/App_Install.hpp"

#include <cstring>
#include <strings.h>

#include "OS_Data.hpp"
#include "system/Log_Functions.hpp"
#include "lua/LuaAppScanner.hpp"
#include "storage/SD_IO.hpp"
#include "storage/SD_Path.hpp"

namespace {
    using namespace PICO_Path::FILE;

    bool InstalledListPath(FixedString<PICO_PATH_LEN>& out, const char* id){
        return out.assign(STORE_INSTALLED_DIR) && out.append("/") && out.append(id) && out.append(".tsv");
    }

    bool PinnedKeyPath(FixedString<PICO_PATH_LEN>& out, const char* id){
        return out.assign(STORE_INSTALLED_DIR) && out.append("/") && out.append(id) && out.append(".key");
    }

    // files.tsv の一覧に rel(大小を区別しない)があるか。w.line は呼び出し側が使っているので w.aux で読む
    bool ListHas(const char* list_path, const char* rel, AppInstall::Work& w){
        FsFile f = OSData::SD.open(list_path, O_RDONLY);
        if(!f) return false;
        bool found = false;
        while(!found && f.fgets(w.aux, sizeof(w.aux)) > 0){
            char* tab = strchr(w.aux, '\t');
            if(tab) *tab = '\0';
            found = strcasecmp(w.aux, rel) == 0;
        }
        f.close();
        return found;
    }
}

bool AppInstall::MakeParent(const char* path){
    FixedString<PICO_PATH_LEN> parent;
    if(!PICO_IO::parent(parent, path)) return false;
    if(parent.empty() || parent == "/") return true;
    if(OSData::SD.exists(parent.c_str())) return true;
    return OSData::SD.mkdir(parent.c_str());
}

bool AppInstall::JoinRel(FixedString<PICO_PATH_LEN>& out, const char* dir, const char* rel){
    return out.assign(dir) && out.append("/") && out.append(rel);
}

bool AppInstall::AppDir(const AppRepo::Entry& e, FixedString<PICO_PATH_LEN>& out){
    return out.assign("/lua/apps/") && out.append(e.dir.c_str());
}

AppInstall::Installed AppInstall::Check(const AppRepo::Entry& e, FixedString<PICO_STR_S>* version){
    if(version) version->clear();
    FixedString<PICO_PATH_LEN> dir, main_lua, cfg_path;
    if(!AppDir(e, dir) || !PICO_IO::join(main_lua, dir, "main.lua")) return Installed::No;
    if(!OSData::SD.exists(main_lua.c_str())) return Installed::No;
    LuaAppScanner::AppInfo info;
    if(PICO_IO::join(cfg_path, dir, "app.cfg")) LuaAppScanner::ReadAppConfig(cfg_path.c_str(), info);
    if(version) version->assign(info.version);
    if(info.id.empty()) return Installed::Foreign;
    if(!(info.id == e.id.c_str())) return Installed::Conflict;
    //入れたときの作者の鍵と違えば更新させない(覚えていなければ=v1で入れたもの、次に入れたときに覚える)
    uint8_t pinned[32];
    if(ReadPinnedKey(e.id.c_str(), pinned) && memcmp(pinned, e.author_key, 32) != 0) return Installed::KeyChanged;
    const bool vok = AppRepo::ValidVersion(info.version.c_str());
    const int c = vok ? AppRepo::CompareVersion(info.version.c_str(), e.version.c_str()) : -1;
    return c < 0 ? Installed::Update : c == 0 ? Installed::Same : Installed::Newer;
}

bool AppInstall::ReadPinnedKey(const char* id, uint8_t out[32]){
    FixedString<PICO_PATH_LEN> path;
    if(!PinnedKeyPath(path, id) || !OSData::SD.exists(path.c_str())) return false;
    FsFile f = OSData::SD.open(path.c_str(), O_RDONLY);
    if(!f) return false;
    char hex[65] = {};
    const int n = f.read(hex, 64);
    f.close();
    return n == 64 && AppRepo::HexDecode(hex, 64, out, 32);
}

bool AppInstall::WritePinnedKey(const char* id, const uint8_t key[32]){
    FixedString<PICO_PATH_LEN> path;
    if(!PinnedKeyPath(path, id)) return false;
    if(!OSData::SD.exists(STORE_DIR)) OSData::SD.mkdir(STORE_DIR);
    if(!OSData::SD.exists(STORE_INSTALLED_DIR)) OSData::SD.mkdir(STORE_INSTALLED_DIR);
    FsFile f = OSData::SD.open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
    if(!f) return false;
    char hex[66];
    AppRepo::HexEncode(key, 32, hex);
    hex[64] = '\n';
    const bool ok = f.write(hex, 65) == 65;
    f.close();
    return ok;
}

void AppInstall::Forget(const char* id){
    FixedString<PICO_PATH_LEN> path;
    if(InstalledListPath(path, id)) OSData::SD.remove(path.c_str());
    //覚えていた作者の鍵も忘れる(入れ直せば、そのときの一覧の鍵で入る)
    if(PinnedKeyPath(path, id)) OSData::SD.remove(path.c_str());
}

bool AppInstall::ReadFileLine(const char* path, int n, Work& w){
    FsFile f = OSData::SD.open(path, O_RDONLY);
    if(!f) return false;
    int idx = 0;
    bool found = false;
    while(f.fgets(w.line, sizeof(w.line)) > 0){
        bool skip = false;
        if(AppRepo::IsSkipLine(w.line)) continue;
        if(idx++ == n){
            found = AppRepo::ParseFilesLine(w.line, w.file, skip);
            break;
        }
    }
    f.close();
    return found;
}

const char* AppInstall::CheckFilesList(const AppRepo::Entry& e, Work& w, int& count){
    FsFile f = OSData::SD.open(STORE_STAGING_FILES, O_RDONLY);
    if(!f) return "SDカードから読めません";
    char prev[AppRepo::kMaxPathLen + 1] = "";
    uint32_t total = 0;
    count = 0;
    bool has_main = false, has_cfg = false, ok = true;
    int len;
    while(ok && (len = f.fgets(w.line, sizeof(w.line))) > 0){
        if(len == (int)sizeof(w.line) - 1 && w.line[len - 1] != '\n'){ ok = false; break; }
        bool skip = false;
        if(!AppRepo::ParseFilesLine(w.line, w.file, skip)){
            if(!skip) ok = false;
            continue;
        }
        if(++count > AppRepo::kMaxFiles){ ok = false; break; }
        if(prev[0] && strcasecmp(prev, w.file.path.c_str()) >= 0){ ok = false; break; }
        strcpy(prev, w.file.path.c_str());
        total += w.file.bytes;
        if(w.file.path == "main.lua") has_main = true;
        if(w.file.path == "app.cfg") has_cfg = true;
    }
    f.close();
    if(!ok) return "ファイル一覧(files.tsv)の形式が違います";
    if(!has_main || !has_cfg) return "main.lua か app.cfg がありません";
    if(total != e.bytes || total > AppRepo::kMaxAppBytes) return "ファイルの合計が一覧と合いません";
    return nullptr;
}

const char* AppInstall::CheckStagedConfig(const AppRepo::Entry& e){
    FixedString<PICO_PATH_LEN> cfg_path;
    JoinRel(cfg_path, STORE_STAGING_APP, "app.cfg");
    LuaAppScanner::AppInfo info;
    if(!LuaAppScanner::ReadAppConfig(cfg_path.c_str(), info)) return "app.cfg を読めません";
    if(!(info.id == e.id.c_str()) || !(info.version == e.version.c_str())) return "app.cfg の id か版が一覧と合いません";
    //一覧で見せた(=利用者が承知した)権限より強い権限は付けない
    uint8_t perms = 0;
    if(info.permissions.network) perms |= AppRepo::PermNetwork;
    if(info.permissions.notify) perms |= AppRepo::PermNotify;
    if(info.permissions.sd_outside_app_dir) perms |= AppRepo::PermSd;
    if(info.permissions.background) perms |= AppRepo::PermBackground;
    if(info.permissions.microphone) perms |= AppRepo::PermMicrophone;
    if((perms & ~e.perms) != 0) return "app.cfg の権限が一覧より多くなっています";
    return nullptr;
}

const char* AppInstall::Commit(const AppRepo::Entry& e, bool allow_replace, Work& w){
    FixedString<PICO_PATH_LEN> dir;
    if(!AppDir(e, dir)) return "インストール先の名前が長すぎます";

    //確かめている間に、同じ名前のディレクトリに別のアプリが置かれていないか
    {
        FixedString<PICO_PATH_LEN> cfg_path, main_lua;
        PICO_IO::join(cfg_path, dir, "app.cfg");
        PICO_IO::join(main_lua, dir, "main.lua");
        if(OSData::SD.exists(main_lua.c_str())){
            LuaAppScanner::AppInfo cur;
            LuaAppScanner::ReadAppConfig(cfg_path.c_str(), cur);
            if(cur.id.empty() ? !allow_replace : !(cur.id == e.id.c_str())) return "同じ名前のアプリが入っています";
        }
    }

    //1つずつ移す(ディレクトリごとの移動はしない。更新では、アプリが自分で書いたファイルを残すため)。
    //途中で失敗すると新旧が混ざるが、files.tsvが残るので入れ直せば揃う
    {
        FsFile list = OSData::SD.open(STORE_STAGING_FILES, O_RDONLY);
        if(!list) return "ファイル一覧を読み直せません";
        bool ok = true;
        while(ok && list.fgets(w.line, sizeof(w.line)) > 0){
            bool skip = false;
            if(!AppRepo::ParseFilesLine(w.line, w.file, skip)) continue;
            FixedString<PICO_PATH_LEN> src, dst;
            if(!JoinRel(src, STORE_STAGING_APP, w.file.path.c_str()) || !JoinRel(dst, dir.c_str(), w.file.path.c_str())
               || !MakeParent(dst.c_str())){
                ok = false;
                break;
            }
            if(OSData::SD.exists(dst.c_str())) OSData::SD.remove(dst.c_str());
            if(!OSData::SD.rename(src.c_str(), dst.c_str())) ok = false;
        }
        list.close();
        if(!ok) return "インストール先へファイルを移せません";
    }

    //前の版にあって今の版に無いファイルを消す(前の版の一覧に載っていたものだけ。アプリが書いたファイルは残る)
    FixedString<PICO_PATH_LEN> list_path;
    InstalledListPath(list_path, e.id.c_str());
    if(OSData::SD.exists(list_path.c_str())){
        FsFile old_list = OSData::SD.open(list_path.c_str(), O_RDONLY);
        AppRepo::FileEntry old;
        while(old_list && old_list.fgets(w.line, sizeof(w.line)) > 0){
            bool skip = false;
            if(!AppRepo::ParseFilesLine(w.line, old, skip)) continue;
            FixedString<PICO_PATH_LEN> stale;
            if(!ListHas(STORE_STAGING_FILES, old.path.c_str(), w) && JoinRel(stale, dir.c_str(), old.path.c_str())){
                OSData::SD.remove(stale.c_str());
            }
        }
        if(old_list) old_list.close();
        OSData::SD.remove(list_path.c_str());
    }
    MakeParent(list_path.c_str());
    if(!OSData::SD.exists(STORE_INSTALLED_DIR)) OSData::SD.mkdir(STORE_INSTALLED_DIR);
    if(!OSData::SD.rename(STORE_STAGING_FILES, list_path.c_str())){
        LOG_SYS_WARN("AppInstall: インストールしたファイルの一覧を残せません (%s)", list_path.c_str());
    }
    //次の更新から、この作者の鍵の署名だけを受け付ける
    if(!WritePinnedKey(e.id.c_str(), e.author_key)){
        LOG_SYS_WARN("AppInstall: 作者の鍵を残せません (%s)", e.id.c_str());
    }
    PICO_IO::removeRecursive(STORE_STAGING);

    if(!LuaAppScanner::RegisterOrRefresh(dir.c_str())){
        //ファイルは入っているので、次の起動のスキャンで並ぶ(登録簿の上限に当たった等)
        LOG_SYS_WARN("AppInstall: ランチャへ登録できません (%s)", dir.c_str());
    }
    return nullptr;
}
