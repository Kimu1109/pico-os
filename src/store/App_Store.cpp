#include "store/App_Store.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

#include "Arduino.h"
#include "OS_Data.hpp"
#include "functions/Config_Functions.hpp"
#include "functions/App_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "lua/LuaAppScanner.hpp"
#include "storage/SD_IO.hpp"
#include "storage/SD_Path.hpp"
#include "store/App_Repo_Keys.hpp"

namespace {
    using namespace PICO_Path::FILE;
    using namespace PICO_Path::FILE::CFG;

    // 1回のupdate()でHTTPを進める回数と時間の上限(1回で最大1KB読む)
    constexpr int kPumpPerUpdate = 8;
    constexpr unsigned long kPumpBudgetMs = 6;

    // path の親ディレクトリを作る(実機のSdFat::mkdirは親も作る)
    bool MakeParent(const char* path){
        FixedString<PICO_PATH_LEN> parent;
        if(!PICO_IO::parent(parent, path)) return false;
        if(parent.empty() || parent == "/") return true;
        if(OSData::SD.exists(parent.c_str())) return true;
        return OSData::SD.mkdir(parent.c_str());
    }

    bool JoinRel(FixedString<PICO_PATH_LEN>& out, const char* dir, const char* rel){
        return out.assign(dir) && out.append("/") && out.append(rel);
    }

    bool InstalledListPath(FixedString<PICO_PATH_LEN>& out, const char* id){
        return out.assign(STORE_INSTALLED_DIR) && out.append("/") && out.append(id) && out.append(".tsv");
    }

