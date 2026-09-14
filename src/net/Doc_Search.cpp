#include "net/Doc_Search.hpp"

#include "functions/Log_Functions.hpp"

#include <cstring>

bool DocSearch::Sink::write(const void* data, size_t len){
    if(!owner) return true;

    const char* p = (const char*)data;
    for(size_t i = 0; i < len; i++){
        const char c = p[i];

        if(c == '\n'){
            owner->line_[owner->line_len_] = '\0';
            owner->takeLine();
            owner->line_len_ = 0;
            continue;
        }
        if(c == '\r') continue;

        //入りきらないぶんは捨てる。必要な2列は行の先頭にあるので、
        //ここで切れて困るのは使っていないsnippetだけ
        if(owner->line_len_ + 1 < sizeof(owner->line_)){
            owner->line_[owner->line_len_++] = c;
        }
    }
    return true;
}

void DocSearch::takeLine(){
    if(count_ >= kMaxHits) return;
    if(line_len_ == 0) return;   //空行(一致が無い場合は本文が空で返る)
    if(line_[0] == '#') return;  //注釈行

    //1列目 = path / 2列目 = title。3列目以降は使わない
    char* tab = strchr(line_, '\t');
    if(!tab) return;             //パスだけの行は表示しようがない
    *tab = '\0';

    const char* path = line_;
    char* title = tab + 1;
    char* title_end = strchr(title, '\t');
    if(title_end) *title_end = '\0';

    //サーバ絶対パスでなければ何を基準に解決するか決まらない(PROTOCOL.md)
    if(path[0] != '/') return;

    SearchHit& out = hits_[count_];

    //パスが切れると別の文書を指してしまうので、収まらない行は捨てる
    if(!out.path.assign(path)) return;

    //タイトルは表示専用。画面幅の都合でどのみち切れるので、
    //ここで切り詰まっても構わない(戻り値を見ないのはそのため)
    out.title.assign(title[0] != '\0' ? title : path);

    count_++;
}

bool DocSearch::begin(const Url& server, const char* search_path, const char* query, int offset){
    this->cancel();

    offset_ = (offset > 0) ? offset : 0;

    if(!search_path || search_path[0] == '\0'){
        finish(State::Failed, "このサーバは検索に対応していません");
        return false;
    }
    if(!query || query[0] == '\0'){
        finish(State::Failed, "検索語が空です");
        return false;
    }

    Url target = server;
    target.query.clear();
    if(!target.path.assign(search_path)){
        finish(State::Failed, "検索URLが長すぎます");
        return false;
    }

    //検索語は日本語なので、そのままではリクエストラインへ書けない
    FixedString<PICO_STR_LL> encoded;
    if(!UrlTools::EncodeComponent(encoded, query)){
        finish(State::Failed, "検索語が長すぎます");
        return false;
    }

    bool ok = true;
    ok = target.query.assign("q=") && ok;
    ok = target.query.append(encoded) && ok;
    ok = target.query.appendFormat("&limit=%d&offset=%d", kMaxHits, offset_) && ok;
    if(!ok){
        finish(State::Failed, "検索語が長すぎます");
        return false;
    }

    sink.owner = this;
    if(!http.begin(target, &sink)){
        finish(State::Failed, http.failureToStr());
        return false;
    }

    state_ = State::Fetching;
    message_ = "";
    return true;
}

void DocSearch::update(){
    if(state_ != State::Fetching) return;

    http.update();

    const TaskTools::Status s = http.getStatus();
    if(s == TaskTools::PROCESSING) return;

    if(s == TaskTools::FAILED){
        //検索非対応のサーバがdiscoveryにだけsearchを書いている、という取り違えを
        //見分けられるよう、状態コードが取れていればそれを出す
        const int code = http.response().statusCode();
        if(code >= 400){
            static char buf[48];
            snprintf(buf, sizeof(buf), "検索に失敗しました (%d)", code);
            LOG_SYS_WARN("DocSearch: %s", buf);
            finish(State::Failed, buf);
        }else{
            LOG_SYS_WARN("DocSearch: %s", http.failureToStr());
            finish(State::Failed, http.failureToStr());
        }
        return;
    }

    //最後の行が改行で終わっていない応答も拾う
    if(line_len_ > 0){
        line_[line_len_] = '\0';
        this->takeLine();
        line_len_ = 0;
    }

    finish(State::Ready, "");
}

void DocSearch::finish(State next, const char* message){
    state_ = next;
    message_ = message ? message : "";
}

void DocSearch::cancel(){
    http.cancel();
    sink.owner = nullptr;

    count_ = 0;
    line_len_ = 0;
    state_ = State::Idle;
    message_ = "";
}
