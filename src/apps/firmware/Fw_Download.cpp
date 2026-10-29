#include "apps/firmware/Fw_Download.hpp"
#include "storage/SD_IO.hpp"
#include "system/Log_Functions.hpp"
#include "util/Url.hpp"
#include "OS_Data.hpp"

#include <Arduino.h>
#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// 受け取り口
// ---------------------------------------------------------------------------

bool FwDownload::MemSink::write(const void* data, size_t len){
    if(len_ + len > sizeof(buf_)){
        overflow_ = true;
        return false;
    }
    memcpy(buf_ + len_, data, len);
    len_ += len;
    return true;
}

bool FwDownload::FileSink::open(const char* path, uint32_t limit){
    close();
    sha_.reset();
    bytes_ = 0;
    limit_ = limit;
    overflow_ = false;
    failed_ = false;
    file_ = OSData::SD.open(path, O_WRONLY | O_CREAT | O_TRUNC);
    return (bool)file_;
}

void FwDownload::FileSink::close(){
    if(file_) file_.close();
}

bool FwDownload::FileSink::write(const void* data, size_t len){
    if(!file_){ failed_ = true; return false; }
    if(bytes_ + len > limit_){
        overflow_ = true;
        return false;
    }
    if(file_.write(data, len) != len){ failed_ = true; return false; }
    sha_.update(data, len);
    bytes_ += (uint32_t)len;
    return true;
}

// ---------------------------------------------------------------------------
// 設定
// ---------------------------------------------------------------------------

FwDownload::FwDownload(){
    base_url_.assign(kDefaultUrl);
    http_.setKeepAlive(true);
    // 1.4MBほどあるので、全体の10秒ではなく「何も届かない時間」で打ち切る
    http_.setIdleTimeout(true);
}

FwDownload::~FwDownload(){
    cancel();
    close();
}

