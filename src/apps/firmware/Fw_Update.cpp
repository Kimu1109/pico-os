#include "apps/firmware/Fw_Update.hpp"

#include <cstdio>
#include <cstring>
#include "system/I18n_Functions.hpp"

namespace {
    uint32_t Le32(const uint8_t* p){
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }
}

void FwUpdate::start(FwSource* src, FwStore* store, const Env& env){
    src_ = src;
    store_ = store;
    env_ = env;
    state_ = State::Header;
    message_.assign(TR("更新ファイルを読んでいます"));
    confirm_.clear();
    header_ = FwImage::Header{};
    sig_ = FwImage::Sig::Unsigned;
    prefix_len_ = 0;
    received_ = verified_ = 0;
    writing_ = reading_ = mounted_ = false;
}

void FwUpdate::fail(const char* text){
    if(writing_){ store_->closeWrite(); writing_ = false; }
    if(reading_){ store_->closeRead(); reading_ = false; }
    if(mounted_){ store_->clear(); store_->unmount(); mounted_ = false; }
    message_.assign(text);
    state_ = State::Failed;
    char line[PICO_STR_LL + 8];
    snprintf(line, sizeof(line), "ERR %s", text);
    if(src_) src_->reply(line);
}

void FwUpdate::cancel(const char* reason){
    if(state_ == State::Failed) return;
    if(state_ == State::Done){
        // 指示を書いた後でも、再起動する前なら消せば書き換わらない
        if(store_->mount()){ store_->clear(); store_->unmount(); }
        message_.assign(reason);
        state_ = State::Failed;
        return;
    }
    fail(reason);
}

void FwUpdate::step(size_t budget){
    switch(state_){
        case State::Header:  stepHeader(); break;
        case State::Confirm:
            if(src_->closed()) fail(TR("相手が接続を切りました"));
            break;
        case State::Payload: stepPayload(budget); break;
        case State::Verify:  stepVerify(budget); break;
        default: break;
    }
}

void FwUpdate::stepHeader(){
    while(prefix_len_ < sizeof(prefix_)){
        const int n = src_->read(prefix_ + prefix_len_, sizeof(prefix_) - prefix_len_);
        if(n < 0){ fail(prefix_len_ == 0 ? TR("更新ファイルが空です") : TR("更新ファイルが短すぎます")); return; }
        if(n == 0) return;
        prefix_len_ += (size_t)n;
    }

    FwImage::Error e = FwImage::Parse(prefix_, FwImage::kHeaderBytes, header_);
    if(e != FwImage::Error::None){ fail(FwImage::ErrorText(e)); return; }

    if(env_.keys) sig_ = FwImage::VerifySignature(prefix_, prefix_ + FwImage::kHeaderBytes, env_.keys, env_.key_count);
    else          sig_ = FwImage::VerifyBuiltin(prefix_, prefix_ + FwImage::kHeaderBytes);
    if(sig_ == FwImage::Sig::Bad){ fail(FwImage::SigText(sig_)); return; }
    const bool unverified = sig_ != FwImage::Sig::Trusted;
    if(unverified && !env_.allow_unsigned){
        fail(sig_ == FwImage::Sig::Unsigned ? TR("署名の無い更新ファイルは受け付けません")
                                            : TR("登録されていない鍵で署名されています"));
        return;
    }

    // 置き場を空けてから、入るかどうかを見る(前の更新の残りがあれば消える)
    if(!store_->mount()){ fail(TR("置き場(内蔵フラッシュ)を開けません")); return; }
    mounted_ = true;
    store_->clear();
    e = FwImage::Check(header_, store_->fsStart(), store_->fsEnd(), store_->capacity());
    if(e != FwImage::Error::None){ fail(FwImage::ErrorText(e)); return; }

    // 確認の理由
    confirm_.clear();
    auto add = [&](const char* t){
        if(!confirm_.empty()) confirm_.append("\n");
        confirm_.append(t);
    };
    if(unverified) add(FwImage::SigText(sig_));
    if(header_.serial < env_.current_serial) add(TR("今より古い版です"));
    else if(header_.serial == env_.current_serial && strcmp(header_.git, env_.current_git) == 0) add(TR("今と同じ版です"));

    if(!confirm_.empty() || env_.always_confirm){
        state_ = State::Confirm;
        message_.assign(TR("確認を待っています"));
        char line[PICO_STR_LL + 8];
        snprintf(line, sizeof(line), "WAIT %s", confirm_.empty() ? TR("本体で確認してください") : confirm_.c_str());
        // 改行を含むと行が分かれるので、TCPへは空白にして送る
        for(char* p = line; *p; ++p) if(*p == '\n') *p = ' ';
        src_->reply(line);
        return;
    }
    beginPayload();
}

