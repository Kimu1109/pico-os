#include "net/Http_Request.hpp"
#include "system/Log_Functions.hpp"
#include "Arduino.h"

#include <cstdio>
#include <cstring>
#include <strings.h>

const char* HttpRequest::MethodToStr(Method m){
    switch(m){
        case Method::GET:    return "GET";
        case Method::POST:   return "POST";
        case Method::PUT:    return "PUT";
        case Method::PATCH:  return "PATCH";
        case Method::Delete: return "DELETE";
        default:             return "GET";
    }
}

bool HttpRequest::SameEndpoint(const Url& a, const Url& b){
    return a.secure == b.secure && a.port == b.port && strcasecmp(a.host.c_str(), b.host.c_str()) == 0;
}

void HttpRequest::setKeepAlive(bool on){
    keep_alive_ = on;
    if(!on && phase != Phase::Connecting && phase != Phase::Sending && phase != Phase::SendingBody
       && phase != Phase::Receiving){
        closeConnection();
    }
}

bool HttpRequest::addExtraHeader(const char* name, const char* value){
    if(!name || !*name || !value) return false;
    //名前はtoken文字だけ、値は制御文字(HTAB以外)を含まない。区切りを混ぜられると別のヘッダや別の要求を差し込める
    for(const char* p = name; *p; p++){
        const unsigned char c = (unsigned char)*p;
        const bool tok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
            || c == '-' || c == '_' || c == '.';
        if(!tok) return false;
    }
    for(const char* p = value; *p; p++){
        const unsigned char c = (unsigned char)*p;
        if((c < 0x20 && c != '\t') || c == 0x7f) return false;
    }
    //1行は送るときの溜め場所(512B)に収まること。全体は extra_header_ に収まること。
    //先に長さを数えてから足す(途中で溢れて半端な行が残らないように。大きな一時領域をスタックに取らない)
    const size_t line = strlen(name) + 2 + strlen(value);
    if(line + 2 >= kMaxHeaderLineBytes) return false;
    const size_t need = extra_header_.length() + (extra_header_.empty() ? 0 : 2) + line;
    if(need >= PICO_STR_1KiB) return false;
    if(!extra_header_.empty()) extra_header_.append("\r\n");
    extra_header_.append(name);
    extra_header_.append(": ");
    extra_header_.append(value);
    return true;
}

bool HttpRequest::setExtraHeader(const char* line){
    extra_header_.clear();
    if(!line || !*line) return true;
    //ヘッダの区切りを混ぜられると別のヘッダ(や別の要求)を差し込めてしまう
    for(const char* p = line; *p; p++){
        if(*p == '\r' || *p == '\n') return false;
    }
    if(!extra_header_.assign(line)){
        extra_header_.clear();
        return false;
    }
    return true;
}

void HttpRequest::closeConnection(){
    client.close();
    conn_reusable_ = false;
}

bool HttpRequest::begin(const Url& target, Method method, IHttpSink* sink,
                         const void* body, size_t body_len, const char* content_type){
    const bool reuse = beginCommon(target, method, sink, content_type);
    body_ = (body && body_len > 0) ? body : nullptr;
    body_len_ = body_ ? body_len : 0;
    return startRequest(reuse);
}

bool HttpRequest::beginStream(const Url& target, Method method, IHttpSink* sink,
                              IHttpBodySource* source, const char* content_type){
    const bool reuse = beginCommon(target, method, sink, content_type);
    body_src_ = (source && source->size() > 0) ? source : nullptr;
    return startRequest(reuse);
}

