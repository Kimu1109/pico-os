#include "task/Http_Request.hpp"
#include "functions/Log_Functions.hpp"
#include "Arduino.h"

#include <cstdio>

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

bool HttpRequest::begin(const Url& target, Method method, IHttpSink* sink,
                         const void* body, size_t body_len, const char* content_type){
    this->cancel();

    url = target;
    method_ = method;
    sink_ = sink;
    body_ = (body && body_len > 0) ? body : nullptr;
    body_len_ = body_ ? body_len : 0;

    content_type_.clear();
    if(content_type && *content_type){
        content_type_.assign(content_type);
    }

    redirects = 0;
    fail_ = Fail::None;
    status = TaskTools::PROCESSING;
    started_ms = millis();

    return startRequest();
}

bool HttpRequest::startRequest(){
    if(url.secure){
        finishWith(TaskTools::FAILED, Fail::NotHttp);
        return false;
    }

    // Http_Getと違いゲートを挟まない: statusCodeによらずsink_へ本文を渡す
    res.reset(sink_);

    client.stop();
    client.setTimeout(kConnectTimeoutMs);

    phase = Phase::Connecting;
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
    //圧縮させない(展開の手段が無い)。Connection: closeで応答後に閉じてもらう
    ok = req.append("Connection: close\r\n") && ok;

    if(body_ && body_len_ > 0){
        if(!content_type_.empty()){
            ok = req.append("Content-Type: ") && ok;
            ok = req.append(content_type_) && ok;
            ok = req.append("\r\n") && ok;
        }
        char len_buf[16];
        snprintf(len_buf, sizeof(len_buf), "%u", (unsigned)body_len_);
        ok = req.append("Content-Length: ") && ok;
        ok = req.append(len_buf) && ok;
        ok = req.append("\r\n") && ok;
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

void HttpRequest::finishWith(TaskTools::Status s, Fail f){
    phase = Phase::Ended;
    fail_ = f;
    status = s;
    client.stop();
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

    if(millis() - started_ms > kTimeoutMs){
        finishWith(TaskTools::FAILED, Fail::Timeout);
        return;
    }

    if(phase == Phase::Connecting){
        //ここだけ同期的に待つ(Http_Getと同じ制約)
        if(client.connect(url.host.c_str(), url.port) != 1){
            LOG_SYS_WARN("HttpRequest: 接続できません (%s:%u)", url.host.c_str(), (unsigned)url.port);
            finishWith(TaskTools::FAILED, Fail::ConnectFailed);
            return;
        }
        phase = Phase::Sending;
        return; //送信は次のフレームへ回してフレーム時間をならす
    }

    if(phase == Phase::Sending){
        if(!sendRequestLine()){
            finishWith(TaskTools::FAILED, Fail::SendFailed);
            return;
        }
        phase = Phase::Receiving;
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

        if(!res.feed(buf, (size_t)got)){
            finishWith(TaskTools::FAILED, Fail::Response);
            return;
        }

        if(res.isDone()) break;
    }

    if(res.isDone()){
        //GETだけリダイレクトを自動で追う。POST等はボディの再送を一律には決められない
        //ため、3xxが返ってきてもそのまま呼び出し側(Luaスクリプト)へ渡す
        if(method_ == Method::GET && res.isRedirect()){
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
        if(!res.finish()){
            finishWith(TaskTools::FAILED, Fail::Response);
            return;
        }
        if(method_ == Method::GET && res.isRedirect()){
            followRedirect();
            return;
        }
        finishWith(TaskTools::SUCCESS, Fail::None);
    }
}

void HttpRequest::cancel(){
    client.stop();
    phase = Phase::Idle;
    fail_ = Fail::None;
    redirects = 0;
    sink_ = nullptr;
    body_ = nullptr;
    body_len_ = 0;
    status = TaskTools::PROCESSING;
}

const char* HttpRequest::failureToStr() const {
    switch(fail_){
        case Fail::None:             return "なし";
        case Fail::NotHttp:          return "httpsは未対応";
        case Fail::ConnectFailed:    return "接続できない";
        case Fail::SendFailed:       return "リクエストを送れない";
        case Fail::Timeout:          return "応答がない";
        case Fail::TooManyRedirects: return "リダイレクトが多すぎる";
        case Fail::BadRedirect:      return "転送先が不正";
        case Fail::Response:         return HttpTools::ErrorToStr(res.error());
        default:                     return "不明";
    }
}
