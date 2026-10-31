#include "apps/recorder/Transcribe_Client.hpp"

#include "system/Config_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "storage/SD_Path.hpp"
#include "crypto/Secret_File.hpp"
#include "OS_Data.hpp"
#include "Arduino.h"
#include "system/I18n_Functions.hpp"

#include <cstdio>
#include <cstring>
#include <strings.h>

namespace {
    constexpr const char* kKeyPurpose = "transcribe-key";

    // ヘッダへそのまま入れるので、英数字と - _ . 以外は受け付けない(OpenAIは sk-proj-…、Groqは gsk_…)
    bool ValidKey(const char* key){
        for(const char* p = key; *p; p++){
            const char c = *p;
            const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
                         || c == '-' || c == '_' || c == '.';
            if(!ok) return false;
        }
        return true;
    }
}

// ---------------------------------------------------------------------------
// 設定
// ---------------------------------------------------------------------------

bool TranscribeClient::configure(const Config& given, const char* key){
    configured_ = false;
    cfg_ = given;
    if(cfg_.api.empty()) cfg_.api.assign(kDefaultApi);
    if(cfg_.model.empty()) cfg_.model.assign(kDefaultModel);
    state_ = State::NotConfigured;

    //末尾の / を落として "/audio/transcriptions" を足す
    char url[PICO_STR_LL + 32];
    size_t n = cfg_.api.length();
    while(n > 0 && cfg_.api.c_str()[n - 1] == '/') n--;
    snprintf(url, sizeof(url), "%.*s/audio/transcriptions", (int)n, cfg_.api.c_str());
    if(!UrlTools::Parse(endpoint_, url)){
        message_.assign(TR("transcribe.cfg の api が URL ではありません"));
        return false;
    }
    if(!Transcribe::SafeFieldValue(cfg_.model.c_str()) || !Transcribe::SafeFieldValue(cfg_.language.c_str())
       || !Transcribe::SafeFieldValue(cfg_.prompt.c_str())){
        message_.assign(TR("transcribe.cfg の model / language / prompt に使えない文字があります"));
        return false;
    }

    req_.clearExtraHeaders();
    const bool has_key = key && *key;
    if(!has_key){
        //OpenAI等の公開のAPIはキーが要る。手元のサーバ(api を変えたとき)なら無くてよい
        if(strcasecmp(cfg_.api.c_str(), kDefaultApi) == 0){
            message_.assign(TR("APIキーがありません。[キー] から入れてください"));
            return false;
        }
    }else{
        if(strlen(key) > kMaxKeyBytes || !ValidKey(key)){
            message_.assign(TR("APIキーに使えない文字があるか、長すぎます"));
            return false;
        }
        char auth[kMaxKeyBytes + 8];
        snprintf(auth, sizeof(auth), "Bearer %s", key);
        if(!req_.addExtraHeader("Authorization", auth)){
            message_.assign(TR("APIキーが長すぎます"));
            return false;
        }
    }
    req_.setKeepAlive(false);
    req_.setIdleTimeout(true);
    req_.setTimeoutMs(kResponseTimeoutMs);
    configured_ = true;
    state_ = State::Ready;
    message_.clear();
    return true;
}

bool TranscribeClient::loadConfig(){
    if(!OSData::SD_usable){
        state_ = State::NotConfigured;
        configured_ = false;
        message_.assign(TR("SDカードがありません"));
        return false;
    }
    Config c;
    c.language.assign(kDefaultLanguage);
    PICO_Config::ParseFile(PICO_Path::FILE::CFG::SYS_TRANSCRIBE_CFG,
        [&](const char* key, const char* value){
            if(strcmp(key, "api") == 0) c.api.assign(value);
            else if(strcmp(key, "model") == 0) c.model.assign(value);
            else if(strcmp(key, "language") == 0) c.language.assign(value);   // 空にすると自動で判定
            else if(strcmp(key, "prompt") == 0) c.prompt.assign(value);
        });

    char key[kMaxKeyBytes + 1];
    bool rewrite = false;
    if(!PICO_SecretFile::Load(PICO_Path::FILE::CFG::SYS_TRANSCRIBE_KEY, kKeyPurpose, key, sizeof(key), &rewrite)){
        key[0] = '\0';
    }
    const bool ok = configure(c, key);
    //母艦で平文のまま置かれていたら、暗号化して書き直す(SDだけを落としたときに読まれないように)
    if(ok && rewrite && key[0]){
        if(SaveKey(key)) LOG_APP_MSG("Transcribe: APIキーを暗号化して保存し直しました");
    }
    memset(key, 0, sizeof(key));
    return ok;
}

