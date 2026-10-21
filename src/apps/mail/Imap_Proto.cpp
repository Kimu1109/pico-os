#include "apps/mail/Imap_Proto.hpp"
#include "apps/mail/Mail_Mime.hpp"

#include <cstdio>
#include <cstring>

namespace {
    char Upper(char c){ return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c; }
    bool IsDigit(char c){ return c >= '0' && c <= '9'; }

    bool EqualsNoCase(const char* a, size_t alen, const char* b){
        const size_t blen = strlen(b);
        if(alen != blen) return false;
        for(size_t i = 0; i < alen; i++) if(Upper(a[i]) != Upper(b[i])) return false;
        return true;
    }

    // 行の末尾の "{123}" / "{123+}" を探す。見つかれば '{' の位置と大きさ
    bool FindLiteralMarker(const char* s, size_t len, size_t& brace, uint32_t& size){
        if(len < 3 || s[len - 1] != '}') return false;
        size_t j = len - 1;
        if(j > 0 && s[j - 1] == '+') j--;
        const size_t digits_end = j;
        while(j > 0 && IsDigit(s[j - 1])) j--;
        if(j == digits_end || j == 0 || s[j - 1] != '{') return false;
        uint64_t v = 0;
        for(size_t k = j; k < digits_end; k++){
            v = v * 10 + (uint64_t)(s[k] - '0');
            if(v > 0xFFFFFFFFull) return false;
        }
        brace = j - 1;
        size = (uint32_t)v;
        return true;
    }

    // ---- FETCH の中身を読む小さなカーソル ----
    struct Cursor {
        const char* p;
        const char* end;

        bool done() const { return p >= end; }
        char peek() const { return p < end ? *p : '\0'; }
        void skipSpaces(){ while(p < end && *p == ' ') p++; }

        bool number(uint32_t& out){
            if(!IsDigit(peek())) return false;
            uint64_t v = 0;
            while(p < end && IsDigit(*p)){
                v = v * 10 + (uint64_t)(*p - '0');
                if(v > 0xFFFFFFFFull) return false;
                p++;
            }
            out = (uint32_t)v;
            return true;
        }

        // 項目名(BODY[HEADER.FIELDS (FROM SUBJECT)]<0> のように [] の中に空白や括弧を含んでよい)
        void itemName(const char*& name, size_t& len){
            name = p;
            while(p < end && *p != ' ' && *p != '(' && *p != ')'){
                if(*p == '['){
                    while(p < end && *p != ']') p++;
                    if(p < end) p++;
                    continue;
                }
                p++;
            }
            len = (size_t)(p - name);
        }

        // 文字列(quoted / literal / NIL)。中身の範囲を返す
        bool nstring(const char*& s, size_t& n){
            skipSpaces();
            if(peek() == '"'){
                p++;
                s = p;
                while(p < end && *p != '"'){
                    if(*p == '\\' && p + 1 < end) p++;
                    p++;
                }
                n = (size_t)(p - s);
                if(p < end) p++;
                return true;
            }
            if(peek() == '{'){
                p++;
                uint32_t size = 0;
                if(!number(size)) return false;
                if(peek() == '+') p++;
                if(peek() != '}') return false;
                p++;
                if(peek() == '\r') p++;
                if(peek() != '\n') return false;
                p++;
                if((size_t)(end - p) < size) size = (uint32_t)(end - p);
                s = p;
                n = size;
                p += size;
                return true;
            }
            if(end - p >= 3 && Upper(p[0]) == 'N' && Upper(p[1]) == 'I' && Upper(p[2]) == 'L'){
                p += 3;
                s = "";
                n = 0;
                return true;
            }
            return false;
        }

