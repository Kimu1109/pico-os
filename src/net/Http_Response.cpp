#include "net/Http_Response.hpp"

#include <cstdlib>
#include <cstring>

namespace {
    //前後の空白を落としつつ、先頭を指すポインタを返す(末尾はNULを詰める)
    char* trim(char* s){
        while(*s == ' ' || *s == '\t') s++;
        size_t n = strlen(s);
        while(n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = '\0';
        return s;
    }

    //ヘッダ名の比較。HTTPのヘッダ名は大小を区別しない
    bool headerIs(const char* line, const char* name){
        const size_t n = strlen(name);
        if(strncasecmp(line, name, n) != 0) return false;
        return line[n] == ':';
    }

    //"Name: value" の value 側を返す
    char* headerValue(char* line){
        char* colon = strchr(line, ':');
        if(!colon) return nullptr;
        return trim(colon + 1);
    }
}

void HttpResponse::reset(IHttpSink* s){
    state = State::Status;
    err = HttpTools::Error::None;
    sink = s;

    line_len = 0;
    line_overflow = false;
    line[0] = '\0';

    status_code = 0;
    content_length = -1;
    body_bytes = 0;

    validator_.clear();
    location_.clear();
    has_etag = false;
}

bool HttpResponse::fail(HttpTools::Error e){
    state = State::Failed;
    err = e;
    return false;
}

bool HttpResponse::pushLineChar(char c){
    if(line_len >= kMaxLineLen - 1){
        //長すぎる行は捨てるが、行の終わりまでは読み飛ばす必要がある
        line_overflow = true;
        return true;
    }
    line[line_len++] = c;
    return true;
}

bool HttpResponse::handleStatusLine(){
    //"HTTP/1.1 200 OK" の 200 を取る
    if(strncmp(line, "HTTP/", 5) != 0) return fail(HttpTools::Error::BadStatusLine);

    const char* space = strchr(line, ' ');
    if(!space) return fail(HttpTools::Error::BadStatusLine);

    while(*space == ' ') space++;
    if(*space < '0' || *space > '9') return fail(HttpTools::Error::BadStatusLine);

    status_code = (int)strtol(space, nullptr, 10);
    if(status_code < 100 || status_code > 599) return fail(HttpTools::Error::BadStatusLine);

    state = State::Headers;
    return true;
}

bool HttpResponse::handleHeaderLine(){
    //空行 = ヘッダの終わり
    if(line_len == 0){
        //本文を持たない応答は、ここで終わり
        if(status_code == 304 || status_code == 204 || content_length == 0){
            state = State::Done;
        }else{
            state = State::Body;
        }
        return true;
    }

    if(headerIs(line, "Content-Length")){
        char* value = headerValue(line);
        if(value){
            const long n = strtol(value, nullptr, 10);
            content_length = (n < 0) ? -1 : (int32_t)n;
        }
    }else if(headerIs(line, "Transfer-Encoding")){
        //PROTOCOL.mdでchunkedを禁止しているのは、ここを実装しないため。
        //黙って本文としてchunkのサイズ行まで書き込むと、壊れたファイルが
        //「正常なキャッシュ」として残ってしまう
        return fail(HttpTools::Error::Chunked);
    }else if(headerIs(line, "ETag")){
        char* value = headerValue(line);
        if(value){
            //弱い検証子("W/\"abc\"")の W/ と引用符は落として中身だけ持つ
            if(strncmp(value, "W/", 2) == 0) value += 2;
            if(*value == '"'){
                value++;
                char* end = strchr(value, '"');
                if(end) *end = '\0';
            }
            validator_.assign(value);
            has_etag = true;
        }
    }else if(headerIs(line, "Last-Modified")){
        //ETagがあるならそちらを優先する(時計に依存しないため)
        if(!has_etag){
            char* value = headerValue(line);
            if(value) validator_.assign(value);
        }
    }else if(headerIs(line, "Location")){
        char* value = headerValue(line);
        if(value) location_.assign(value);
    }

    return true;
}

bool HttpResponse::consumeBody(const uint8_t* data, size_t len, size_t& consumed){
    consumed = len;

    if(content_length >= 0){
        const uint32_t remaining = (uint32_t)content_length - body_bytes;
        if((uint32_t)len > remaining) consumed = remaining;
    }

    if(consumed > 0){
        if(sink && !sink->write(data, consumed)) return fail(HttpTools::Error::SinkFailed);
        body_bytes += (uint32_t)consumed;
    }

    if(content_length >= 0 && body_bytes >= (uint32_t)content_length){
        state = State::Done;
    }
    return true;
}

bool HttpResponse::feed(const void* data, size_t len){
    if(state == State::Failed) return false;
    if(state == State::Done) return true; //余分に届いた分は捨てる
    if(!data || len == 0) return true;

    const uint8_t* p = (const uint8_t*)data;
    size_t i = 0;

    while(i < len){
        if(state == State::Body){
            size_t consumed = 0;
            if(!consumeBody(p + i, len - i, consumed)) return false;
            i += consumed;
            if(state != State::Body) break; //本文を読み切った
            continue;
        }

        if(state != State::Status && state != State::Headers) break;

        const char c = (char)p[i++];

        if(c == '\r') continue; //CRは捨てる(CRLF/LFどちらでも同じに扱う)
        if(c != '\n'){
            pushLineChar(c);
            continue;
        }

        //行が揃った
        if(line_overflow) return fail(HttpTools::Error::LineTooLong);
        line[line_len] = '\0';

        const bool ok = (state == State::Status) ? handleStatusLine() : handleHeaderLine();

        line_len = 0;
        line_overflow = false;
        if(!ok) return false;
    }

    return true;
}

bool HttpResponse::finish(){
    if(state == State::Failed) return false;
    if(state == State::Done) return true;

    //Content-Length未指定なら、接続が閉じた時点が本文の終わり
    if(state == State::Body && content_length < 0){
        state = State::Done;
        return true;
    }

    //長さを宣言しておきながら足りないまま切れた = 途中で切れている。
    //キャッシュへ書かせないよう失敗にする
    if(state == State::Body) return fail(HttpTools::Error::Truncated);

    //ヘッダの途中で切れた
    return fail(HttpTools::Error::Truncated);
}