void FwUpdate::confirm(bool yes){
    if(state_ != State::Confirm) return;
    if(!yes){ fail(TR("本体で取り消されました")); return; }
    beginPayload();
}

void FwUpdate::beginPayload(){
    if(!store_->openWrite()){ fail(TR("置き場に書き込めません")); return; }
    writing_ = true;
    sha_.reset();
    received_ = 0;
    state_ = State::Payload;
    message_.assign(TR("書き込んでいます"));
    src_->reply("OK");
}

void FwUpdate::stepPayload(size_t budget){
    size_t used = 0;
    while(used < budget && received_ < header_.payload_bytes){
        size_t want = header_.payload_bytes - received_;
        if(want > sizeof(buf_)) want = sizeof(buf_);
        const int n = src_->read(buf_, want);
        if(n < 0){ fail(TR("更新ファイルが途中で切れました")); return; }
        if(n == 0) return;
        sha_.update(buf_, (size_t)n);
        if(!store_->write(buf_, (size_t)n)){ fail(TR("置き場への書き込みに失敗しました(空きが足りない?)")); return; }
        received_ += (uint32_t)n;
        used += (size_t)n;
    }
    if(received_ < header_.payload_bytes) return;

    writing_ = false;
    if(!store_->closeWrite()){ fail(TR("置き場への書き込みに失敗しました")); return; }
    uint8_t digest[32];
    sha_.finish(digest);
    if(memcmp(digest, header_.payload_sha256, 32) != 0){ fail(TR("中身が見出しと合いません(壊れています)")); return; }

    // 書いたものを読み直して確かめる(フラッシュへの書き込みの失敗・化けを見つけるため)
    if(!store_->openRead()){ fail(TR("置き場を読み直せません")); return; }
    reading_ = true;
    sha_.reset();
    verified_ = 0;
    state_ = State::Verify;
    message_.assign(TR("書いた中身を確かめています"));
}

void FwUpdate::stepVerify(size_t budget){
    size_t used = 0;
    bool eof = false;
    while(used < budget){
        const int n = store_->read(buf_, sizeof(buf_));
        if(n < 0){ fail(TR("置き場を読み直せません")); return; }
        if(n == 0){ eof = true; break; }
        if(verified_ + (uint32_t)n > header_.payload_bytes){ fail(TR("置き場の中身が長すぎます")); return; }
        // gzipの頭3バイトと末尾4バイト(解いた大きさ)を拾う
        for(int i = 0; i < n; i++){
            const uint32_t pos = verified_ + (uint32_t)i;
            if(pos < 3) gz_head_[pos] = buf_[i];
            if(pos + 4 >= header_.payload_bytes) gz_tail_[pos + 4 - header_.payload_bytes] = buf_[i];
        }
        sha_.update(buf_, (size_t)n);
        verified_ += (uint32_t)n;
        used += (size_t)n;
    }
    if(!eof) return; // 続きは次のフレーム(終わりの0を読むまで)

    reading_ = false;
    store_->closeRead();
    if(verified_ != header_.payload_bytes){ fail(TR("置き場の中身が短すぎます")); return; }
    uint8_t digest[32];
    sha_.finish(digest);
    if(memcmp(digest, header_.payload_sha256, 32) != 0){ fail(TR("置き場から読み直した中身が合いません")); return; }
    // ローダーはgzipの末尾の大きさだけ書く。見出しの大きさ(=アプリの領域に収まると確かめた値)と同じであること
    if(gz_head_[0] != 0x1f || gz_head_[1] != 0x8b || gz_head_[2] != 0x08){ fail(TR("中身がgzipではありません")); return; }
    if(Le32(gz_tail_) != header_.image_bytes){ fail(TR("中身の大きさが見出しと合いません")); return; }

    char note[96];
    snprintf(note, sizeof(note), "serial=%lu git=%s", (unsigned long)header_.serial, header_.git);
    if(!store_->commit(note)){ fail(TR("書き換えの指示を書けませんでした")); return; }
    store_->unmount();
    mounted_ = false;
    state_ = State::Done;
    message_.assign(TR("準備ができました。再起動して書き換えます"));
    src_->reply("DONE");
}