bool TranscribeClient::SaveKey(const char* key){
    if(!key || !*key || strlen(key) > kMaxKeyBytes || !ValidKey(key)) return false;
    return PICO_SecretFile::Save(PICO_Path::FILE::CFG::SYS_TRANSCRIBE_KEY, kKeyPurpose,
                                 "# 文字起こしのAPIキー(暗号化)", key);
}

bool TranscribeClient::TextPathFor(const char* wav_path, char* out, size_t cap){
    const size_t n = wav_path ? strlen(wav_path) : 0;
    if(n < 5 || strcasecmp(wav_path + n - 4, ".wav") != 0 || n + 1 > cap) return false;
    memcpy(out, wav_path, n - 4);
    memcpy(out + n - 4, ".txt", 5);
    return true;
}

void TranscribeClient::partPath(char* out, size_t cap) const {
    snprintf(out, cap, "%s.part", text_path_.c_str());
}

// ---------------------------------------------------------------------------
// 送る
// ---------------------------------------------------------------------------

bool TranscribeClient::start(const char* wav_path){
    if(busy()) return false;
    if(!configured_){
        state_ = State::NotConfigured;
        return false;
    }
    closeFiles(false);
    wav_path_.assign(wav_path ? wav_path : "");
    char txt[PICO_PATH_LEN + 1];
    if(!TextPathFor(wav_path, txt, sizeof(txt))){
        fail(TR("WAVではありません"));
        return false;
    }
    text_path_.assign(txt);

    wav_ = OSData::SD.open(wav_path, O_RDONLY);
    if(!wav_){
        fail(TR("録音を開けません"));
        return false;
    }
    const uint64_t size = wav_.size();
    if(size <= 44){
        fail(TR("録音が空です"));
        return false;
    }
    if(size > kMaxUploadBytes){
        char msg[96];
        snprintf(msg, sizeof(msg), TR("録音が大きすぎます(%luMB。上限25MB)"), (unsigned long)(size / (1024 * 1024)));
        fail(msg);
        return false;
    }
    wav_size_ = (uint32_t)size;

    const char* name = strrchr(wav_path, '/');
    name = name ? name + 1 : wav_path;
    char boundary[40];
    Transcribe::MakeBoundary(boundary, sizeof(boundary), millis() ^ wav_size_);
    Transcribe::Fields f;
    f.model = cfg_.model.c_str();
    f.language = cfg_.language.c_str();
    f.prompt = cfg_.prompt.c_str();
    if(!Transcribe::BuildHead(head_, boundary, f, name) || !Transcribe::BuildTail(tail_, boundary)){
        fail(TR("送る内容を作れません(model / prompt が長すぎる?)"));
        return false;
    }
    char content_type[PICO_STR_L];
    snprintf(content_type, sizeof(content_type), "multipart/form-data; boundary=%s", boundary);

    pos_ = 0;
    err_len_ = 0;
    out_failed_ = false;
    message_.clear();
    state_ = State::Sending;
    if(!req_.beginStream(endpoint_, HttpRequest::Method::POST, this, this, content_type)){
        fail(TR("送り始められません"));
        return false;
    }
    LOG_APP_MSG("Transcribe: %s を送ります(%luB → %s)", name, (unsigned long)wav_size_, endpoint_.host.c_str());
    return true;
}

size_t TranscribeClient::size() const {
    return head_.length() + wav_size_ + tail_.length();
}

int TranscribeClient::read(uint8_t* buf, size_t cap){
    const size_t head_len = head_.length();
    const size_t file_end = head_len + wav_size_;
    const size_t total = file_end + tail_.length();
    size_t n = 0;
    while(n < cap && pos_ < total){
        if(pos_ < head_len){
            size_t m = head_len - pos_;
            if(m > cap - n) m = cap - n;
            memcpy(buf + n, head_.c_str() + pos_, m);
            n += m; pos_ += m;
        }else if(pos_ < file_end){
            size_t m = file_end - pos_;
            if(m > cap - n) m = cap - n;
            const int got = wav_.read(buf + n, m);
            if(got <= 0) return n > 0 ? (int)n : -1;
            n += (size_t)got; pos_ += (size_t)got;
        }else{
            const size_t off = pos_ - file_end;
            size_t m = tail_.length() - off;
            if(m > cap - n) m = cap - n;
            memcpy(buf + n, tail_.c_str() + off, m);
            n += m; pos_ += m;
        }
    }
    return (int)n;
}

