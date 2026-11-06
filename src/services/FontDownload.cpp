#include "services/FontDownload.hpp"
#include "gui/core/JpFont_Ext.hpp"
#include "hal/Network_Functions.hpp"
#include "system/I18n_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "util/Url.hpp"
#include "OS_Data.hpp"

#include <Arduino.h>
#include <cstdio>
#include <cstring>

namespace {
    constexpr const char* kDir = "/sys/fonts";
}

bool FontDownload::FileSink::open(const char* path, uint32_t limit){
    close();
    sha_.reset();
    bytes_ = 0;
    limit_ = limit;
    overflow_ = false;
    failed_ = false;
    file_ = OSData::SD.open(path, O_WRONLY | O_CREAT | O_TRUNC);
    return (bool)file_;
}

void FontDownload::FileSink::close(){
    if(file_) file_.close();
}

bool FontDownload::FileSink::write(const void* data, size_t len){
    if(!file_){ failed_ = true; return false; }
    if(bytes_ + len > limit_){ overflow_ = true; return false; }
    if(file_.write(data, len) != len){ failed_ = true; return false; }
    sha_.update(data, len);
    bytes_ += (uint32_t)len;
    return true;
}

FontDownload::FontDownload(){
    base_url_.assign(kDefaultUrl);
    http_.setKeepAlive(true);
    // 100KBほどあるので、全体の時間ではなく「何も届かない時間」で打ち切る
    http_.setIdleTimeout(true);
}

FontDownload::~FontDownload(){
    cancel();
    http_.closeConnection();
}

bool FontDownload::setBaseUrl(const char* url){
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

uint32_t FontDownload::progressDone() const {
    return done_bytes_ + (state_ == State::Running ? sink_.bytes() : 0);
}

uint32_t FontDownload::progressTotal() const {
    uint32_t t = 0;
    for(int i = 0; i < pending_count_; i++) t += JpFontExt::Info(pending_[i]).bytes;
    return t;
}

void FontDownload::fail(const char* text){
    sink_.close();
    http_.cancel();
    if(!part_path_.empty()) OSData::SD.remove(part_path_.c_str());
    part_path_.clear();
    state_ = State::Failed;
    message_.assign(text);
    LOG_SYS_WARN("FontDownload: %s", text);
}

bool FontDownload::begin(){
    if(busy()) return false;
    if(!NetworkFunctions::IsConnected()){ message_.assign(TR("Wi-Fiに繋がっていません")); state_ = State::Failed; return false; }
    if(!OSData::SD_usable){ message_.assign(TR("SDカードが使えません")); state_ = State::Failed; return false; }
    pending_count_ = 0;
    current_ = -1;
    done_bytes_ = 0;
    JpFontExt::Ready();  // 今の状態を確かめる(まだ開いていなければ開く)
    const int sizes[2] = {16, 24};
    for(int px : sizes){
        if(JpFontExt::Store(px).state() != ExtGlyphStore::State::Ready) pending_[pending_count_++] = px;
    }
    if(pending_count_ == 0){
        state_ = State::Done;
        message_.assign(TR("追加の漢字フォントは入っています"));
        return true;
    }
    if(!OSData::SD.exists(kDir)) OSData::SD.mkdir(kDir);
    state_ = State::Running;
    return startNext();
}

bool FontDownload::startNext(){
    current_++;
    if(current_ >= pending_count_){
        http_.closeConnection();
        JpFontExt::Reload();
        state_ = State::Done;
        message_.assign(TR("追加の漢字フォントを入れました"));
        LOG_SYS_OK("FontDownload: 入れました");
        return true;
    }
    const JpFontData::Ext& e = JpFontExt::Info(pending_[current_]);
    if(!part_path_.assign(e.sd_path) || !part_path_.append(".part")){
        fail(TR("置き場所のパスが長すぎます"));
        return false;
    }
    if(!sink_.open(part_path_.c_str(), e.bytes)){
        fail(TR("SDカードに書き込めません"));
        return false;
    }
    FixedString<PICO_STR_256B> full;
    Url url;
    if(!full.assign(base_url_.c_str()) || !full.append(e.web_name) || !UrlTools::Parse(url, full.c_str())){
        fail(TR("URLが長すぎます"));
        return false;
    }
    gate_.attach(&http_.response(), &sink_);
    if(!http_.begin(url, HttpRequest::Method::GET, &gate_)){
        fail(TR("取得を始められません"));
        return false;
    }
    char m[PICO_STR_LL];
    snprintf(m, sizeof(m), TR("%dpxの字をダウンロード中…"), e.px);
    message_.assign(m);
    LOG_SYS_MSG("FontDownload: %s を %s へ", full.c_str(), e.sd_path);
    return true;
}

void FontDownload::cancel(){
    if(!busy()) return;
    sink_.close();
    http_.cancel();
    if(!part_path_.empty()) OSData::SD.remove(part_path_.c_str());
    part_path_.clear();
    // 取り終えた方は置いてあるので使えるようにする
    JpFontExt::Reload();
    state_ = State::Idle;
    message_.assign(TR("取りやめました"));
}

void FontDownload::update(){
    if(!busy()) return;
    if(!OSData::SD_usable){ fail(TR("SDカードが使えません")); return; }
    const unsigned long start = millis();
    for(int i = 0; i < kPumpPerUpdate; i++){
        http_.update();
        if(http_.getStatus() != TaskTools::PROCESSING) break;
        if(millis() - start >= kPumpBudgetMs) break;
    }
    if(http_.getStatus() == TaskTools::PROCESSING) return;

    sink_.close();
    if(http_.getStatus() != TaskTools::SUCCESS){
        if(sink_.overflowed()) fail(TR("フォントが決まりの大きさより大きいです"));
        else if(sink_.failed()) fail(TR("SDカードに書き込めません(空きが足りない?)"));
        else{
            FixedString<PICO_STR_LL> m;
            m.assign(TR("通信に失敗しました: "));
            m.append(http_.failureToStr());
            fail(m.c_str());
        }
        return;
    }
    const int code = http_.response().statusCode();
    if(code != 200){
        char m[64];
        snprintf(m, sizeof(m), code == 404 ? TR("配布元に見つかりません(404)") : TR("配布元が %d を返しました"), code);
        fail(m);
        return;
    }
    onFile();
}

void FontDownload::onFile(){
    const JpFontData::Ext& e = JpFontExt::Info(pending_[current_]);
    uint8_t digest[32];
    sink_.digest(digest);
    if(sink_.bytes() != e.bytes){
        fail(TR("フォントが途中で切れています"));
        return;
    }
    if(memcmp(digest, e.sha256, 32) != 0){
        fail(TR("フォントの中身が本体の決まりと合いません"));
        return;
    }
    // 開いたままの古いファイルを閉じてから置き換える(次に字を引くとき開き直す)
    JpFontExt::Store(e.px).reload();
    if(OSData::SD.exists(e.sd_path)) OSData::SD.remove(e.sd_path);
    if(!OSData::SD.rename(part_path_.c_str(), e.sd_path)){
        fail(TR("フォントを保存できません"));
        return;
    }
    part_path_.clear();
    done_bytes_ += e.bytes;
    LOG_SYS_OK("FontDownload: %s", e.sd_path);
    startNext();
}