bool HttpRequest::beginCommon(const Url& target, Method method, IHttpSink* sink, const char* content_type){
    //前の応答の後に持っておいた接続が、同じ相手へそのまま使えるか。
    //受信待ちのバイトが残っている接続は、前の応答との境目が狂っているので使わない
    const bool reuse = keep_alive_ && conn_reusable_ && phase == Phase::Ended
                    && SameEndpoint(conn_url_, target)
                    && client.connected() && client.available() <= 0;
    if(reuse){
        conn_reusable_ = false;
        phase = Phase::Idle;
    }else{
        this->cancel();
    }

    url = target;
    method_ = method;
    sink_ = sink;
    body_ = nullptr;
    body_len_ = 0;
    body_src_ = nullptr;
    body_sent_ = 0;

    content_type_.clear();
    if(content_type && *content_type){
        content_type_.assign(content_type);
    }

    redirects = 0;
    fail_ = Fail::None;
    status = TaskTools::PROCESSING;
    started_ms = millis();
    last_activity_ms = started_ms;
    return reuse;
}

bool HttpRequest::startRequest(bool reuse){
    // Http_Getと違いゲートを挟まない: statusCodeによらずsink_へ本文を渡す
    res.reset(sink_);
    got_bytes_ = false;
    conn_reusable_ = false;
    //流す本文は送り直すたびに頭から読む
    if(body_src_ && body_sent_ > 0){
        if(!body_src_->rewind()){
            finishWith(TaskTools::FAILED, Fail::BodyRead);
            return false;
        }
    }
    body_sent_ = 0;

    if(reuse){
        reused_ = true;
        phase = Phase::Sending;
        return true;
    }

    reused_ = false;
    client.close();

    phase = Phase::Connecting;
    return true;
}

bool HttpRequest::retryOnFreshConnection(){
    if(!reused_) return false;
    LOG_SYS_MSG("HttpRequest: 使い回した接続が切れていたので接続し直します (%s)", url.host.c_str());
    startRequest(false);
    return true;
}

bool HttpRequest::sendRequestLine(){
    FixedString<PICO_STR_256B> target;
    FixedString<PICO_STR_M> hostHeader;
    if(!UrlTools::RequestTarget(target, url)) return false;
    if(!UrlTools::HostHeader(hostHeader, url)) return false;

    FixedString<PICO_STR_512B> req;
    bool ok = true;
    ok = req.assign(MethodToStr(method_)) && ok;
    ok = req.append(" ") && ok;
    ok = req.append(target) && ok;
    ok = req.append(" HTTP/1.1\r\nHost: ") && ok;
    ok = req.append(hostHeader) && ok;
    ok = req.append("\r\nUser-Agent: pico-os/1\r\n") && ok;
    //圧縮させない(展開の手段が無い)。使い回さないならConnection: closeで応答後に閉じてもらう
    ok = req.append(keep_alive_ ? "Connection: keep-alive\r\n" : "Connection: close\r\n") && ok;
    if(!ok) return false;

    //足されたヘッダは1行ずつ。reqに収まらなければそこまでを送ってから続ける(スタックに大きな領域を取らないため)
    for(const char* p = extra_header_.c_str(); *p; ){
        const char* end = strstr(p, "\r\n");
        const size_t n = end ? (size_t)(end - p) : strlen(p);
        if(req.length() + n + 2 >= PICO_STR_512B){
            if(client.write((const uint8_t*)req.c_str(), req.length()) != req.length()) return false;
            req.clear();
            if(n + 2 >= kMaxHeaderLineBytes) return false;
        }
        ok = req.append(p, n) && ok;
        ok = req.append("\r\n") && ok;
        p += n;
        if(end) p += 2;
    }

    //Content-Type/Content-Length/終端の空行が入る余地(content_type_は96B未満。合わせて160B以内)を残す
    if(req.length() + 160 >= PICO_STR_512B){
        if(client.write((const uint8_t*)req.c_str(), req.length()) != req.length()) return false;
        req.clear();
    }

    const size_t body_total = body_src_ ? body_src_->size() : body_len_;
    if(body_total > 0){
        if(!content_type_.empty()){
            ok = req.append("Content-Type: ") && ok;
            ok = req.append(content_type_) && ok;
            ok = req.append("\r\n") && ok;
        }
        char len_buf[24];
        snprintf(len_buf, sizeof(len_buf), "%lu", (unsigned long)body_total);
        ok = req.append("Content-Length: ") && ok;
        ok = req.append(len_buf) && ok;
        ok = req.append("\r\n") && ok;
    }else if(method_ == Method::POST || method_ == Method::PUT || method_ == Method::PATCH){
        //本文の無いPOST/PUT/PATCHにも長さを付ける。無いと411(Length Required)を返すサーバがある
        //(SpotifyのWeb APIの再生/次へ等)。本文を持つメソッドなので0と明示しても害は無い
        ok = req.append("Content-Length: 0\r\n") && ok;
    }
    ok = req.append("\r\n") && ok;

    if(!ok) return false;

    const size_t len = req.length();
    if(client.write((const uint8_t*)req.c_str(), len) != len) return false;

    if(body_ && body_len_ > 0){
        if(client.write((const uint8_t*)body_, body_len_) != body_len_) return false;
    }
    return true;
}

