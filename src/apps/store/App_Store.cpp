#include "apps/store/App_Store.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

#include "Arduino.h"
#include "OS_Data.hpp"
#include "system/Config_Functions.hpp"
#include "services/App_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "lua/LuaAppScanner.hpp"
#include "storage/SD_IO.hpp"
#include "storage/SD_Path.hpp"
#include "apps/store/App_Repo_Keys.hpp"

namespace {
    using namespace PICO_Path::FILE;
    using AppInstall::MakeParent;
    using AppInstall::JoinRel;
    using namespace PICO_Path::FILE::CFG;

    // 1回のupdate()でHTTPを進める回数と時間の上限(1回で最大1KB読む)
    constexpr int kPumpPerUpdate = 8;
    constexpr unsigned long kPumpBudgetMs = 6;

    // /sys/store/state.cfg(最後に受け付けた一覧の取得元・鍵・serial)
    struct SavedState {
        FixedString<PICO_STR_L> url;
        FixedString<65> key;
        uint32_t serial = 0;
    };
    void ReadState(SavedState& st){
        if(!OSData::SD.exists(STORE_STATE)) return;
        PICO_Config::ParseFile(STORE_STATE, [&](const char* key, const char* value){
            if(strcmp(key, "url") == 0) st.url.assign(value);
            else if(strcmp(key, "key") == 0) st.key.assign(value);
            else if(strcmp(key, "serial") == 0) st.serial = (uint32_t)strtoul(value, nullptr, 10);
        });
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
    int n = f.fgets(work_.line, sizeof(work_.line));
    if(n > 0 && AppRepo::ParseCatalogHeader(work_.line, serial)){
        ok = true;
        if(fill) count_ = 0;
        int lineno = 1;
        while((n = f.fgets(work_.line, sizeof(work_.line))) > 0){
            lineno++;
            //1行が長すぎる(途中で切れた)行は、残りごと読み捨てる
            if(n == (int)sizeof(work_.line) - 1 && work_.line[n - 1] != '\n'){
                LOG_SYS_WARN("AppStore: カタログの%d行目が長すぎます", lineno);
                while((n = f.fgets(work_.line, sizeof(work_.line))) > 0 && work_.line[n - 1] != '\n'){}
                continue;
            }
            if(!fill) continue;
            if(count_ >= AppRepo::kMaxEntries){
                LOG_SYS_WARN("AppStore: カタログのアプリは%d件まで読みます", AppRepo::kMaxEntries);
                break;
            }
            bool skip = false;
            if(AppRepo::ParseCatalogLine(work_.line, entries_[count_], skip)){
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
    if(!OSData::SD.exists(STORE_STATE)) return false;
    SavedState saved;
    ReadState(saved);
    const uint32_t saved_serial = saved.serial;
    if(!(saved.url == base_url_.c_str())) return false;
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

bool AppStore::installable(int index) const {
    const AppRepo::Entry* e = entry(index);
    return e && e->status == AppRepo::Status::Normal && !AppInstall::CheckOs(*e);
}

bool AppStore::needsNewerOs(int index) const {
    const AppRepo::Entry* e = entry(index);
    return e && AppInstall::CheckOs(*e) != nullptr;
}

bool AppStore::blockedInstalled(int index) const {
    const AppRepo::Entry* e = entry(index);
    if(!e || e->status != AppRepo::Status::Blocked) return false;
    const Installed st = installed_[index];
    return st != Installed::No && st != Installed::Foreign && st != Installed::Conflict;
}

bool AppStore::appDir(int index, FixedString<PICO_PATH_LEN>& out) const {
    const AppRepo::Entry* e = entry(index);
    return e && AppInstall::AppDir(*e, out);
}

void AppStore::refreshInstalled(){
    held_mask_ = 0;
    for(int i = 0; i < count_; i++){
        installed_[i] = AppInstall::Check(entries_[i], &installed_ver_[i], &installed_perms_[i]);
        FixedString<PICO_STR_S> hold;
        if(installed_[i] == Installed::Update && AppInstall::HeldVersion(entries_[i].id.c_str(), hold)
           && AppRepo::CompareVersion(entries_[i].version.c_str(), hold.c_str()) <= 0){
            held_mask_ |= (uint64_t)1 << i;
        }
    }
}

bool AppStore::held(int index) const {
    return index >= 0 && index < count_ && (held_mask_ >> index) & 1;
}

bool AppStore::permsGrow(int index) const {
    const AppRepo::Entry* e = entry(index);
    return e && (e->perms & ~installed_perms_[index]) != 0;
}

bool AppStore::batchUpdatable(int index) const {
    return installed(index) == Installed::Update && installable(index) && !held(index) && !permsGrow(index);
}

int AppStore::batchCount() const {
    int n = 0;
    for(int i = 0; i < count_; i++) if(batchUpdatable(i)) n++;
    return n;
}

int AppStore::updateCount() const {
    int n = 0;
    for(int i = 0; i < count_; i++) if(installed_[i] == Installed::Update && installable(i) && !held(i)) n++;
    return n;
}

bool AppStore::beginUpdateAll(){
    if(busy() || batchRunning()) return false;
    batch_mask_ = 0;
    for(int i = 0; i < count_; i++) if(batchUpdatable(i)) batch_mask_ |= (uint64_t)1 << i;
    if(batch_mask_ == 0) return false;
    batch_ok_ = batch_failed_ = 0;
    batch_first_error_.clear();
    advanceBatch();
    return true;
}

// 1つ終わるたび(と始め)に呼ぶ。次を始めるか、全部済んだら結果の1行を出す
void AppStore::advanceBatch(){
    if(batch_current_ >= 0){
        const AppRepo::Entry* e = entry(batch_current_);
        if(state_ == State::Done){
            batch_ok_++;
        }else{
            batch_failed_++;
            if(batch_first_error_.empty() && e){
                batch_first_error_.assign(e->name.c_str());
                batch_first_error_.append(": ");
                batch_first_error_.append(message_.c_str());
            }
        }
        batch_current_ = -1;
    }
    while(batch_mask_ != 0){
        int i = 0;
        while(!((batch_mask_ >> i) & 1)) i++;
        batch_mask_ &= ~((uint64_t)1 << i);
        //一覧を取り直した等で入れられなくなっていれば飛ばす
        if(!batchUpdatable(i)) continue;
        batch_current_ = i;
        if(beginInstall(i)) return;
        advanceBatch(); //始められなかった(失敗として数えて次へ)
        return;
    }
    char m[PICO_STR_LL];
    if(batch_failed_ == 0){
        snprintf(m, sizeof(m), "%d件のアプリを更新しました", batch_ok_);
        finishOk(m);
    }else{
        snprintf(m, sizeof(m), "%d件を更新、%d件は失敗 (%s)", batch_ok_, batch_failed_, batch_first_error_.c_str());
        state_ = State::Failed;
        message_.assign(m);
        LOG_SYS_WARN("AppStore: %s", m);
    }
}

bool AppStore::previousVersion(int index, FixedString<PICO_STR_S>& out) const {
    out.clear();
    const AppRepo::Entry* e = entry(index);
    const Installed st = installed(index);
    if(!e || st == Installed::No || st == Installed::Foreign || st == Installed::Conflict) return false;
    return AppInstall::PreviousVersion(e->id.c_str(), out);
}

bool AppStore::rollback(int index){
    if(busy() || batchRunning()) return false;
    const AppRepo::Entry* e = entry(index);
    FixedString<PICO_STR_S> prev;
    FixedString<PICO_PATH_LEN> dir;
    if(!e || !previousVersion(index, prev) || !appDir(index, dir)) return false;
    FixedString<PICO_STR_S> restored;
    const char* err = AppInstall::Rollback(e->id.c_str(), dir.c_str(), work_, &restored);
    refreshInstalled();
    revision_++;
    if(err){
        state_ = State::Failed;
        message_.assign(err);
        return false;
    }
    char m[PICO_STR_LL];
    snprintf(m, sizeof(m), "%s を %s へ戻しました", e->name.c_str(), restored.c_str());
    finishOk(m);
    return true;
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
    const bool small = step_ == Step::Signature || step_ == Step::FilesSig;
    gate_.attach(&http_.response(), small ? (IHttpSink*)&mem_sink_ : (IHttpSink*)&file_sink_);
    if(!http_.begin(url, HttpRequest::Method::GET, &gate_)){
        fail("取得を始められません");
        return false;
    }
    return true;
}

bool AppStore::beginRefresh(){
    if(busy() || batchRunning()) return false;
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
    batch_mask_ = 0;
    batch_current_ = -1;
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
    if(batch_current_ >= 0 && state_ != State::Busy){
        advanceBatch();
        return;
    }
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
        case Step::FilesSig:  onFilesSig(); break;
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
    //同じURL・同じ鍵で前に見た番号より古い一覧は受け付けない(古い署名済みの一覧へ戻されるのを防ぐ)。
    //鍵ごとに数えるのは、漏れた鍵で最大の番号を署名されても、鍵を替えれば数え直せるようにするため
    char pubhex[65];
    AppRepo::HexEncode(pub, 32, pubhex);
    SavedState saved;
    ReadState(saved);
    const bool same_source = saved.url == base_url_.c_str() && saved.key == pubhex;
    if(same_source && serial < saved.serial){
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
    PICO_Config::SetValue(STORE_STATE, "key", pubhex);
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
    if(state_ == State::Busy || (batch_current_ >= 0 && batch_current_ != index)) return false;
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
    if(st == Installed::KeyChanged){
        state_ = State::Failed;
        message_.assign("作者の鍵が変わりました(削除してから入れ直すと、新しい鍵で入ります)");
        return false;
    }
    //入っている版より古い版は入れない(漏れたカタログ鍵で、作者が昔署名した穴のある版へ戻されるのを防ぐ)
    if(st == Installed::Newer){
        state_ = State::Failed;
        message_.assign("入っている版より古い版は入れられません");
        return false;
    }
    if(e->status != AppRepo::Status::Normal){
        state_ = State::Failed;
        message_.assign(e->status == AppRepo::Status::Blocked ? "危険と判断されたアプリです" : "取り下げられたアプリです");
        return false;
    }
    if(const char* err = AppInstall::CheckOs(*e)){
        state_ = State::Failed;
        message_.assign(err);
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

void AppStore::onFiles(){
    const AppRepo::Entry& e = entries_[target_];
    uint8_t digest[32];
    file_sink_.digest(digest);
    if(memcmp(digest, e.files_sha256, 32) != 0){
        fail("ファイル一覧(files.tsv)が一覧の署名と合いません");
        return;
    }
    step_ = Step::FilesSig;
    mem_sink_.reset();
    FixedString<PICO_STR_L> rel;
    rel.assign(e.id.c_str());
    rel.append("/");
    rel.append(e.version.c_str());
    rel.append("/files.sig");
    startGet(rel.c_str());
}

void AppStore::onFilesSig(){
    const AppRepo::Entry& e = entries_[target_];
    uint8_t pub[32], sig[64];
    if(!AppRepo::ParseSignature(mem_sink_.text(), pub, sig)){
        fail("作者の署名(files.sig)を読めません");
        return;
    }
    if(memcmp(pub, e.author_key, 32) != 0){
        fail("作者の署名の鍵が一覧と合いません");
        return;
    }
    if(installed_[target_] == Installed::KeyChanged){
        fail("作者の鍵が変わりました(削除してから入れ直すと、新しい鍵で入ります)");
        return;
    }
    if(!AppRepo::VerifyFiles(e.id.c_str(), e.version.c_str(), e.files_sha256, pub, sig)){
        fail("作者の署名が合いません");
        return;
    }
    int count = 0;
    if(const char* err = AppInstall::CheckFilesList(e, work_, count)){
        fail(err);
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
        if(const char* err = AppInstall::CheckStagedConfig(e)){
            fail(err);
            return false;
        }
        message_.assign("インストール中…");
        if(const char* err = AppInstall::Commit(e, allow_replace_, work_)){
            fail(err);
            return false;
        }
        refreshInstalled();
        revision_++;
        char m[PICO_STR_LL];
        snprintf(m, sizeof(m), "%s %s を入れました", e.name.c_str(), e.version.c_str());
        finishOk(m);
        return true;
    }
    if(!AppInstall::ReadFileLine(STORE_STAGING_FILES, file_index_, work_)){
        fail("ファイル一覧を読み直せません");
        return false;
    }
    file_ = work_.file;
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

bool AppStore::remove(int index){
    if(busy() || batchRunning()) return false;
    const AppRepo::Entry* e = entry(index);
    if(!e) return false;
    const Installed st = installed_[index];
    if(st == Installed::No || st == Installed::Conflict) return false;
    FixedString<PICO_PATH_LEN> dir;
    appDir(index, dir);
    LuaAppScanner::Unregister(dir.c_str());
    const bool ok = PICO_IO::removeRecursive(dir.c_str());
    AppInstall::Forget(e->id.c_str());
    refreshInstalled();
    revision_++;
    char m[PICO_STR_LL];
    snprintf(m, sizeof(m), ok ? "%s を削除しました" : "%s を削除しきれませんでした", e->name.c_str());
    state_ = ok ? State::Done : State::Failed;
    message_.assign(m);
    return ok;
}