    void SetMsg(FixedString<PICO_STR_LL>& m, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
    void SetMsg(FixedString<PICO_STR_LL>& m, const char* fmt, ...){
        char buf[PICO_STR_LL];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        m.assign(buf); //長ければ切れるだけ(UTF-8の境目はFixedStringが守る)
    }
}

// ---------------------------------------------------------------------------
// 受け取り口
// ---------------------------------------------------------------------------

bool AppStore::FileSink::open(const char* path, uint32_t limit){
    close();
    sha_.reset();
    bytes_ = 0;
    limit_ = limit;
    overflow_ = false;
    if(!MakeParent(path)) return false;
    file_ = OSData::SD.open(path, O_WRONLY | O_CREAT | O_TRUNC);
    return (bool)file_;
}

void AppStore::FileSink::close(){
    if(file_) file_.close();
}

bool AppStore::FileSink::write(const void* data, size_t len){
    if(!file_) return false;
    if(bytes_ + len > limit_){
        overflow_ = true;
        return false;
    }
    if(file_.write(data, len) != len) return false;
    sha_.update(data, len);
    bytes_ += (uint32_t)len;
    return true;
}

bool AppStore::MemSink::write(const void* data, size_t len){
    if(len_ + len > AppRepo::kMaxSigBytes){
        overflow_ = true;
        return false;
    }
    memcpy(buf_ + len_, data, len);
    len_ += len;
    buf_[len_] = '\0';
    return true;
}

// ---------------------------------------------------------------------------
// 設定・カタログ
// ---------------------------------------------------------------------------

AppStore::AppStore(){
    base_url_.assign(kDefaultUrl);
    http_.setKeepAlive(true);
}

AppStore::~AppStore(){
    cancel();
    close();
}

bool AppStore::setBaseUrl(const char* url){
    Url u;
    if(!url || !UrlTools::Parse(u, url)) return false;
    FixedString<PICO_STR_L> next;
    if(!next.assign(url)) return false;
    if(next.length() == 0 || next.c_str()[next.length() - 1] != '/'){
        if(!next.append("/")) return false;
    }
    base_url_.assign(next);
    return true;
}

bool AppStore::addTrustedKey(const uint8_t pub[32]){
    if(key_count_ >= kMaxExtraKeys) return false;
    memcpy(keys_[key_count_++], pub, 32);
    return true;
}

void AppStore::loadConfig(){
    if(!OSData::SD_usable || !OSData::SD.exists(SYS_STORE_CFG)) return;
    PICO_Config::ParseFile(SYS_STORE_CFG, [&](const char* key, const char* value){
        if(strcmp(key, "url") == 0){
            if(!setBaseUrl(value)) LOG_SYS_WARN("AppStore: store.cfgのurlが読めません (%s)", value);
        }else if(strcmp(key, "key") == 0){
            uint8_t pub[32];
            if(AppRepo::HexDecode(value, strlen(value), pub, 32)){
                if(!addTrustedKey(pub)) LOG_SYS_WARN("AppStore: store.cfgのkeyは%d個までです", kMaxExtraKeys);
            }else{
                LOG_SYS_WARN("AppStore: store.cfgのkeyが16進64桁ではありません");
            }
        }
    });
}

bool AppStore::trustedKey(const uint8_t pub[32]) const {
    if(AppRepo::IsBuiltinKey(pub)) return true;
    for(int i = 0; i < key_count_; i++){
        if(memcmp(keys_[i], pub, 32) == 0) return true;
    }
    return false;
}

bool AppStore::parseCatalogFile(const char* path, uint32_t& serial, bool fill){
    FsFile f = OSData::SD.open(path, O_RDONLY);
    if(!f) return false;
    bool ok = false;
    int n = f.fgets(line_, sizeof(line_));
    if(n > 0 && AppRepo::ParseCatalogHeader(line_, serial)){
        ok = true;
        if(fill) count_ = 0;
        int lineno = 1;
        while((n = f.fgets(line_, sizeof(line_))) > 0){
            lineno++;
            //1行が長すぎる(途中で切れた)行は、残りごと読み捨てる
            if(n == (int)sizeof(line_) - 1 && line_[n - 1] != '\n'){
                LOG_SYS_WARN("AppStore: カタログの%d行目が長すぎます", lineno);
                while((n = f.fgets(line_, sizeof(line_))) > 0 && line_[n - 1] != '\n'){}
                continue;
            }
            if(!fill) continue;
            if(count_ >= AppRepo::kMaxEntries){
                LOG_SYS_WARN("AppStore: カタログのアプリは%d件まで読みます", AppRepo::kMaxEntries);
                break;
            }
            bool skip = false;
            if(AppRepo::ParseCatalogLine(line_, entries_[count_], skip)){
                count_++;
            }else if(!skip){
                //知らない形の行は読み飛ばす(新しい本体向けの行を古い本体が誤読しないため、項目の検査は厳しい)
                LOG_SYS_WARN("AppStore: カタログの%d行目を読めません", lineno);
            }
        }
    }
    f.close();
    return ok;
}

bool AppStore::loadCachedCatalog(){
    if(!OSData::SD_usable || !OSData::SD.exists(STORE_CATALOG)) return false;
    //別のURLで取ったカタログは使わない(取得先を変えた直後に古い一覧から入れようとしないため)
    FixedString<PICO_STR_L> url;
    uint32_t saved_serial = 0;
    if(!OSData::SD.exists(STORE_STATE)) return false;
    PICO_Config::ParseFile(STORE_STATE, [&](const char* key, const char* value){
        if(strcmp(key, "url") == 0) url.assign(value);
        else if(strcmp(key, "serial") == 0) saved_serial = (uint32_t)strtoul(value, nullptr, 10);
    });
    if(!(url == base_url_.c_str())) return false;
    uint32_t serial = 0;
    if(!parseCatalogFile(STORE_CATALOG, serial, true)){
        count_ = 0;
        return false;
    }
    serial_ = serial > saved_serial ? serial : saved_serial;
    refreshInstalled();
    revision_++;
    return true;
}

const AppRepo::Entry* AppStore::entry(int index) const {
    return (index >= 0 && index < count_) ? &entries_[index] : nullptr;
}

AppStore::Installed AppStore::installed(int index) const {
    return (index >= 0 && index < count_) ? installed_[index] : Installed::No;
}

const char* AppStore::installedVersion(int index) const {
    return (index >= 0 && index < count_) ? installed_ver_[index].c_str() : "";
}

bool AppStore::appDir(int index, FixedString<PICO_PATH_LEN>& out) const {
    const AppRepo::Entry* e = entry(index);
    return e && out.assign("/lua/apps/") && out.append(e->dir.c_str());
}

void AppStore::refreshInstalled(){
    for(int i = 0; i < count_; i++){
        installed_[i] = Installed::No;
        installed_ver_[i].clear();
        FixedString<PICO_PATH_LEN> dir, main_lua, cfg_path;
        if(!appDir(i, dir) || !PICO_IO::join(main_lua, dir, "main.lua")) continue;
        if(!OSData::SD.exists(main_lua.c_str())) continue;
        LuaAppScanner::AppInfo info;
        if(PICO_IO::join(cfg_path, dir, "app.cfg")) LuaAppScanner::ReadAppConfig(cfg_path.c_str(), info);
        installed_ver_[i].assign(info.version);
        if(info.id.empty()){
            installed_[i] = Installed::Foreign;
        }else if(!(info.id == entries_[i].id.c_str())){
            installed_[i] = Installed::Conflict;
        }else{
            const bool vok = AppRepo::ValidVersion(info.version.c_str());
            const int c = vok ? AppRepo::CompareVersion(info.version.c_str(), entries_[i].version.c_str()) : -1;
            installed_[i] = c < 0 ? Installed::Update : c == 0 ? Installed::Same : Installed::Newer;
        }
    }
}

bool AppStore::launch(int index){
    FixedString<PICO_PATH_LEN> dir, main_lua;
    if(!appDir(index, dir) || !PICO_IO::join(main_lua, dir, "main.lua")) return false;
    const int app = AppFunctions::FindByArg(main_lua.c_str());
    if(app < 0) return false;
    AppFunctions::Launch(app);
    return true;
}

// ---------------------------------------------------------------------------
// 通信
// ---------------------------------------------------------------------------

void AppStore::fail(const char* msg){
    file_sink_.close();
    http_.cancel(); //失敗した接続は使い回さない
    step_ = Step::None;
    state_ = State::Failed;
    message_.assign(msg);
    LOG_SYS_WARN("AppStore: %s", msg);
    if(target_ >= 0) PICO_IO::removeRecursive(STORE_STAGING);
    target_ = -1;
    OSData::SD.remove(STORE_CATALOG_PART);
}

void AppStore::finishOk(const char* msg){
    step_ = Step::None;
    state_ = State::Done;
    target_ = -1;
    message_.assign(msg);
    LOG_SYS_OK("AppStore: %s", msg);
}

bool AppStore::startGet(const char* rel_url){
    FixedString<PICO_STR_256B> full;
    Url url;
    if(!full.assign(base_url_.c_str()) || !full.append(rel_url) || !UrlTools::Parse(url, full.c_str())){
        fail("URLが長すぎます");
        return false;
    }
    //200の本文だけを書く(転送(3xx)やエラーの本文をSHA-256に混ぜない)
    gate_.attach(&http_.response(), step_ == Step::Signature ? (IHttpSink*)&mem_sink_ : (IHttpSink*)&file_sink_);
    if(!http_.begin(url, HttpRequest::Method::GET, &gate_)){
        fail("取得を始められません");
        return false;
    }
    return true;
}

bool AppStore::beginRefresh(){
    if(busy()) return false;
    if(!OSData::SD_usable){
        state_ = State::Failed;
        message_.assign("SDカードがありません");
        return false;
    }
    bool any_key = key_count_ > 0;
    for(int i = 0; i < AppRepoKeys::kBuiltinCount && !any_key; i++){
        any_key = AppRepo::IsBuiltinKey(AppRepoKeys::kBuiltin[i].pub);
    }
    if(!any_key){
        state_ = State::Failed;
        message_.assign("信頼する鍵がありません(/sys/store.cfg の key)");
        return false;
    }
    state_ = State::Busy;
    target_ = -1;
    progress_done_ = 0;
    progress_total_ = 0;
    message_.assign("一覧を取得中…");
    if(!OSData::SD.exists(STORE_DIR)) OSData::SD.mkdir(STORE_DIR);
    if(!file_sink_.open(STORE_CATALOG_PART, (uint32_t)AppRepo::kMaxCatalogBytes)){
        fail("SDカードに書き込めません");
        return false;
    }
    step_ = Step::Catalog;
    return startGet("catalog.tsv");
}

void AppStore::cancel(){
    if(!busy()) return;
    file_sink_.close();
    http_.cancel();
    if(target_ >= 0) PICO_IO::removeRecursive(STORE_STAGING);
    OSData::SD.remove(STORE_CATALOG_PART);
    step_ = Step::None;
    target_ = -1;
    state_ = State::Idle;
    message_.assign("取りやめました");
}

void AppStore::close(){
    http_.closeConnection();
}

void AppStore::update(){
    if(state_ != State::Busy || step_ == Step::None) return;
    const unsigned long start = millis();
    for(int i = 0; i < kPumpPerUpdate; i++){
        http_.update();
        if(http_.getStatus() != TaskTools::PROCESSING) break;
        if(millis() - start >= kPumpBudgetMs) break;
    }
    if(http_.getStatus() == TaskTools::PROCESSING) return;

    file_sink_.close();
    if(http_.getStatus() != TaskTools::SUCCESS){
        if(file_sink_.overflowed()){
            if(step_ == Step::File){
                char m[PICO_STR_LL];
                snprintf(m, sizeof(m), "%s の中身が一覧と合いません(大きすぎます)", file_.path.c_str());
                fail(m);
            }else{
                fail("配布元のファイルが大きすぎます");
            }
            return;
        }
        FixedString<PICO_STR_LL> m;
        m.assign("通信に失敗しました: ");
        m.append(http_.failureToStr());
        fail(m.c_str());
        return;
    }
    const int code = http_.response().statusCode();
    if(code != 200){
        char m[64];
        snprintf(m, sizeof(m), "配布元が %d を返しました", code);
        fail(m);
        return;
    }
    switch(step_){
        case Step::Catalog:   onCatalog(); break;
        case Step::Signature: onSignature(); break;
        case Step::Files:     onFiles(); break;
        case Step::File:      onFile(); break;
        default: break;
    }
}

void AppStore::onCatalog(){
    step_ = Step::Signature;
    mem_sink_.reset();
    startGet("catalog.sig");
}

void AppStore::onSignature(){
    uint8_t digest[32], pub[32], sig[64];
    file_sink_.digest(digest);
    if(!AppRepo::ParseSignature(mem_sink_.text(), pub, sig)){
        fail("署名(catalog.sig)を読めません");
        return;
    }
    if(!trustedKey(pub)){
        fail("署名の鍵が信頼されていません(store.cfgのkeyを確かめてください)");
        return;
    }
    if(!AppRepo::VerifyCatalog(digest, pub, sig)){
        fail("一覧の署名が合いません(改ざん・取得の途中で更新された可能性)");
        return;
    }
    uint32_t serial = 0;
    if(!parseCatalogFile(STORE_CATALOG_PART, serial, false)){
        fail("一覧(catalog.tsv)の形式が違います");
        return;
    }
    //同じURLで前に見た番号より古い一覧は受け付けない(古い署名済みの一覧へ戻されるのを防ぐ)
    FixedString<PICO_STR_L> url;
    uint32_t saved = 0;
    if(OSData::SD.exists(STORE_STATE)){
        PICO_Config::ParseFile(STORE_STATE, [&](const char* key, const char* value){
            if(strcmp(key, "url") == 0) url.assign(value);
            else if(strcmp(key, "serial") == 0) saved = (uint32_t)strtoul(value, nullptr, 10);
        });
    }
    const bool same_url = url == base_url_.c_str();
    if(same_url && serial < saved){
        fail("前に取得したものより古い一覧が返されました");
        return;
    }
    if(OSData::SD.exists(STORE_CATALOG)) OSData::SD.remove(STORE_CATALOG);
    if(!OSData::SD.rename(STORE_CATALOG_PART, STORE_CATALOG)){
        fail("一覧を保存できません");
        return;
    }
    char num[16];
    snprintf(num, sizeof(num), "%lu", (unsigned long)serial);
    PICO_Config::SetValue(STORE_STATE, "url", base_url_.c_str());
    PICO_Config::SetValue(STORE_STATE, "serial", num);
    serial_ = serial;
    parseCatalogFile(STORE_CATALOG, serial, true);
    refreshInstalled();
    revision_++;
    char m[64];
    snprintf(m, sizeof(m), "%d件のアプリ", count_);
    finishOk(m);
}

// ---------------------------------------------------------------------------
// インストール
// ---------------------------------------------------------------------------

bool AppStore::beginInstall(int index, bool allow_replace){
    if(busy()) return false;
    const AppRepo::Entry* e = entry(index);
    if(!e) return false;
    const Installed st = installed_[index];
    if(st == Installed::Conflict){
        state_ = State::Failed;
        message_.assign("同じ名前の別のアプリが入っています");
        return false;
    }
    if(st == Installed::Foreign && !allow_replace){
        state_ = State::Failed;
        message_.assign("同じ名前のアプリが入っています(上書きの確認が要ります)");
        return false;
    }
    if(!OSData::SD_usable){
        state_ = State::Failed;
        message_.assign("SDカードがありません");
        return false;
    }
    target_ = index;
    allow_replace_ = allow_replace;
    state_ = State::Busy;
    progress_done_ = 0;
    progress_total_ = e->bytes;
    file_index_ = 0;
    file_count_ = 0;
    SetMsg(message_, "%s を取得中…", e->name.c_str());

    PICO_IO::removeRecursive(STORE_STAGING); //前回の取り残し
    if(!file_sink_.open(STORE_STAGING_FILES, (uint32_t)AppRepo::kMaxFilesBytes)){
        fail("SDカードに書き込めません");
        return false;
    }
    step_ = Step::Files;
    FixedString<PICO_STR_L> rel;
    rel.assign(e->id.c_str());
    rel.append("/");
    rel.append(e->version.c_str());
    rel.append("/files.tsv");
    return startGet(rel.c_str());
}

bool AppStore::listHas(const char* list_path, const char* rel){
    //line_は呼び出し側が使っているので、こちらは別の作業場所で読む(files.tsvの1行は短い)
    FsFile f = OSData::SD.open(list_path, O_RDONLY);
    if(!f) return false;
    bool found = false;
    while(!found && f.fgets(aux_, sizeof(aux_)) > 0){
        char* tab = strchr(aux_, '\t');
        if(tab) *tab = '\0';
        found = strcasecmp(aux_, rel) == 0;
    }
    f.close();
    return found;
}

bool AppStore::readFileLine(const char* path, int n, AppRepo::FileEntry& out){
    FsFile f = OSData::SD.open(path, O_RDONLY);
    if(!f) return false;
    int idx = 0;
    bool found = false;
    while(f.fgets(line_, sizeof(line_)) > 0){
        bool skip = false;
        if(AppRepo::IsSkipLine(line_)) continue;
        if(idx++ == n){
            found = AppRepo::ParseFilesLine(line_, out, skip);
            break;
        }
    }
    f.close();
    return found;
}

void AppStore::onFiles(){
    const AppRepo::Entry& e = entries_[target_];
    uint8_t digest[32];
    file_sink_.digest(digest);
    if(memcmp(digest, e.files_sha256, 32) != 0){
        fail("ファイル一覧(files.tsv)が一覧の署名と合いません");
        return;
    }
    //全部の行を先に確かめる: 形式・パスの並び(大小を区別せず昇順=重なり無し)・数・合計・必須のファイル
    FsFile f = OSData::SD.open(STORE_STAGING_FILES, O_RDONLY);
    if(!f){
        fail("SDカードから読めません");
        return;
    }
    char prev[AppRepo::kMaxPathLen + 1] = "";
    uint32_t total = 0;
    int count = 0;
    bool has_main = false, has_cfg = false, ok = true;
    int len;
    while(ok && (len = f.fgets(line_, sizeof(line_))) > 0){
        if(len == (int)sizeof(line_) - 1 && line_[len - 1] != '\n'){ ok = false; break; }
        bool skip = false;
        if(!AppRepo::ParseFilesLine(line_, file_, skip)){
            if(!skip) ok = false;
            continue;
        }
        if(++count > AppRepo::kMaxFiles){ ok = false; break; }
        if(prev[0] && strcasecmp(prev, file_.path.c_str()) >= 0){ ok = false; break; }
        strcpy(prev, file_.path.c_str());
        total += file_.bytes;
        if(file_.path == "main.lua") has_main = true;
        if(file_.path == "app.cfg") has_cfg = true;
    }
    f.close();
    if(!ok){
        fail("ファイル一覧(files.tsv)の形式が違います");
        return;
    }
    if(!has_main || !has_cfg){
        fail("main.lua か app.cfg がありません");
        return;
    }
    if(total != e.bytes || total > AppRepo::kMaxAppBytes){
        fail("ファイルの合計が一覧と合いません");
        return;
    }
    file_count_ = count;
    file_index_ = 0;
    progress_done_ = 0;
    startNextFile();
}

bool AppStore::startNextFile(){
    const AppRepo::Entry& e = entries_[target_];
    if(file_index_ >= file_count_){
        if(!checkStagedConfig()) return false;
        if(!commitInstall()) return false;
        return true;
    }
    if(!readFileLine(STORE_STAGING_FILES, file_index_, file_)){
        fail("ファイル一覧を読み直せません");
        return false;
    }
    FixedString<PICO_PATH_LEN> dst;
    if(!JoinRel(dst, STORE_STAGING_APP, file_.path.c_str()) || !file_sink_.open(dst.c_str(), file_.bytes)){
        fail("SDカードに書き込めません");
        return false;
    }
    SetMsg(message_, "%s を取得中 (%d/%d)", e.name.c_str(), file_index_ + 1, file_count_);
    step_ = Step::File;
    FixedString<PICO_STR_256B> rel;
    rel.assign(e.id.c_str());
    rel.append("/");
    rel.append(e.version.c_str());
    rel.append("/");
    if(!rel.append(file_.path.c_str())){
        fail("URLが長すぎます");
        return false;
    }
    return startGet(rel.c_str());
}

void AppStore::onFile(){
    uint8_t digest[32];
    file_sink_.digest(digest);
    if(file_sink_.bytes() != file_.bytes || memcmp(digest, file_.sha256, 32) != 0){
        char m[PICO_STR_LL];
        snprintf(m, sizeof(m), "%s の中身が一覧と合いません", file_.path.c_str());
        fail(m);
        return;
    }
    progress_done_ += file_.bytes;
    file_index_++;
    startNextFile();
}

bool AppStore::checkStagedConfig(){
    const AppRepo::Entry& e = entries_[target_];
    FixedString<PICO_PATH_LEN> cfg_path;
    JoinRel(cfg_path, STORE_STAGING_APP, "app.cfg");
    LuaAppScanner::AppInfo info;
    if(!LuaAppScanner::ReadAppConfig(cfg_path.c_str(), info)){
        fail("app.cfg を読めません");
        return false;
    }
    if(!(info.id == e.id.c_str()) || !(info.version == e.version.c_str())){
        fail("app.cfg の id か版が一覧と合いません");
        return false;
    }
    //一覧で見せた(=利用者が承知した)権限より強い権限は付けない
    uint8_t perms = 0;
    if(info.permissions.network) perms |= AppRepo::PermNetwork;
    if(info.permissions.notify) perms |= AppRepo::PermNotify;
    if(info.permissions.sd_outside_app_dir) perms |= AppRepo::PermSd;
    if(info.permissions.background) perms |= AppRepo::PermBackground;
    if((perms & ~e.perms) != 0){
        fail("app.cfg の権限が一覧より多くなっています");
        return false;
    }
    return true;
}

bool AppStore::commitInstall(){
    const AppRepo::Entry& e = entries_[target_];
    FixedString<PICO_PATH_LEN> dir;
    appDir(target_, dir);

    //インストールを始めた後で、同じ名前のディレクトリに別のアプリが置かれていないか
    {
        FixedString<PICO_PATH_LEN> cfg_path, main_lua;
        PICO_IO::join(cfg_path, dir, "app.cfg");
        PICO_IO::join(main_lua, dir, "main.lua");
        if(OSData::SD.exists(main_lua.c_str())){
            LuaAppScanner::AppInfo cur;
            LuaAppScanner::ReadAppConfig(cfg_path.c_str(), cur);
            if(cur.id.empty() ? !allow_replace_ : !(cur.id == e.id.c_str())){
                fail("同じ名前のアプリが入っています");
                return false;
            }
        }
    }

    message_.assign("インストール中…");
    //1つずつ移す(ディレクトリごとの移動はしない。更新では、アプリが自分で書いたファイルを残すため)。
    //途中で失敗すると新旧が混ざるが、files.tsvが残るので入れ直せば揃う
    {
        FsFile list = OSData::SD.open(STORE_STAGING_FILES, O_RDONLY);
        if(!list){
            fail("ファイル一覧を読み直せません");
            return false;
        }
        bool ok = true;
        while(ok && list.fgets(line_, sizeof(line_)) > 0){
            bool skip = false;
            if(!AppRepo::ParseFilesLine(line_, file_, skip)) continue;
            FixedString<PICO_PATH_LEN> src, dst;
            if(!JoinRel(src, STORE_STAGING_APP, file_.path.c_str()) || !JoinRel(dst, dir.c_str(), file_.path.c_str())
               || !MakeParent(dst.c_str())){
                ok = false;
                break;
            }
            if(OSData::SD.exists(dst.c_str())) OSData::SD.remove(dst.c_str());
            if(!OSData::SD.rename(src.c_str(), dst.c_str())) ok = false;
        }
        list.close();
        if(!ok){
            fail("インストール先へファイルを移せません");
            return false;
        }
    }

    //前の版にあって今の版に無いファイルを消す(前の版の一覧に載っていたものだけ。アプリが書いたファイルは残る)
    FixedString<PICO_PATH_LEN> list_path;
    InstalledListPath(list_path, e.id.c_str());
    if(OSData::SD.exists(list_path.c_str())){
        FsFile old_list = OSData::SD.open(list_path.c_str(), O_RDONLY);
        AppRepo::FileEntry old;
        while(old_list && old_list.fgets(line_, sizeof(line_)) > 0){
            bool skip = false;
            if(!AppRepo::ParseFilesLine(line_, old, skip)) continue;
            FixedString<PICO_PATH_LEN> stale;
            if(!listHas(STORE_STAGING_FILES, old.path.c_str()) && JoinRel(stale, dir.c_str(), old.path.c_str())){
                OSData::SD.remove(stale.c_str());
            }
        }
        if(old_list) old_list.close();
        OSData::SD.remove(list_path.c_str());
    }
    MakeParent(list_path.c_str());
    if(!OSData::SD.exists(STORE_INSTALLED_DIR)) OSData::SD.mkdir(STORE_INSTALLED_DIR);
    if(!OSData::SD.rename(STORE_STAGING_FILES, list_path.c_str())){
        LOG_SYS_WARN("AppStore: インストールしたファイルの一覧を残せません (%s)", list_path.c_str());
    }
    PICO_IO::removeRecursive(STORE_STAGING);

    if(!LuaAppScanner::RegisterOrRefresh(dir.c_str())){
        //ファイルは入っているので、次の起動のスキャンで並ぶ(登録簿の上限に当たった等)
        LOG_SYS_WARN("AppStore: ランチャへ登録できません (%s)", dir.c_str());
    }
    refreshInstalled();
    revision_++;
    char m[PICO_STR_LL];
    snprintf(m, sizeof(m), "%s %s を入れました", e.name.c_str(), e.version.c_str());
    finishOk(m);
    return true;
}

bool AppStore::remove(int index){
    if(busy()) return false;
    const AppRepo::Entry* e = entry(index);
    if(!e) return false;
    const Installed st = installed_[index];
    if(st == Installed::No || st == Installed::Conflict) return false;
    FixedString<PICO_PATH_LEN> dir;
    appDir(index, dir);
    LuaAppScanner::Unregister(dir.c_str());
    const bool ok = PICO_IO::removeRecursive(dir.c_str());
    FixedString<PICO_PATH_LEN> list_path;
    if(InstalledListPath(list_path, e->id.c_str())) OSData::SD.remove(list_path.c_str());
    refreshInstalled();
    revision_++;
    char m[PICO_STR_LL];
    snprintf(m, sizeof(m), ok ? "%s を削除しました" : "%s を削除しきれませんでした", e->name.c_str());
    state_ = ok ? State::Done : State::Failed;
    message_.assign(m);
    return ok;
}