        // 何でも1つ読み飛ばす(ENVELOPE / BODYSTRUCTURE 等の入れ子の括弧も)
        bool skipValue(int depth = 0){
            skipSpaces();
            if(depth > 16) return false;
            const char c = peek();
            if(c == '('){
                p++;
                while(true){
                    skipSpaces();
                    if(done()) return false;
                    if(peek() == ')'){ p++; return true; }
                    if(!skipValue(depth + 1)) return false;
                }
            }
            if(c == '"' || c == '{'){
                const char* s; size_t n;
                return nstring(s, n);
            }
            //atom / number / \Flag
            const char* start = p;
            while(p < end && *p != ' ' && *p != ')' && *p != '('){
                if(*p == '['){ while(p < end && *p != ']') p++; }
                if(p < end) p++;
            }
            return p > start;
        }
    };
}

namespace Imap {

// ---------------------------------------------------------------------------
// Reader
// ---------------------------------------------------------------------------

void Reader::reset(){
    len_ = 0;
    overflow_ = false;
    tail_len_ = 0;
    mode_ = Mode::Line;
    lit_left_ = 0;
    lit_store_ = 0;
    lit_stream_ = false;
}

void Reader::putLineChar(char c){
    if(!overflow_ && len_ < kCap) buf_[len_++] = c;
    else overflow_ = true;
    if(tail_len_ == sizeof(tail_)){
        memmove(tail_, tail_ + 1, sizeof(tail_) - 1);
        tail_len_--;
    }
    tail_[tail_len_++] = c;
}

void Reader::endLine(Handler& h){
    if(!overflow_ && len_ > 0 && buf_[len_ - 1] == '\r') len_--;
    if(tail_len_ > 0 && tail_[tail_len_ - 1] == '\r') tail_len_--;

    size_t brace = 0;
    uint32_t size = 0;
    const bool literal = overflow_ ? FindLiteralMarker(tail_, tail_len_, brace, size)
                                   : FindLiteralMarker(buf_, len_, brace, size);
    tail_len_ = 0;
    if(literal){
        const size_t line_len = overflow_ ? len_ : brace;
        buf_[line_len] = '\0';
        lit_stream_ = h.onLiteralStart(buf_, line_len, size);
        lit_store_ = 0;
        if(!overflow_){
            len_ = brace;
            if(lit_stream_){
                //流した中身の代わりに空の文字列を置いておく(後ろの項目を読めるように)
                buf_[len_++] = '"';
                buf_[len_++] = '"';
            }else if(len_ + 16 > kCap){
                //印を書き直す場所も無い。この応答は溢れたものとして扱う
                overflow_ = true;
            }else{
                //入りきる分だけ貯め、印の数字もそれに合わせて書き直す(ParseFetch がずれないように)。
                //後ろに続く項目のために少し空けておく
                constexpr size_t kReserve = 96;
                const size_t room = (kCap > len_ + kReserve) ? kCap - len_ - kReserve : 0;
                lit_store_ = size < room ? size : (uint32_t)room;
                len_ += (size_t)snprintf(buf_ + len_, kCap + 1 - len_, "{%u}\n", (unsigned)lit_store_);
            }
        }
        lit_left_ = size;
        if(size == 0){
            if(lit_stream_) h.onLiteralEnd();
            mode_ = Mode::Line;
        }else{
            mode_ = Mode::Literal;
        }
        return;
    }

    buf_[len_ < kCap ? len_ : kCap] = '\0';
    h.onResponse(buf_, len_ < kCap ? len_ : kCap, overflow_);
    len_ = 0;
    overflow_ = false;
}

void Reader::feed(const uint8_t* data, size_t n, Handler& h){
    size_t i = 0;
    while(i < n){
        if(mode_ == Mode::Literal){
            size_t take = n - i;
            if(take > lit_left_) take = lit_left_;
            if(lit_stream_){
                h.onLiteralData(data + i, take);
            }else if(lit_store_ > 0){
                const size_t store = take < lit_store_ ? take : lit_store_;
                memcpy(buf_ + len_, data + i, store);
                len_ += store;
                lit_store_ -= (uint32_t)store;
            }
            lit_left_ -= (uint32_t)take;
            i += take;
            if(lit_left_ == 0){
                if(lit_stream_) h.onLiteralEnd();
                lit_stream_ = false;
                mode_ = Mode::Line;
            }
            continue;
        }
        const char c = (char)data[i++];
        if(c == '\n') endLine(h);
        else putLineChar(c);
    }
}

// ---------------------------------------------------------------------------
// 応答の分類
// ---------------------------------------------------------------------------

bool KeywordIs(const Response& r, const char* word){
    return EqualsNoCase(r.keyword, r.keyword_len, word);
}

bool ParseResponse(const char* s, Response& out){
    out = Response{};
    if(!s) return false;
    if(s[0] == '+'){
        out.kind = Kind::Continuation;
        out.text = s + 1;
        while(*out.text == ' ') out.text++;
        return true;
    }

    const char* p = s;
    if(p[0] == '*' && p[1] == ' '){
        out.kind = Kind::Untagged;
        p += 2;
        if(IsDigit(*p)){
            uint64_t v = 0;
            while(IsDigit(*p)){ v = v * 10 + (uint64_t)(*p - '0'); p++; }
            out.number = (uint32_t)v;
            out.has_number = true;
            while(*p == ' ') p++;
        }
    }else{
        //タグ("a12")
        const char* tag = p;
        while(*p && *p != ' ') p++;
        if(p == tag || *p != ' '){
            out.kind = Kind::Other;
            out.text = s;
            return false;
        }
        out.kind = Kind::Tagged;
        if(tag[0] == 'a' || tag[0] == 'A'){
            uint64_t v = 0;
            for(const char* q = tag + 1; q < p && IsDigit(*q); q++) v = v * 10 + (uint64_t)(*q - '0');
            out.tag = (uint32_t)v;
        }
        while(*p == ' ') p++;
    }

    out.keyword = p;
    while(*p && *p != ' ') p++;
    out.keyword_len = (size_t)(p - out.keyword);
    while(*p == ' ') p++;
    out.text = p;

    if(KeywordIs(out, "OK")) out.status = Status::Ok;
    else if(KeywordIs(out, "NO")) out.status = Status::No;
    else if(KeywordIs(out, "BAD")) out.status = Status::Bad;
    else if(KeywordIs(out, "BYE")) out.status = Status::Bye;
    else if(KeywordIs(out, "PREAUTH")) out.status = Status::PreAuth;
    return true;
}

// ---------------------------------------------------------------------------
// FETCH
// ---------------------------------------------------------------------------

bool ParseFetch(const char* s, size_t len, Fetch& out){
    out = Fetch{};
    Cursor c{s, s + len};
    if(len < 2 || s[0] != '*' || s[1] != ' ') return false;
    c.p += 2;
    if(!c.number(out.seq)) return false;
    c.skipSpaces();
    const char* kw; size_t kw_len;
    c.itemName(kw, kw_len);
    if(!EqualsNoCase(kw, kw_len, "FETCH")) return false;
    c.skipSpaces();
    if(c.peek() != '(') return false;
    c.p++;

    while(true){
        c.skipSpaces();
        if(c.done()) return true; //閉じ括弧が無い(溢れて切れた等)。読めた分は使う
        if(c.peek() == ')') return true;
        const char* name; size_t name_len;
        c.itemName(name, name_len);
        if(name_len == 0) return false;
        c.skipSpaces();

        if(EqualsNoCase(name, name_len, "UID")){
            if(!c.number(out.uid)) return false;
            out.has_uid = true;
        }else if(EqualsNoCase(name, name_len, "RFC822.SIZE")){
            if(!c.number(out.size)) return false;
            out.has_size = true;
        }else if(EqualsNoCase(name, name_len, "FLAGS")){
            if(c.peek() != '(') return false;
            c.p++;
            out.has_flags = true;
            while(true){
                c.skipSpaces();
                if(c.done()) return false;
                if(c.peek() == ')'){ c.p++; break; }
                const char* f; size_t fl;
                c.itemName(f, fl);
                if(fl == 0) return false;
                if(EqualsNoCase(f, fl, "\\Seen")) out.seen = true;
                else if(EqualsNoCase(f, fl, "\\Answered")) out.answered = true;
                else if(EqualsNoCase(f, fl, "\\Flagged")) out.flagged = true;
                else if(EqualsNoCase(f, fl, "\\Deleted")) out.deleted = true;
            }
        }else if(EqualsNoCase(name, name_len, "INTERNALDATE")){
            const char* v; size_t vn;
            if(!c.nstring(v, vn)) return false;
            char buf[48];
            if(vn >= sizeof(buf)) vn = sizeof(buf) - 1;
            memcpy(buf, v, vn);
            buf[vn] = '\0';
            out.has_date = MailMime::ParseInternalDate(buf, out.date);
        }else if(name_len >= 5 && (strncmp(name, "BODY[", 5) == 0 || strncmp(name, "body[", 5) == 0
                                   || EqualsNoCase(name, name_len, "RFC822.HEADER"))){
            const char* v; size_t vn;
            if(!c.nstring(v, vn)) return false;
            //ヘッダーだけを取ったもの(BODY[HEADER] / BODY[HEADER.FIELDS (...)])
            bool header = EqualsNoCase(name, name_len, "RFC822.HEADER");
            for(size_t k = 0; k + 6 <= name_len && !header; k++){
                if(EqualsNoCase(name + k, 6, "HEADER")) header = true;
            }
            if(header){
                out.header = v;
                out.header_len = vn;
            }
        }else{
            if(!c.skipValue()) return false;
        }
    }
}

void ApplyFetch(const Fetch& f, Summary& out){
    out.seq = f.seq;
    if(f.has_uid) out.uid = f.uid;
    if(f.has_size) out.size = f.size;
    if(f.has_flags){
        out.seen = f.seen;
        out.answered = f.answered;
        out.flagged = f.flagged;
    }
    if(f.has_date) out.date = f.date;
    if(!f.header) return;

    //ヘッダーの欄を1つずつ(折り返しの続きの行も含めて)見る
    const char* p = f.header;
    const char* end = f.header + f.header_len;
    while(p < end){
        const char* line = p;
        //欄の終わり = 次の行が空白で始まらない改行
        const char* q = p;
        while(q < end){
            if(*q == '\n' && (q + 1 >= end || (q[1] != ' ' && q[1] != '\t'))) break;
            q++;
        }
        const char* colon = static_cast<const char*>(memchr(line, ':', (size_t)(q - line)));
        p = (q < end) ? q + 1 : end;
        if(!colon) continue;
        size_t name_len = (size_t)(colon - line);
        while(name_len > 0 && line[name_len - 1] == ' ') name_len--;
        const char* v = colon + 1;
        size_t vn = (size_t)(q - v);
        while(vn > 0 && (v[vn - 1] == '\r' || v[vn - 1] == '\n')) vn--;

        if(EqualsNoCase(line, name_len, "Subject")){
            char buf[PICO_STR_LL];
            MailMime::DecodeHeader(v, vn, buf, sizeof(buf));
            out.subject.assign(buf);
        }else if(EqualsNoCase(line, name_len, "From")){
            char decoded[PICO_STR_256B];
            MailMime::DecodeHeader(v, vn, decoded, sizeof(decoded));
            char name[PICO_STR_L];
            MailMime::DisplayAddress(decoded, name, sizeof(name));
            out.from.assign(name);
        }else if(EqualsNoCase(line, name_len, "Date") && out.date == 0){
            char buf[64];
            const size_t n = vn < sizeof(buf) - 1 ? vn : sizeof(buf) - 1;
            memcpy(buf, v, n);
            buf[n] = '\0';
            int64_t t = 0;
            if(MailMime::ParseDate(buf, t)) out.date = t;
        }
    }
}

bool Quote(const char* s, char* out, size_t cap){
    if(!s || cap < 3) return false;
    size_t n = 0;
    out[n++] = '"';
    for(const char* p = s; *p; p++){
        const unsigned char c = (unsigned char)*p;
        if(c == '\r' || c == '\n' || c >= 0x80) return false;
        const bool esc = (c == '"' || c == '\\');
        if(n + (esc ? 2 : 1) + 2 > cap) return false;
        if(esc) out[n++] = '\\';
        out[n++] = (char)c;
    }
    out[n++] = '"';
    out[n] = '\0';
    return true;
}

}
