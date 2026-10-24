#include "net/Setup_Portal.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
    char Lower(char c){ return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

    bool StartsWithNoCase(const char* s, const char* prefix){
        for(; *prefix; s++, prefix++) if(Lower(*s) != *prefix) return false;
        return true;
    }

    int HexDigit(char c){
        if(c >= '0' && c <= '9') return c - '0';
        if(c >= 'a' && c <= 'f') return c - 'a' + 10;
        if(c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    // ページの共通の頭(スマホの幅で読めるように viewport を付ける)
    void PageHead(SetupPortal::Response& o, const char* device_name){
        o.append("<!DOCTYPE html><html lang=\"ja\"><head><meta charset=\"utf-8\">"
                 "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
                 "<title>pico-os Wi-Fi設定</title><style>"
                 "body{font-family:sans-serif;max-width:28em;margin:1em auto;padding:0 1em}"
                 "input{display:block;width:100%;box-sizing:border-box;font-size:1.1em;margin:.3em 0 1em;padding:.4em}"
                 "button{font-size:1.1em;padding:.5em 1.5em}.msg{padding:.6em;background:#eef}"
                 "</style></head><body><h1>");
        SetupPortal::AppendEscaped(o, device_name);
        o.append(" のWi-Fi設定</h1>");
    }

    void FormBody(SetupPortal::Response& o){
        o.append("<form method=\"post\" action=\"/save\">"
                 "<label>Wi-Fiの名前(SSID)<input name=\"ssid\" maxlength=\"32\" required autocapitalize=\"off\" autocorrect=\"off\"></label>"
                 "<label>パスワード(無ければ空欄)<input name=\"pass\" type=\"password\" maxlength=\"64\" autocapitalize=\"off\"></label>"
                 "<button type=\"submit\">本体へ送る</button></form>"
                 "<p>送ると本体に保存され、本体はこのアクセスポイントを止めてそのWi-Fiへ繋ぎます。</p>");
    }

    const char* Reason(int status){
        switch(status){
            case 200: return "OK";
            case 413: return "Payload Too Large";
            default:  return "Bad Request";
        }
    }
}

void SetupPortal::Request::reset(){
    len_ = 0;
    body_start_ = 0;
    content_length_ = 0;
    headers_done_ = false;
    post_ = false;
    path_.clear();
    state_ = State::Reading;
    buf_[0] = '\0';
}

bool SetupPortal::Request::parseHeaders(){
    // 要求行 "METHOD /path HTTP/1.1"
    const char* p = buf_;
    if(StartsWithNoCase(p, "post ")){ post_ = true; p += 5; }
    else if(StartsWithNoCase(p, "get ")){ post_ = false; p += 4; }
    else if(StartsWithNoCase(p, "head ")){ post_ = false; p += 5; }
    else return false;
    const char* sp = strchr(p, ' ');
    if(!sp) return false;
    size_t plen = (size_t)(sp - p);
    const char* q = (const char*)memchr(p, '?', plen); // クエリは使わない
    if(q) plen = (size_t)(q - p);
    if(plen >= PICO_STR_M) plen = PICO_STR_M - 1;       // 長いパスは切ってよい(どのパスでも同じページ)
    path_.assign(p, plen);

    // Content-Length だけ見る
    content_length_ = 0;
    for(const char* line = strstr(buf_, "\r\n"); line && line + 2 < buf_ + body_start_; line = strstr(line + 2, "\r\n")){
        if(StartsWithNoCase(line + 2, "content-length:")){
            content_length_ = (size_t)strtoul(line + 2 + 15, nullptr, 10);
        }
    }
    return true;
}

SetupPortal::Request::State SetupPortal::Request::feed(const void* data, size_t n){
    if(state_ != State::Reading) return state_;
    if(len_ + n > kMaxRequest){
        state_ = State::TooLarge;
        return state_;
    }
    memcpy(buf_ + len_, data, n);
    len_ += n;
    buf_[len_] = '\0';
    if(!headers_done_){
        const char* end = strstr(buf_, "\r\n\r\n");
        if(!end) return state_;
        headers_done_ = true;
        body_start_ = (size_t)(end - buf_) + 4;
        if(!parseHeaders()){
            state_ = State::Bad;
            return state_;
        }
        if(body_start_ + content_length_ > kMaxRequest){
            state_ = State::TooLarge;
            return state_;
        }
    }
    if(len_ >= body_start_ + content_length_){
        buf_[body_start_ + content_length_] = '\0';
        state_ = State::Done;
    }
    return state_;
}

bool SetupPortal::FormValue(const char* body, const char* key, char* out, size_t cap){
    if(!body || !key || !out || cap == 0) return false;
    const size_t klen = strlen(key);
    const char* p = body;
    while(*p){
        const char* amp = strchr(p, '&');
        const char* end = amp ? amp : p + strlen(p);
        const char* eq = (const char*)memchr(p, '=', (size_t)(end - p));
        const char* name_end = eq ? eq : end;
        if((size_t)(name_end - p) == klen && strncmp(p, key, klen) == 0){
            size_t n = 0;
            for(const char* v = eq ? eq + 1 : end; v < end; v++){
                char c = *v;
                if(c == '+'){
                    c = ' ';
                }else if(c == '%' && v + 2 < end && HexDigit(v[1]) >= 0 && HexDigit(v[2]) >= 0){
                    c = (char)(HexDigit(v[1]) * 16 + HexDigit(v[2]));
                    v += 2;
                }
                if(n + 1 >= cap) return false; // 入りきらない
                out[n++] = c;
            }
            out[n] = '\0';
            return true;
        }
        if(!amp) break;
        p = amp + 1;
    }
    return false;
}

void SetupPortal::FormatHeader(int status, size_t body_len, char* out, size_t cap){
    snprintf(out, cap,
             "HTTP/1.1 %d %s\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %u\r\n"
             "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
             status, Reason(status), (unsigned)body_len);
}

int SetupPortal::Respond(const Request& req, SaveFn save, const char* device_name,
                         Response& body, FixedString<PICO_STR_M>& saved_ssid){
    saved_ssid.clear();
    body.clear();

    if(req.state() == Request::State::TooLarge){
        PageHead(body, device_name);
        body.append("<p class=\"msg\">送られた内容が大きすぎます。</p></body></html>");
        return 413;
    }
    if(req.state() != Request::State::Done){
        body.assign("bad request");
        return 400;
    }

    PageHead(body, device_name);
    if(req.isPost() && strcmp(req.path(), "/save") == 0){
        char ssid[PICO_STR_M] = "";
        char pass[PICO_STR_L] = "";
        const bool has_ssid = FormValue(req.body(), "ssid", ssid, sizeof(ssid));
        const bool pass_ok = !strstr(req.body(), "pass=") || FormValue(req.body(), "pass", pass, sizeof(pass));
        const SaveResult r = (has_ssid && ssid[0] && pass_ok && save) ? save(ssid, pass) : SaveResult::Invalid;
        switch(r){
            case SaveResult::Saved:
                saved_ssid.assign(ssid);
                body.append("<p class=\"msg\">「");
                AppendEscaped(body, ssid);
                body.append("」を保存しました。本体はこのWi-Fiへ繋ぎに行きます。"
                            "このアクセスポイントは止まるので、スマホは元のWi-Fiへ戻してください。</p></body></html>");
                break;
            case SaveResult::Full:
                body.append("<p class=\"msg\">保存できる数がいっぱいです。本体の設定アプリで不要なものを削除してください。</p>");
                FormBody(body);
                body.append("</body></html>");
                break;
            case SaveResult::Invalid:
                body.append("<p class=\"msg\">SSIDは1〜32バイト、パスワードは64文字までです。</p>");
                FormBody(body);
                body.append("</body></html>");
                break;
        }
        return 200;
    }

    // それ以外は全部入力のページ(スマホのポータル検出にもこれを返して、ページを開かせる)
    FormBody(body);
    body.append("</body></html>");
    return 200;
}

size_t SetupPortal::BuildDnsReply(const uint8_t* q, size_t len, uint32_t ip, uint8_t* out, size_t cap){
    if(!q || !out || len < 12 || len > kMaxDnsPacket) return 0;
    const bool is_response = (q[2] & 0x80) != 0;
    const uint8_t opcode = (uint8_t)((q[2] >> 3) & 0x0F);
    const uint16_t qdcount = (uint16_t)((q[4] << 8) | q[5]);
    if(is_response || opcode != 0 || qdcount != 1) return 0;

    // 質問の名前(ラベルの並び)を読み飛ばす。圧縮ポインタは質問には来ない
    size_t p = 12;
    while(true){
        if(p >= len) return 0;
        const uint8_t l = q[p];
        if(l == 0){ p++; break; }
        if(l & 0xC0) return 0;
        p += 1 + l;
    }
    if(p + 4 > len) return 0;
    const uint16_t qtype  = (uint16_t)((q[p] << 8) | q[p + 1]);
    const uint16_t qclass = (uint16_t)((q[p + 2] << 8) | q[p + 3]);
    const size_t question_end = p + 4;
    const bool answer = (qtype == 1 || qtype == 255) && qclass == 1; // A か ANY、IN

    const size_t total = question_end + (answer ? 16 : 0);
    if(total > cap) return 0;
    memcpy(out, q, question_end);                  // ID・質問をそのまま
    out[2] = (uint8_t)(0x80 | (q[2] & 0x01) | 0x04); // 応答・RDを写す・AA
    out[3] = 0x80;                                 // RA、エラー無し
    out[6] = 0; out[7] = answer ? 1 : 0;           // ANCOUNT
    out[8] = 0; out[9] = 0; out[10] = 0; out[11] = 0;
    if(answer){
        uint8_t* a = out + question_end;
        a[0] = 0xC0; a[1] = 0x0C;                  // 名前は質問の名前を指す
        a[2] = 0; a[3] = 1;                        // A
        a[4] = 0; a[5] = 1;                        // IN
        a[6] = 0; a[7] = 0; a[8] = 0; a[9] = 60;   // TTL 60秒
        a[10] = 0; a[11] = 4;
        a[12] = (uint8_t)(ip >> 24); a[13] = (uint8_t)(ip >> 16); a[14] = (uint8_t)(ip >> 8); a[15] = (uint8_t)ip;
    }
    return total;
}