void HttpRequest::sendBodyChunk(){
    uint8_t buf[1024];
    const size_t total = body_src_->size();
    size_t sent_now = 0;
    while(body_sent_ < total && sent_now < kSendPerUpdate){
        //送り終える前に相手が応答してきた(401や413で読まずに閉じる等)。その応答を返す
        if(client.available() > 0){
            phase = Phase::Receiving;
            return;
        }
        size_t want = total - body_sent_;
        if(want > sizeof(buf)) want = sizeof(buf);
        const int got = body_src_->read(buf, want);
        if(got <= 0){
            LOG_SYS_WARN("HttpRequest: 送る本文を読めません (%lu/%lu)", (unsigned long)body_sent_, (unsigned long)total);
            finishWith(TaskTools::FAILED, Fail::BodyRead);
            return;
        }
        if(client.write(buf, (size_t)got) != (size_t)got){
            //相手が先に閉じたなら、届いている応答を読む
            if(client.available() > 0){
                phase = Phase::Receiving;
                return;
            }
            finishWith(TaskTools::FAILED, Fail::SendFailed);
            return;
        }
        body_sent_ += (size_t)got;
        sent_now += (size_t)got;
        last_activity_ms = millis();
    }
    if(body_sent_ >= total) phase = Phase::Receiving;
}

void HttpRequest::finishWith(TaskTools::Status s, Fail f){
    phase = Phase::Ended;
    fail_ = f;
    status = s;
    //成功して、相手も続けてよいと言っているときだけ接続を持っておく
    conn_reusable_ = keep_alive_ && s == TaskTools::SUCCESS && res.canReuseConnection();
    if(conn_reusable_){
        conn_url_ = url;
    }else{
        client.close();
    }
}

bool HttpRequest::followRedirect(){
    if(redirects >= kMaxRedirects){
        finishWith(TaskTools::FAILED, Fail::TooManyRedirects);
        return false;
    }
    if(res.location().empty()){
        finishWith(TaskTools::FAILED, Fail::BadRedirect);
        return false;
    }

    Url next;
    if(!UrlTools::Resolve(next, url, res.location().c_str())){
        finishWith(TaskTools::FAILED, Fail::BadRedirect);
        return false;
    }

    redirects++;
    url = next;
    return startRequest();
}