bool FwDownload::setBaseUrl(const char* url){
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

const FwIndex::Entry* FwDownload::entry(int index) const {
    return (index >= 0 && index < count_) ? &entries_[index] : nullptr;
}

// ---------------------------------------------------------------------------
// 通信
// ---------------------------------------------------------------------------

void FwDownload::fail(const char* text){
    file_sink_.close();
    http_.cancel(); // 失敗した接続は使い回さない
    if(state_ == State::File && !part_path_.empty()) OSData::SD.remove(part_path_.c_str());
    state_ = State::Failed;
    message_.assign(text);
    LOG_SYS_WARN("FwDownload: %s", text);
}

bool FwDownload::startGet(const char* name, IHttpSink* sink){
    FixedString<PICO_STR_256B> full;
    Url url;
    if(!full.assign(base_url_.c_str()) || !full.append(name) || !UrlTools::Parse(url, full.c_str())){
        fail("URLが長すぎます");
        return false;
    }
    // 200の本文だけを書く(転送(3xx)やエラーの本文をSHA-256・一覧に混ぜない)
    gate_.attach(&http_.response(), sink);
    if(!http_.begin(url, HttpRequest::Method::GET, &gate_)){
        fail("取得を始められません");
        return false;
    }
    return true;
}

bool FwDownload::beginIndex(const FwIndex::Filter& filter){
    if(busy()) return false;
    filter_env_.assign(filter.env ? filter.env : "");
    filter_ = filter;
    filter_.env = filter_env_.c_str();
    count_ = 0;
    target_ = -1;
    result_ = FwIndex::Result{};
    path_.clear();
    part_path_.clear();
    mem_sink_.reset();
    state_ = State::Index;
    message_.assign("版の一覧を取得中…");
    return startGet(FwIndex::kFileName, &mem_sink_);
}

bool FwDownload::beginFile(int index, const char* dir){
    if(busy() || state_ == State::Idle || index < 0 || index >= count_) return false;
    if(!OSData::SD_usable){
        state_ = State::Failed;
        message_.assign("SDカードが使えません");
        return false;
    }
    const FwIndex::Entry& e = entries_[index];
    target_ = index;
    state_ = State::File;
    if(!OSData::SD.exists(dir)) OSData::SD.mkdir(dir);
    FixedString<PICO_STR_L> part_name;
    part_name.assign(e.file.c_str());
    if(!PICO_IO::join(path_, dir, e.file.c_str()) || !part_name.append(".part") ||
       !PICO_IO::join(part_path_, dir, part_name.c_str())){
        part_path_.clear();
        fail("置き場所のパスが長すぎます");
        return false;
    }
    if(!file_sink_.open(part_path_.c_str(), e.bytes)){
        fail("SDカードに書き込めません");
        return false;
    }
    char m[PICO_STR_LL];
    snprintf(m, sizeof(m), "#%lu (%s) をダウンロード中…", (unsigned long)e.serial, e.git.c_str());
    message_.assign(m);
    LOG_SYS_MSG("FwDownload: %s%s を %s へ", base_url_.c_str(), e.file.c_str(), path_.c_str());
    return startGet(e.file.c_str(), &file_sink_);
}

void FwDownload::cancel(){
    if(!busy()) return;
    const bool file = state_ == State::File;
    file_sink_.close();
    http_.cancel();
    if(file && !part_path_.empty()) OSData::SD.remove(part_path_.c_str());
    // 一覧を取れていれば選び直せるようにする
    state_ = (file && count_ > 0) ? State::Listed : State::Idle;
    target_ = -1;
    message_.assign("取りやめました");
}

void FwDownload::close(){
    http_.closeConnection();
}

void FwDownload::update(){
    if(!busy()) return;
    const unsigned long start = millis();
    for(int i = 0; i < kPumpPerUpdate; i++){
        http_.update();
        if(http_.getStatus() != TaskTools::PROCESSING) break;
        if(millis() - start >= kPumpBudgetMs) break;
    }
    if(http_.getStatus() == TaskTools::PROCESSING) return;

    file_sink_.close();
    if(http_.getStatus() != TaskTools::SUCCESS){
        if(state_ == State::Index && mem_sink_.overflowed()){
            fail("版の一覧が大きすぎます");
        }else if(state_ == State::File && file_sink_.overflowed()){
            fail("更新ファイルが一覧の大きさより大きいです");
        }else if(state_ == State::File && file_sink_.failed()){
            fail("SDカードに書き込めません(空きが足りない?)");
        }else{
            FixedString<PICO_STR_LL> m;
            m.assign("通信に失敗しました: ");
            m.append(http_.failureToStr());
            fail(m.c_str());
        }
        return;
    }
    const int code = http_.response().statusCode();
    if(code != 200){
        char m[64];
        snprintf(m, sizeof(m), code == 404 ? "配布元に見つかりません(404)" : "配布元が %d を返しました", code);
        fail(m);
        return;
    }
    if(state_ == State::Index) onIndex();
    else onFile();
}

void FwDownload::onIndex(){
    if(!FwIndex::Parse(mem_sink_.data(), mem_sink_.size(), filter_, entries_, FwIndex::kMaxEntries, result_)){
        fail("版の一覧の形式が違います(本体が古い・配布元が違う?)");
        return;
    }
    count_ = result_.count;
    state_ = State::Listed;
    char m[PICO_STR_LL];
    if(count_ > 0){
        snprintf(m, sizeof(m), "%d個の版があります", count_);
    }else if(result_.hidden_env > 0 && result_.hidden_unsigned == 0){
        snprintf(m, sizeof(m), "この構成(%s)向けの配布はありません", filter_env_.c_str());
    }else if(result_.hidden_unsigned > 0){
        snprintf(m, sizeof(m), "署名のある版がありません");
    }else{
        snprintf(m, sizeof(m), "配布されている版がありません");
    }
    message_.assign(m);
    LOG_SYS_MSG("FwDownload: 一覧 %d件(構成違い%d・署名なし%d・読めない行%d)", count_,
                result_.hidden_env, result_.hidden_unsigned, result_.bad_lines);
}

void FwDownload::onFile(){
    const FwIndex::Entry& e = entries_[target_];
    uint8_t digest[32];
    file_sink_.digest(digest);
    if(file_sink_.bytes() != e.bytes){
        fail("更新ファイルが途中で切れています");
        return;
    }
    if(memcmp(digest, e.sha256, 32) != 0){
        fail("更新ファイルの中身が一覧と合いません(取得の途中で配布元が更新された?)");
        return;
    }
    if(OSData::SD.exists(path_.c_str())) OSData::SD.remove(path_.c_str());
    if(!OSData::SD.rename(part_path_.c_str(), path_.c_str())){
        fail("更新ファイルを保存できません");
        return;
    }
    part_path_.clear();
    state_ = State::Done;
    char m[PICO_STR_LL];
    snprintf(m, sizeof(m), "#%lu (%s) をダウンロードしました", (unsigned long)e.serial, e.git.c_str());
    message_.assign(m);
    LOG_SYS_OK("FwDownload: %s", path_.c_str());
}
