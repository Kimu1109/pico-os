#include "net/File_Share.hpp"
#include "storage/SD_IO.hpp"

#include <cctype>
#include <cstdlib>

namespace FileShare {

namespace {
    int HexValue(char c){
        if(c >= '0' && c <= '9') return c - '0';
        if(c >= 'a' && c <= 'f') return c - 'a' + 10;
        if(c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    bool EqualsNoCase(const char* a, size_t alen, const char* b){
        const size_t blen = strlen(b);
        if(alen != blen) return false;
        for(size_t i = 0; i < alen; i++){
            if(tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
        }
        return true;
    }

    // 見出しの値の前後の空白を除いて out へ(入らなければ false)
    template <size_t N>
    bool AssignTrimmed(FixedString<N>& out, const char* v, size_t len){
        while(len > 0 && (*v == ' ' || *v == '\t')){ v++; len--; }
        while(len > 0 && (v[len - 1] == ' ' || v[len - 1] == '\t')) len--;
        if(len > out.capacity()) return false;
        out.assign(v, len);
        return true;
    }
}

// ---------- 要求の見出し ----------

void RequestHead::reset(){
    len_ = 0;
    buf_[0] = '\0';
    method_ = Method::Other;
    path_.clear();
    query_.clear();
    host_.clear();
    cookie_.clear();
    key_.clear();
    content_length_ = 0;
    has_length_ = false;
    state_ = State::Reading;
}

RequestHead::State RequestHead::feed(const uint8_t* data, size_t len, size_t& used){
    used = 0;
    if(state_ != State::Reading) return state_;
    // 1バイトずつ見て、空行("\r\n\r\n")で止める。止めた後ろは本文なので食べない
    for(size_t i = 0; i < len; i++){
        if(len_ >= kMaxHead){ used = i; return state_ = State::TooLarge; }
        buf_[len_++] = (char)data[i];
        if(len_ >= 4 && memcmp(buf_ + len_ - 4, "\r\n\r\n", 4) == 0){
            buf_[len_] = '\0';
            used = i + 1;
            return state_ = parse() ? State::Done : State::Bad;
        }
    }
    used = len;
    return state_;
}

bool RequestHead::parse(){
    const char* line_end = strstr(buf_, "\r\n");
    if(!line_end) return false;
    // 要求行: METHOD SP TARGET SP VERSION
    const char* sp1 = (const char*)memchr(buf_, ' ', (size_t)(line_end - buf_));
    if(!sp1) return false;
    const char* target = sp1 + 1;
    const char* sp2 = (const char*)memchr(target, ' ', (size_t)(line_end - target));
    if(!sp2 || sp2 == target || *target != '/') return false;
    const size_t mlen = (size_t)(sp1 - buf_);
    if(mlen == 3 && memcmp(buf_, "GET", 3) == 0) method_ = Method::Get;
    else if(mlen == 4 && memcmp(buf_, "HEAD", 4) == 0) method_ = Method::Head;
    else if(mlen == 3 && memcmp(buf_, "PUT", 3) == 0) method_ = Method::Put;
    else if(mlen == 4 && memcmp(buf_, "POST", 4) == 0) method_ = Method::Post;
    else method_ = Method::Other;

    const char* q = (const char*)memchr(target, '?', (size_t)(sp2 - target));
    const char* path_end = q ? q : sp2;
    if((size_t)(path_end - target) > path_.capacity()) return false;
    path_.assign(target, (size_t)(path_end - target));
    if(q){
        if((size_t)(sp2 - q - 1) > query_.capacity()) return false;
        query_.assign(q + 1, (size_t)(sp2 - q - 1));
    }

    // 見出し: 要るものだけ拾う
    const char* p = line_end + 2;
    while(*p && !(p[0] == '\r' && p[1] == '\n')){
        const char* e = strstr(p, "\r\n");
        if(!e) return false;
        const char* colon = (const char*)memchr(p, ':', (size_t)(e - p));
        if(colon){
            const size_t nlen = (size_t)(colon - p);
            const char* v = colon + 1;
            const size_t vlen = (size_t)(e - v);
            if(EqualsNoCase(p, nlen, "host")){
                if(!AssignTrimmed(host_, v, vlen)) return false;
            }else if(EqualsNoCase(p, nlen, "cookie")){
                AssignTrimmed(cookie_, v, vlen); // 長すぎるクッキーは無いものとして扱う(キーが無い=断る)
            }else if(EqualsNoCase(p, nlen, "x-pico-key")){
                if(!AssignTrimmed(key_, v, vlen)) key_.clear();
            }else if(EqualsNoCase(p, nlen, "content-length")){
                FixedString<PICO_STR_S> num;
                if(!AssignTrimmed(num, v, vlen) || num.empty()) return false;
                uint64_t n = 0;
                for(const char* c = num.c_str(); *c; c++){
                    if(*c < '0' || *c > '9') return false;
                    if(n > (UINT64_MAX - 9) / 10) return false;
                    n = n * 10 + (uint64_t)(*c - '0');
                }
                content_length_ = n;
                has_length_ = true;
            }else if(EqualsNoCase(p, nlen, "transfer-encoding")){
                return false; // chunked は読まない(ブラウザの fetch も curl -T も Content-Length を付ける)
            }
        }
        p = e + 2;
    }
    return true;
}

// ---------- 何の要求か ----------

Route Classify(Method m, const char* path, const char** file_part){
    if(file_part) *file_part = nullptr;
    const bool get = (m == Method::Get || m == Method::Head);
    if(strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) return get ? Route::Page : Route::MethodNotAllowed;
    if(strncmp(path, "/f/", 3) == 0 || strcmp(path, "/f") == 0){
        if(file_part) *file_part = path + 2;
        if(get) return Route::Download;
        if(m == Method::Put) return Route::Upload;
        return Route::MethodNotAllowed;
    }
    struct Api { const char* path; Route route; bool post; };
    static const Api kApis[] = {
        {"/api/info",  Route::Info,   false},
        {"/api/ls",    Route::List,   false},
        {"/api/mkdir", Route::Mkdir,  true},
        {"/api/rm",    Route::Remove, true},
        {"/api/mv",    Route::Move,   true},
    };
    for(const Api& a : kApis){
        if(strcmp(path, a.path) != 0) continue;
        if(a.post) return m == Method::Post ? a.route : Route::MethodNotAllowed;
        return get ? a.route : Route::MethodNotAllowed;
    }
    return Route::NotFound;
}

// ---------- 文字列の道具 ----------

bool PercentDecode(const char* s, size_t len, bool plus_is_space, char* out, size_t cap){
    size_t o = 0;
    for(size_t i = 0; i < len; i++){
        char c = s[i];
        if(c == '%'){
            if(i + 2 >= len) return false;
            const int hi = HexValue(s[i + 1]);
            const int lo = HexValue(s[i + 2]);
            if(hi < 0 || lo < 0) return false;
            c = (char)(hi * 16 + lo);
            if(c == '\0') return false;
            i += 2;
        }else if(c == '+' && plus_is_space){
            c = ' ';
        }
        if(o + 1 >= cap) return false;
        out[o++] = c;
    }
    if(cap == 0) return false;
    out[o] = '\0';
    return true;
}

bool QueryValue(const char* query, const char* key, char* out, size_t cap){
    if(!query || !key) return false;
    const size_t klen = strlen(key);
    const char* p = query;
    while(*p){
        const char* amp = strchr(p, '&');
        const char* end = amp ? amp : p + strlen(p);
        const char* eq = (const char*)memchr(p, '=', (size_t)(end - p));
        const char* name_end = eq ? eq : end;
        if((size_t)(name_end - p) == klen && memcmp(p, key, klen) == 0){
            const char* v = eq ? eq + 1 : end;
            return PercentDecode(v, (size_t)(end - v), true, out, cap);
        }
        if(!amp) break;
        p = amp + 1;
    }
    return false;
}

bool CookieValue(const char* cookie, const char* name, char* out, size_t cap){
    if(!cookie || !name || cap == 0) return false;
    const size_t nlen = strlen(name);
    const char* p = cookie;
    while(*p){
        while(*p == ' ' || *p == ';') p++;
        const char* end = strchr(p, ';');
        if(!end) end = p + strlen(p);
        const char* eq = (const char*)memchr(p, '=', (size_t)(end - p));
        if(eq && (size_t)(eq - p) == nlen && memcmp(p, name, nlen) == 0){
            const size_t vlen = (size_t)(end - eq - 1);
            if(vlen + 1 > cap) return false;
            memcpy(out, eq + 1, vlen);
            out[vlen] = '\0';
            return true;
        }
        p = end;
    }
    return false;
}

// ---------- パス ----------

bool IsSystemPath(const char* path){
    if(!path) return false;
    return (tolower((unsigned char)path[0]) == '/' &&
            tolower((unsigned char)path[1]) == 's' &&
            tolower((unsigned char)path[2]) == 'y' &&
            tolower((unsigned char)path[3]) == 's' &&
            (path[4] == '\0' || path[4] == '/'));
}

PathCheck CheckPath(const char* decoded, FixedString<PICO_PATH_LEN>& out){
    if(!decoded || strlen(decoded) > PICO_PATH_LEN) return PathCheck::Bad;
    // 名前ごとに確かめる。"." と ".." は正規化が畳む(ルートを超える分は捨てる)ので通す
    const char* p = decoded;
    while(*p){
        while(*p == '/') p++;
        if(!*p) break;
        const char* start = p;
        while(*p && *p != '/') p++;
        char name[PICO_PATH_LEN + 1];
        const size_t len = (size_t)(p - start);
        memcpy(name, start, len);
        name[len] = '\0';
        if(strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        if(!PICO_IO::isValidName(name)) return PathCheck::Bad;
    }
    if(!PICO_IO::normalize(out, decoded)) return PathCheck::Bad;
    if(IsSystemPath(out.c_str())) return PathCheck::Forbidden;
    return PathCheck::Ok;
}

PathCheck PathFromUrl(const char* raw, size_t len, FixedString<PICO_PATH_LEN>& out){
    char decoded[PICO_PATH_LEN + 1];
    if(!PercentDecode(raw, len, false, decoded, sizeof(decoded))) return PathCheck::Bad;
    return CheckPath(decoded, out);
}

// ---------- 守り ----------

bool HostAllowed(const char* host, const char* hostname, const char* ip, uint16_t port){
    if(!host || !host[0]) return false;
    // ":port" を外す(付いていれば待ち受けの番号と同じであること)
    const char* colon = strrchr(host, ':');
    size_t hlen = strlen(host);
    if(colon){
        const char* ps = colon + 1;
        if(!*ps) return false;
        unsigned long v = 0;
        for(const char* c = ps; *c; c++){
            if(*c < '0' || *c > '9') return false;
            v = v * 10 + (unsigned long)(*c - '0');
            if(v > 65535) return false;
        }
        if(v != port) return false;
        hlen = (size_t)(colon - host);
    }
    if(ip && ip[0] && hlen == strlen(ip) && memcmp(host, ip, hlen) == 0) return true;
    if(hostname && hostname[0]){
        const size_t nlen = strlen(hostname);
        if(EqualsNoCase(host, hlen, hostname)) return true;
        // "<名前>.local"
        if(hlen == nlen + 6 && EqualsNoCase(host, nlen, hostname) && EqualsNoCase(host + nlen, 6, ".local")) return true;
    }
    return false;
}

bool KeyEquals(const char* a, const char* b){
    if(!a || !b) return false;
    const size_t la = strlen(a), lb = strlen(b);
    unsigned diff = (unsigned)(la ^ lb);
    for(size_t i = 0; i < lb; i++){
        diff |= (unsigned)((unsigned char)(i < la ? a[i] : 0) ^ (unsigned char)b[i]);
    }
    return diff == 0 && lb > 0;
}

void MakeKey(const uint8_t* rnd, char (&out)[kKeyLen + 1]){
    constexpr size_t n = sizeof(kKeyAlphabet) - 1;
    for(size_t i = 0; i < kKeyLen; i++) out[i] = kKeyAlphabet[rnd[i] % n];
    out[kKeyLen] = '\0';
}

// ---------- 応答 ----------

const char* ContentType(const char* name){
    const char* dot = name ? strrchr(name, '.') : nullptr;
    if(!dot) return "application/octet-stream";
    struct Ext { const char* ext; const char* type; };
    static const Ext kTypes[] = {
        {".txt", "text/plain; charset=utf-8"}, {".md", "text/plain; charset=utf-8"},
        {".lua", "text/plain; charset=utf-8"}, {".cfg", "text/plain; charset=utf-8"},
        {".log", "text/plain; charset=utf-8"}, {".mml", "text/plain; charset=utf-8"},
        {".csv", "text/plain; charset=utf-8"}, {".tsv", "text/plain; charset=utf-8"},
        {".ics", "text/plain; charset=utf-8"}, {".json", "application/json"},
        {".html", "text/plain; charset=utf-8"}, // SDのHTMLを本体のページとして動かさない
        {".png", "image/png"}, {".jpg", "image/jpeg"}, {".jpeg", "image/jpeg"},
        {".bmp", "image/bmp"}, {".gif", "image/gif"},
        {".wav", "audio/wav"},
    };
    for(const Ext& e : kTypes){
        if(EqualsNoCase(dot, strlen(dot), e.ext)) return e.type;
    }
    return "application/octet-stream";
}

const char* StatusText(int status){
    switch(status){
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 303: return "See Other";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 409: return "Conflict";
        case 411: return "Length Required";
        case 413: return "Payload Too Large";
        case 423: return "Locked";
        case 429: return "Too Many Requests";
        case 431: return "Request Header Fields Too Large";
        case 500: return "Internal Server Error";
        case 503: return "Service Unavailable";
        case 507: return "Insufficient Storage";
        default:  return "Error";
    }
}

void FormatHeader(int status, const char* content_type, int64_t length, const char* extra, char* out, size_t cap){
    int n = snprintf(out, cap, "HTTP/1.1 %d %s\r\n", status, StatusText(status));
    auto add = [&](const char* fmt, auto... args){
        if(n < 0 || (size_t)n >= cap) return;
        n += snprintf(out + n, cap - (size_t)n, fmt, args...);
    };
    if(content_type) add("Content-Type: %s\r\n", content_type);
    if(length >= 0) add("Content-Length: %lld\r\n", (long long)length);
    add("%s", "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n");
    if(extra) add("%s", extra);
    add("%s", "\r\n");
}

} // namespace FileShare