void HttpRequest::update(){
    if(phase == Phase::Idle || phase == Phase::Ended) return;

    {
        const unsigned long now = millis();
        const bool timed_out = idle_timeout_
            ? (now - last_activity_ms > timeout_ms_ || now - started_ms > kLongTransferMaxMs)
            : (now - started_ms > timeout_ms_);
        if(timed_out){
            finishWith(TaskTools::FAILED, Fail::Timeout);
            return;
        }
    }

    if(phase == Phase::Connecting){
        //ここだけ同期的に待つ(Http_Getと同じ制約)
        if(!client.connect(url, kConnectTimeoutMs)){
            LOG_SYS_WARN("HttpRequest: 接続できません (%s:%u, %s)",
                         url.host.c_str(), (unsigned)url.port, client.errorText());
            switch(client.error()){
                case HttpTransport::Error::ClockNotSet: finishWith(TaskTools::FAILED, Fail::ClockNotSet); break;
                case HttpTransport::Error::Tls:
                case HttpTransport::Error::NoMemory:    finishWith(TaskTools::FAILED, Fail::TlsFailed); break;
                default:                                finishWith(TaskTools::FAILED, Fail::ConnectFailed); break;
            }
            return;
        }
        phase = Phase::Sending;
        return; //送信は次のフレームへ回してフレーム時間をならす
    }

    if(phase == Phase::Sending){
        if(!sendRequestLine()){
            if(retryOnFreshConnection()) return;
            finishWith(TaskTools::FAILED, Fail::SendFailed);
            return;
        }
        phase = body_src_ ? Phase::SendingBody : Phase::Receiving;
        return;
    }

    if(phase == Phase::SendingBody){
        sendBodyChunk();
        return;
    }

    // ---- 受信 ----
    uint8_t buf[256];
    size_t readTotal = 0;

    while(readTotal < kReadPerUpdate){
        const int avail = client.available();
        if(avail <= 0) break;

        size_t want = sizeof(buf);
        if((size_t)avail < want) want = (size_t)avail;
        if(readTotal + want > kReadPerUpdate) want = kReadPerUpdate - readTotal;

        const int got = client.read(buf, want);
        if(got <= 0) break;

        readTotal += (size_t)got;
        got_bytes_ = true;
        last_activity_ms = millis();

        if(!res.feed(buf, (size_t)got)){
            finishWith(TaskTools::FAILED, Fail::Response);
            return;
        }

        if(res.isDone()) break;
    }

    if(res.isDone()){
        //GETだけリダイレクトを自動で追う。POST等はボディの再送を一律には決められない
        //ため、3xxが返ってきてもそのまま呼び出し側(Luaスクリプト)へ渡す
        if(method_ == Method::GET && follow_redirects_ && res.isRedirect()){
            followRedirect();
            return;
        }
        //Http_Getと違い、ここでの成功/失敗は「応答を最後まで読めたか」だけを見る。
        //ステータスコードの意味(2xx/4xx/5xx等)は呼び出し側の判断に委ねる
        finishWith(TaskTools::SUCCESS, Fail::None);
        return;
    }

    //相手が閉じた かつ 読むものが無い = 応答の終わり
    if(!client.connected() && client.available() <= 0){
        //使い回した接続で1バイトも来ないまま閉じた = 送る前から死んでいた
        if(!got_bytes_ && retryOnFreshConnection()) return;
        if(!res.finish()){
            finishWith(TaskTools::FAILED, Fail::Response);
            return;
        }
        if(method_ == Method::GET && follow_redirects_ && res.isRedirect()){
            followRedirect();
            return;
        }
        finishWith(TaskTools::SUCCESS, Fail::None);
    }
}

void HttpRequest::cancel(){
    client.close();
    conn_reusable_ = false;
    reused_ = false;
    phase = Phase::Idle;
    fail_ = Fail::None;
    redirects = 0;
    sink_ = nullptr;
    body_ = nullptr;
    body_len_ = 0;
    body_src_ = nullptr;
    body_sent_ = 0;
    status = TaskTools::PROCESSING;
}

const char* HttpRequest::failureToStr() const {
    switch(fail_){
        case Fail::None:             return "なし";
        case Fail::ClockNotSet:      return "時計が合っていない(NTP同期前)";
        case Fail::TlsFailed:        return client.errorText();
        case Fail::ConnectFailed:    return "接続できない";
        case Fail::SendFailed:       return "リクエストを送れない";
        case Fail::BodyRead:         return "送るファイルを読めない";
        case Fail::Timeout:          return "応答がない";
        case Fail::TooManyRedirects: return "リダイレクトが多すぎる";
        case Fail::BadRedirect:      return "転送先が不正";
        case Fail::Response:         return HttpTools::ErrorToStr(res.error());
        default:                     return "不明";
    }
}
