#include "task/Http_Get.hpp"
#include "functions/Log_Functions.hpp"
#include "Arduino.h"

#include <cstring>

namespace {
    // 検証子がHTTP-dateかETagかを見分ける。
    //
    // Doc_Cacheの目録は値だけを持ち「どちらのヘッダで来たか」を覚えていないため、
    // 形で判断する。HTTP-dateは必ず "GMT" で終わり、ETagにこの綴りが現れることは
    // 実質無いので、これで足りる。
    // (PROTOCOL.mdがETagを推奨しているのは、そもそもこの曖昧さと、
    //  クライアントの時計がNTP同期前には当てにならないため)
    bool looksLikeHttpDate(const char* v){
        return v && strstr(v, "GMT") != nullptr;
    }
}

bool HttpGet::begin(const Url& target, IHttpSink* sink, const char* validator_in){
    this->cancel();

    url = target;
    sink_ = sink;
    gate.attach(&res, sink);

    validator.clear();
    if(validator_in && *validator_in){
        //収まらない検証子は送らない。切り詰めて送ると常に不一致になり、
        //毎回まるごと取り直すことになる
        if(!validator.assign(validator_in)) validator.clear();
    }

    redirects = 0;
    fail_ = Fail::None;
    status = TaskTools::PROCESSING;
    started_ms = millis();

    return startRequest();
}

bool HttpGet::startRequest(){
    //httpsは接続する手前で弾く。URLのschemeを見て判断できるのが
    //Url型を用意した理由(各所でstrncmpしない)
    if(url.secure){
        finishWith(TaskTools::FAILED, Fail::NotHttp);
        return false;
    }

    res.reset(&gate);
    //リダイレクトで作り直したresへ繋ぎ直す
    gate.attach(&res, sink_);

    client.stop();
    client.setTimeout(kConnectTimeoutMs);

    phase = Phase::Connecting;
    return true;
}

bool HttpGet::sendRequestLine(){
    FixedString<PICO_STR_256B> target;
    FixedString<PICO_STR_M> hostHeader;
    if(!UrlTools::RequestTarget(target, url)) return false;
    if(!UrlTools::HostHeader(hostHeader, url)) return false;

    FixedString<PICO_STR_512B> req;
    bool ok = true;
    ok = req.assign("GET ") && ok;
    ok = req.append(target) && ok;
    ok = req.append(" HTTP/1.1\r\nHost: ") && ok;
    ok = req.append(hostHeader) && ok;
    ok = req.append("\r\nUser-Agent: pico-os/1\r\n") && ok;
    //圧縮させない(展開の手段が無い)。Connection: closeで応答後に閉じてもらう
    ok = req.append("Connection: close\r\n") && ok;

    if(!validator.empty()){
        if(looksLikeHttpDate(validator.c_str())){
            ok = req.append("If-Modified-Since: ") && ok;
            ok = req.append(validator) && ok;
        }else{
            ok = req.append("If-None-Match: \"") && ok;
            ok = req.append(validator) && ok;
            ok = req.append("\"") && ok;
        }
        ok = req.append("\r\n") && ok;
    }
    ok = req.append("\r\n") && ok;

    if(!ok) return false;

    const size_t len = req.length();
    return client.write((const uint8_t*)req.c_str(), len) == len;
}

void HttpGet::finishWith(TaskTools::Status s, Fail f){
    phase = Phase::Ended;
    fail_ = f;
    status = s;
    client.stop();
}

bool HttpGet::followRedirect(){
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

void HttpGet::update(){
    if(phase == Phase::Idle || phase == Phase::Ended) return;

    //全体の打ち切り。リダイレクトを跨いでも起点は動かさない
    if(millis() - started_ms > kTimeoutMs){
        finishWith(TaskTools::FAILED, Fail::Timeout);
        return;
    }

    if(phase == Phase::Connecting){
        //ここだけ同期的に待つ(クラスのコメント参照)
        if(client.connect(url.host.c_str(), url.port) != 1){
            LOG_SYS_WARN("HttpGet: 接続できません (%s:%u)", url.host.c_str(), (unsigned)url.port);
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
        if(res.isRedirect()){
            followRedirect();
            return;
        }
        //200と304は成功。それ以外(404/5xx)は呼び出し側がstatusCode()で判断する
        const bool ok = (res.statusCode() == 200 || res.statusCode() == 304);
        finishWith(ok ? TaskTools::SUCCESS : TaskTools::FAILED, Fail::None);
        return;
    }

    //相手が閉じた かつ 読むものが無い = 応答の終わり
    if(!client.connected() && client.available() <= 0){
        if(!res.finish()){
            finishWith(TaskTools::FAILED, Fail::Response);
            return;
        }
        if(res.isRedirect()){
            followRedirect();
            return;
        }
        const bool ok = (res.statusCode() == 200 || res.statusCode() == 304);
        finishWith(ok ? TaskTools::SUCCESS : TaskTools::FAILED, Fail::None);
    }
}

void HttpGet::cancel(){
    client.stop();
    phase = Phase::Idle;
    fail_ = Fail::None;
    redirects = 0;
    gate.detach();
    sink_ = nullptr;
    status = TaskTools::PROCESSING;
}

const char* HttpGet::failureToStr() const {
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