bool TranscribeClient::rewind(){
    pos_ = 0;
    return wav_ && wav_.seek(0);
}

// ---------------------------------------------------------------------------
// 返事
// ---------------------------------------------------------------------------

bool TranscribeClient::write(const void* data, size_t len){
    if(req_.response().statusCode() != 200){
        //理由を拾う分だけ取っておく
        const size_t room = sizeof(err_body_) - 1 - err_len_;
        const size_t m = len < room ? len : room;
        memcpy(err_body_ + err_len_, data, m);
        err_len_ += m;
        err_body_[err_len_] = '\0';
        return true;
    }
    if(out_failed_) return false;
    if(!out_){
        char part[PICO_PATH_LEN + 8];
        partPath(part, sizeof(part));
        out_ = OSData::SD.open(part, O_WRONLY | O_CREAT | O_TRUNC);
        if(!out_){
            out_failed_ = true;
            return false;
        }
    }
    if(out_.write((const uint8_t*)data, len) != len){
        out_failed_ = true;
        return false;
    }
    return true;
}

void TranscribeClient::update(bool network_up){
    if(!busy()) return;
    if(!network_up){
        req_.cancel();
        fail(TR("Wi-Fiが切れました"));
        return;
    }
    req_.update();
    if(state_ == State::Sending && req_.waitingResponse()) state_ = State::Waiting;
    if(req_.getStatus() != TaskTools::PROCESSING) finish();
}

void TranscribeClient::finish(){
    if(req_.getStatus() == TaskTools::FAILED){
        if(out_failed_){
            fail(TR("文章をSDへ書けません"));
        }else{
            char msg[160];
            snprintf(msg, sizeof(msg), TR("送れませんでした: %s"), req_.failureToStr());
            fail(msg);
        }
        req_.closeConnection();
        return;
    }
    const int status = req_.response().statusCode();
    req_.closeConnection();
    if(status != 200){
        Transcribe::ErrorParser p;
        p.begin();
        p.feed(err_body_, err_len_);
        char msg[PICO_STR_512B];
        Transcribe::DescribeFailure(msg, sizeof(msg), status, p.message());
        LOG_APP_WARN("Transcribe: %s", msg);
        fail(msg);
        return;
    }
    //文章が空でも .txt は作る(「聞き取れる言葉が無かった」ことが分かるように)
    if(!out_){
        char part[PICO_PATH_LEN + 8];
        partPath(part, sizeof(part));
        out_ = OSData::SD.open(part, O_WRONLY | O_CREAT | O_TRUNC);
    }
    if(!out_){
        fail(TR("文章をSDへ書けません"));
        return;
    }
    closeFiles(true);
    if(state_ == State::Failed) return;
    state_ = State::Done;
    message_.assign(TR("文字起こしが終わりました"));
    LOG_APP_MSG("Transcribe: %s へ置きました", text_path_.c_str());
}

void TranscribeClient::closeFiles(bool keep_text){
    if(wav_) wav_.close();
    if(!out_) return;
    const bool wrote_ok = out_.sync();
    out_.close();
    char part[PICO_PATH_LEN + 8];
    partPath(part, sizeof(part));
    if(!keep_text || !wrote_ok){
        OSData::SD.remove(part);
        if(keep_text) fail(TR("文章をSDへ書けません"));
        return;
    }
    if(OSData::SD.exists(text_path_.c_str())) OSData::SD.remove(text_path_.c_str());
    if(!OSData::SD.rename(part, text_path_.c_str())) fail(TR("文章をSDへ書けません"));
}

void TranscribeClient::fail(const char* text){
    closeFiles(false);
    state_ = State::Failed;
    message_.assign(text ? text : "");
}

void TranscribeClient::cancel(){
    if(!busy()) return;
    req_.cancel();
    closeFiles(false);
    state_ = configured_ ? State::Ready : State::NotConfigured;
    message_.assign(TR("やめました"));
}

int TranscribeClient::progressPercent() const {
    const size_t total = req_.bodyTotal();
    if(total == 0) return 0;
    return (int)((uint64_t)req_.bodySent() * 100 / total);
}
