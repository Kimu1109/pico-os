#include "apps/recorder/Transcribe_Proto.hpp"
#include "system/I18n_Functions.hpp"

#include <cstdio>
#include <cstring>

void Transcribe::MakeBoundary(char* out, size_t cap, uint32_t seed){
    //英数字だけ。WAVの中身とたまたま一致しても困らないよう長めにする(RFC 2046 は70文字まで)
    static const char kChars[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    uint32_t x = seed ^ 0x9E3779B9u;
    if(x == 0) x = 1;
    const char* prefix = "picoos-";
    size_t n = 0;
    for(const char* p = prefix; *p && n + 1 < cap; p++) out[n++] = *p;
    for(int i = 0; i < 24 && n + 1 < cap; i++){
        //xorshift32
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        out[n++] = kChars[x % 36];
    }
    if(cap > 0) out[n] = '\0';
}

bool Transcribe::SafeFieldValue(const char* s){
    if(!s) return false;
    for(const char* p = s; *p; p++){
        const unsigned char c = (unsigned char)*p;
        if(c < 0x20 || c == 0x7f || c == '"') return false;
    }
    return true;
}

namespace {
    bool AppendField(Transcribe::Head& out, const char* boundary, const char* name, const char* value){
        bool ok = true;
        ok = out.append("--") && ok;
        ok = out.append(boundary) && ok;
        ok = out.append("\r\nContent-Disposition: form-data; name=\"") && ok;
        ok = out.append(name) && ok;
        ok = out.append("\"\r\n\r\n") && ok;
        ok = out.append(value) && ok;
        ok = out.append("\r\n") && ok;
        return ok;
    }
}

bool Transcribe::BuildHead(Head& out, const char* boundary, const Fields& f, const char* filename){
    out.clear();
    if(!boundary || !*boundary || !f.model || !*f.model) return false;
    if(!SafeFieldValue(f.model) || !SafeFieldValue(f.language) || !SafeFieldValue(f.prompt)
       || !SafeFieldValue(filename) || !*filename) return false;
    bool ok = AppendField(out, boundary, "model", f.model);
    if(f.language && *f.language) ok = AppendField(out, boundary, "language", f.language) && ok;
    if(f.prompt && *f.prompt) ok = AppendField(out, boundary, "prompt", f.prompt) && ok;
    ok = AppendField(out, boundary, "response_format", "text") && ok;
    ok = out.append("--") && ok;
    ok = out.append(boundary) && ok;
    ok = out.append("\r\nContent-Disposition: form-data; name=\"file\"; filename=\"") && ok;
    ok = out.append(filename) && ok;
    ok = out.append("\"\r\nContent-Type: audio/wav\r\n\r\n") && ok;
    if(!ok) out.clear();
    return ok;
}

bool Transcribe::BuildTail(Tail& out, const char* boundary){
    out.clear();
    bool ok = out.assign("\r\n--");
    ok = out.append(boundary) && ok;
    ok = out.append("--\r\n") && ok;
    return ok;
}

// ---------------------------------------------------------------------------
// 失敗の理由
// ---------------------------------------------------------------------------

void Transcribe::ErrorParser::begin(){
    reader_.reset(this);
    msg_.clear();
    first_line_.clear();
    first_line_done_ = false;
    in_error_ = false;
    rank_ = 0;
}

void Transcribe::ErrorParser::feed(const void* data, size_t len){
    //本文の1行目(JSONでなかったときの理由)。先頭の空白は飛ばす
    const char* p = (const char*)data;
    for(size_t i = 0; i < len && !first_line_done_; i++){
        const char c = p[i];
        if(c == '\n' || c == '\r'){
            if(!first_line_.empty()) first_line_done_ = true;
            continue;
        }
        if(first_line_.empty() && (c == ' ' || c == '\t')) continue;
        if(!first_line_.append(&c, 1)) first_line_done_ = true;
    }
    if(!reader_.failed()) reader_.feed(data, len);
}

const char* Transcribe::ErrorParser::message() const {
    if(!msg_.empty()) return msg_.c_str();
    //JSONとして読めたのに理由が無かったなら、本文をそのまま見せても役に立たない
    if(reader_.complete()) return "";
    return first_line_.c_str();
}

void Transcribe::ErrorParser::onBegin(const char* key, bool is_array, int depth){
    if(!is_array && depth == 2 && key && strcmp(key, "error") == 0) in_error_ = true;
}

void Transcribe::ErrorParser::onEnd(bool is_array, int depth){
    //depth は閉じた入れ物の中の深さ("error": {…} を閉じたら2)
    if(!is_array && depth == 2) in_error_ = false;
}

void Transcribe::ErrorParser::onValue(const char* key, JsonReader::Type type, const char* text, size_t,
                                      bool, int depth){
    if(type != JsonReader::Type::String || !key) return;
    uint8_t rank = 0;
    if(in_error_ && depth == 2 && strcmp(key, "message") == 0) rank = 4;       // {"error":{"message":…}}
    else if(depth == 1 && strcmp(key, "error") == 0) rank = 3;                  // {"error":"…"}
    else if(depth == 1 && strcmp(key, "detail") == 0) rank = 2;
    else if(depth == 1 && strcmp(key, "message") == 0) rank = 1;
    if(rank > rank_){
        rank_ = rank;
        msg_.assign(text);
    }
}

void Transcribe::DescribeFailure(char* out, size_t cap, int status, const char* reason){
    const char* what;
    switch(status){
        case 400: what = TR("受け付けられませんでした"); break;
        case 401: what = TR("APIキーが違います"); break;
        case 403: what = TR("使う権限がありません"); break;
        case 404: what = TR("送り先が見つかりません(api / model を確認)"); break;
        case 413: what = TR("録音が大きすぎます"); break;
        case 429: what = TR("回数か残高の上限です"); break;
        default:  what = status >= 500 ? TR("相手のサーバの不調です") : TR("失敗しました"); break;
    }
    if(reason && *reason) snprintf(out, cap, "%s(HTTP %d: %s)", what, status, reason);
    else snprintf(out, cap, "%s(HTTP %d)", what, status);
}
