#include "apps/mail/Mail_Mime.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using MailCharset::BufSink;
using MailCharset::Charset;

namespace {
    char Lower(char c){ return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }
    bool IsSpace(char c){ return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
    bool IsDigit(char c){ return c >= '0' && c <= '9'; }
    bool IsAlnum(char c){ return IsDigit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

    bool EqualsNoCase(const char* a, size_t alen, const char* b){
        const size_t blen = strlen(b);
        if(alen != blen) return false;
        for(size_t i = 0; i < alen; i++) if(Lower(a[i]) != Lower(b[i])) return false;
        return true;
    }
    bool StartsNoCase(const char* s, const char* prefix){
        for(; *prefix; s++, prefix++) if(Lower(*s) != Lower(*prefix)) return false;
        return true;
    }

    int HexValue(char c){
        if(c >= '0' && c <= '9') return c - '0';
        if(c >= 'A' && c <= 'F') return c - 'A' + 10;
        if(c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    }

    int Base64Value(char c){
        if(c >= 'A' && c <= 'Z') return c - 'A';
        if(c >= 'a' && c <= 'z') return c - 'a' + 26;
        if(c >= '0' && c <= '9') return c - '0' + 52;
        if(c == '+') return 62;
        if(c == '/') return 63;
        return -1;
    }

    void CopyStr(char* out, size_t cap, const char* s, size_t n){
        if(cap == 0) return;
        if(n >= cap) n = cap - 1;
        memcpy(out, s, n);
        out[n] = '\0';
    }

    // 文字列の末尾が UTF-8 の書きかけなら、その文字ごと落とす
    size_t TrimPartialUtf8(const char* s, size_t len){
        size_t i = len;
        int back = 0;
        while(i > 0 && back < 4 && (((uint8_t)s[i - 1]) & 0xC0) == 0x80){ i--; back++; }
        if(i == 0) return len;
        const uint8_t lead = (uint8_t)s[i - 1];
        int need = 0;
        if(lead >= 0xF0) need = 3;
        else if(lead >= 0xE0) need = 2;
        else if(lead >= 0xC0) need = 1;
        else return len; //ASCII。後ろの続きバイトは元から壊れていた(そのまま)
        return (back >= need) ? len : i - 1;
    }

    // 符号化された語 "=?charset?B|Q?text?=" を読む。読めたら全体の長さを返す(読めなければ0)
    size_t ParseEncodedWord(const char* s, size_t len, const char*& cs, size_t& cs_len,
                            char& enc, const char*& text, size_t& text_len){
        if(len < 8 || s[0] != '=' || s[1] != '?') return 0;
        size_t i = 2;
        cs = s + i;
        while(i < len && s[i] != '?' && !IsSpace(s[i])) i++;
        if(i >= len || s[i] != '?' || i == 2) return 0;
        cs_len = (size_t)(s + i - cs);
        //"charset*lang"(RFC 2231)の言語は使わない
        for(size_t k = 0; k < cs_len; k++) if(cs[k] == '*'){ cs_len = k; break; }
        i++;
        if(i + 2 >= len || s[i + 1] != '?') return 0;
        enc = Lower(s[i]);
        if(enc != 'b' && enc != 'q') return 0;
        i += 2;
        text = s + i;
        while(i + 1 < len && !(s[i] == '?' && s[i + 1] == '=')){
            if(s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n') return 0;
            i++;
        }
        if(i + 1 >= len) return 0;
        text_len = (size_t)(s + i - text);
        return i + 2;
    }

    // 符号化された語の中身をバイト列へ。書いた長さ
    size_t DecodeWordBytes(char enc, const char* text, size_t len, uint8_t* out, size_t cap){
        size_t n = 0;
        if(enc == 'b'){
            uint32_t acc = 0;
            int bits = 0;
            for(size_t i = 0; i < len && n < cap; i++){
                const int v = Base64Value(text[i]);
                if(v < 0) continue;
                acc = (acc << 6) | (uint32_t)v;
                bits += 6;
                if(bits >= 8){
                    bits -= 8;
                    out[n++] = (uint8_t)((acc >> bits) & 0xFF);
                }
            }
            return n;
        }
        for(size_t i = 0; i < len && n < cap; i++){
            const char c = text[i];
            if(c == '_'){ out[n++] = ' '; continue; }
            if(c == '=' && i + 2 < len && HexValue(text[i + 1]) >= 0 && HexValue(text[i + 2]) >= 0){
                out[n++] = (uint8_t)((HexValue(text[i + 1]) << 4) | HexValue(text[i + 2]));
                i += 2;
                continue;
            }
            out[n++] = (uint8_t)c;
        }
        return n;
    }

    // 時差の名前(RFC 5322 の古い書き方と、日本のメールで見かける JST)
    bool ZoneOffset(const char* s, size_t n, int& minutes){
        struct Z { const char* name; int min; };
        static const Z kZones[] = {
            {"GMT", 0}, {"UT", 0}, {"UTC", 0}, {"Z", 0},
            {"EST", -300}, {"EDT", -240}, {"CST", -360}, {"CDT", -300},
            {"MST", -420}, {"MDT", -360}, {"PST", -480}, {"PDT", -420}, {"JST", 540},
        };
        if(n == 5 && (s[0] == '+' || s[0] == '-') && IsDigit(s[1]) && IsDigit(s[2]) && IsDigit(s[3]) && IsDigit(s[4])){
            const int v = ((s[1] - '0') * 10 + (s[2] - '0')) * 60 + (s[3] - '0') * 10 + (s[4] - '0');
            minutes = (s[0] == '-') ? -v : v;
            return true;
        }
        for(const Z& z : kZones){
            if(EqualsNoCase(s, n, z.name)){ minutes = z.min; return true; }
        }
        return false;
    }

    int MonthIndex(const char* s, size_t n){
        static const char* const kMonths[] = {"jan","feb","mar","apr","may","jun","jul","aug","sep","oct","nov","dec"};
        if(n < 3) return 0;
        for(int i = 0; i < 12; i++){
            if(Lower(s[0]) == kMonths[i][0] && Lower(s[1]) == kMonths[i][1] && Lower(s[2]) == kMonths[i][2]) return i + 1;
        }
        return 0;
    }

    bool ParseClock(const char* s, size_t n, int& h, int& m, int& sec){
        //"hh:mm" か "hh:mm:ss"
        int parts[3] = {0, 0, 0};
        int count = 0;
        int cur = -1;
        for(size_t i = 0; i < n; i++){
            if(IsDigit(s[i])){
                cur = (cur < 0 ? 0 : cur) * 10 + (s[i] - '0');
                if(cur > 99) return false;
            }else if(s[i] == ':'){
                if(cur < 0 || count >= 2) return false;
                parts[count++] = cur;
                cur = -1;
            }else{
                return false;
            }
        }
        if(cur < 0 || count < 1) return false;
        parts[count++] = cur;
        h = parts[0];
        m = parts[1];
        sec = (count == 3) ? parts[2] : 0;
        return h < 24 && m < 60 && sec <= 60;
    }

    int64_t ToEpoch(int y, int mon, int d, int h, int m, int s, int zone_min){
        return MailMime::DaysFromCivil(y, mon, d) * 86400 + h * 3600 + m * 60 + s - (int64_t)zone_min * 60;
    }
}

namespace MailMime {

int64_t DaysFromCivil(int y, int m, int d){
    //Howard Hinnant の days_from_civil
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

size_t DecodeHeader(const char* raw, size_t len, char* out, size_t cap){
    BufSink sink(out, cap);
    if(!raw || cap == 0) return 0;

    MailCharset::Decoder literal;
    literal.begin(Charset::Utf8);

    //隣り合う符号化された語は、文字コードが同じなら繋いでから直す
    //(UTF-8の1文字を2つの語に分けて書くメーラーがあるため)
    uint8_t pend[192];
    size_t pend_len = 0;
    Charset pend_cs = Charset::Utf8;
    auto flush = [&](){
        if(pend_len == 0) return;
        MailCharset::DecodeAll(pend_cs, pend, pend_len, sink);
        pend_len = 0;
    };

    bool last_encoded = false;
    bool pending_space = false; //語と語の間の空白(次が普通の文字なら1つ出す)
    bool space_out = false;     //直前に空白を出した(続く空白を詰める)
    size_t i = 0;
    while(i < len){
        const char c = raw[i];
        if(c == '\r' || c == '\n'){ i++; continue; }
        if(c == ' ' || c == '\t'){
            if(last_encoded) pending_space = true;
            else if(!space_out && sink.length() > 0){ literal.finish(sink); sink.put(" ", 1); space_out = true; }
            i++;
            continue;
        }
        if(c == '='){
            const char* cs; size_t cs_len; char enc; const char* text; size_t text_len;
            const size_t used = ParseEncodedWord(raw + i, len - i, cs, cs_len, enc, text, text_len);
            if(used > 0){
                literal.finish(sink);
                const Charset wcs = MailCharset::Parse(cs, cs_len);
                if(pend_len > 0 && wcs != pend_cs) flush();
                pend_cs = wcs;
                uint8_t bytes[192];
                const size_t n = DecodeWordBytes(enc, text, text_len, bytes, sizeof(bytes));
                if(pend_len + n > sizeof(pend)) flush();
                memcpy(pend + pend_len, bytes, n);
                pend_len += n;
                last_encoded = true;
                pending_space = false;
                space_out = false;
                i += used;
                continue;
            }
        }
        flush();
        if(last_encoded && pending_space) sink.put(" ", 1);
        last_encoded = false;
        pending_space = false;
        space_out = false;
        literal.feed((const uint8_t*)&c, 1, sink);
        i++;
    }
    flush();
    literal.finish(sink);

    //前後の空白を落とす
    size_t n = sink.length();
    while(n > 0 && out[n - 1] == ' ') n--;
    out[n] = '\0';
    size_t start = 0;
    while(start < n && out[start] == ' ') start++;
    if(start > 0){
        memmove(out, out + start, n - start + 1);
        n -= start;
    }
    return n;
}

size_t DisplayAddress(const char* s, char* out, size_t cap){
    if(cap == 0) return 0;
    out[0] = '\0';
    if(!s) return 0;
    while(*s == ' ' || *s == '\t') s++;

    //"名前" <addr>
    if(*s == '"'){
        size_t n = 0;
        const char* p = s + 1;
        while(*p && *p != '"'){
            if(*p == '\\' && p[1]) p++;
            if(n + 1 < cap) out[n++] = *p;
            p++;
        }
        out[n] = '\0';
        while(n > 0 && out[n - 1] == ' ') out[--n] = '\0';
        if(n > 0) return n;
        s = (*p == '"') ? p + 1 : p;
        while(*s == ' ') s++;
    }

    //最初の宛先だけ(引用符の外の , まで)
    const char* end = s;
    bool quoted = false;
    while(*end && (quoted || *end != ',')){
        if(*end == '"') quoted = !quoted;
        end++;
    }

    const char* lt = nullptr;
    for(const char* p = s; p < end; p++) if(*p == '<'){ lt = p; break; }
    if(lt){
        //名前 <addr>(引用符なしの名前)
        const char* a = s;
        const char* b = lt;
        while(a < b && (*a == ' ' || *a == '"')) a++;
        while(b > a && (b[-1] == ' ' || b[-1] == '"')) b--;
        if(b > a){
            CopyStr(out, cap, a, (size_t)(b - a));
            return strlen(out);
        }
        const char* gt = lt + 1;
        while(gt < end && *gt != '>') gt++;
        CopyStr(out, cap, lt + 1, (size_t)(gt - lt - 1));
        return strlen(out);
    }

    //addr (名前)
    const char* paren = nullptr;
    for(const char* p = s; p < end; p++) if(*p == '('){ paren = p; break; }
    if(paren){
        const char* close = paren + 1;
        while(close < end && *close != ')') close++;
        if(close > paren + 1){
            CopyStr(out, cap, paren + 1, (size_t)(close - paren - 1));
            return strlen(out);
        }
        end = paren;
    }
    while(end > s && end[-1] == ' ') end--;
    CopyStr(out, cap, s, (size_t)(end - s));
    return strlen(out);
}

bool ParseInternalDate(const char* s, int64_t& epoch){
    if(!s) return false;
    while(*s == ' ' || *s == '"') s++;
    //dd-Mon-yyyy hh:mm:ss +zzzz(日は1桁のこともある)
    int d = 0;
    while(IsDigit(*s)){ d = d * 10 + (*s - '0'); s++; }
    if(*s != '-' || d < 1 || d > 31) return false;
    s++;
    const int mon = MonthIndex(s, strlen(s));
    if(mon == 0 || s[3] != '-') return false;
    s += 4;
    int y = 0;
    for(int k = 0; k < 4; k++){
        if(!IsDigit(s[k])) return false;
        y = y * 10 + (s[k] - '0');
    }
    s += 4;
    while(*s == ' ') s++;
    const char* clock = s;
    while(*s && *s != ' ') s++;
    int h, m, sec;
    if(!ParseClock(clock, (size_t)(s - clock), h, m, sec)) return false;
    while(*s == ' ') s++;
    const char* zone = s;
    while(*s && *s != ' ' && *s != '"') s++;
    int zone_min = 0;
    if(s > zone && !ZoneOffset(zone, (size_t)(s - zone), zone_min)) return false;
    epoch = ToEpoch(y, mon, d, h, m, sec, zone_min);
    return true;
}

bool ParseDate(const char* s, int64_t& epoch){
    if(!s) return false;
    //空白とカンマで区切った語を順に見る: [曜日,] 日 月 年 時刻 [時差]
    const char* tok[8];
    size_t tlen[8];
    int count = 0;
    const char* p = s;
    while(*p && count < 8){
        while(*p == ' ' || *p == ',' || *p == '\t') p++;
        if(!*p || *p == '(') break; //(JST) 等の注釈は見ない
        tok[count] = p;
        while(*p && *p != ' ' && *p != ',' && *p != '\t') p++;
        tlen[count] = (size_t)(p - tok[count]);
        count++;
    }
    int i = 0;
    if(i < count && !IsDigit(tok[i][0])) i++; //曜日
    if(count - i < 4) return false;
    const int d = atoi(tok[i]);
    const int mon = MonthIndex(tok[i + 1], tlen[i + 1]);
    int y = atoi(tok[i + 2]);
    if(tlen[i + 2] <= 2) y += (y < 50) ? 2000 : 1900;
    int h, m, sec;
    if(d < 1 || d > 31 || mon == 0 || !ParseClock(tok[i + 3], tlen[i + 3], h, m, sec)) return false;
    int zone_min = 0;
    if(i + 4 < count) ZoneOffset(tok[i + 4], tlen[i + 4], zone_min);
    epoch = ToEpoch(y, mon, d, h, m, sec, zone_min);
    return true;
}

bool GetParam(const char* value, const char* name, char* out, size_t cap){
    if(cap == 0) return false;
    out[0] = '\0';
    if(!value || !name) return false;
    const size_t name_len = strlen(name);

    //name*0*= / name*1= …(RFC 2231 の分割)は出てきた順に繋ぐ
    uint8_t ext[256];
    size_t ext_len = 0;
    bool have_ext = false;
    Charset ext_cs = Charset::Utf8;
    bool have_plain = false;

    const char* p = value;
    //最初の ; までは型そのもの
    bool quoted = false;
    while(*p && (quoted || *p != ';')){ if(*p == '"') quoted = !quoted; p++; }
    while(*p == ';'){
        p++;
        while(IsSpace(*p)) p++;
        const char* key = p;
        while(*p && *p != '=' && *p != ';') p++;
        const char* key_end = p;
        while(key_end > key && IsSpace(key_end[-1])) key_end--;
        if(*p != '='){ continue; }
        p++;
        while(IsSpace(*p)) p++;
        //値(引用符つきなら外す)
        char val[256];
        size_t vn = 0;
        if(*p == '"'){
            p++;
            while(*p && *p != '"'){
                if(*p == '\\' && p[1]) p++;
                if(vn + 1 < sizeof(val)) val[vn++] = *p;
                p++;
            }
            if(*p == '"') p++;
            while(*p && *p != ';') p++;
        }else{
            while(*p && *p != ';' && !IsSpace(*p)){
                if(vn + 1 < sizeof(val)) val[vn++] = *p;
                p++;
            }
            while(*p && *p != ';') p++;
        }
        val[vn] = '\0';

        const size_t klen = (size_t)(key_end - key);
        if(klen < name_len || !EqualsNoCase(key, name_len, name)) continue;
        const char* rest = key + name_len;
        const size_t rest_len = klen - name_len;
        if(rest_len == 0){
            if(!have_plain){
                CopyStr(out, cap, val, vn);
                have_plain = true;
            }
            continue;
        }
        if(rest[0] != '*') continue;
        //"*" / "*0" / "*0*" / "*1" …
        const bool extended = rest[rest_len - 1] == '*';
        const bool first = (rest_len == 1) || (rest_len >= 2 && rest[1] == '0');
        const char* v = val;
        if(extended && first){
            //charset'lang'中身
            const char* q1 = strchr(val, '\'');
            const char* q2 = q1 ? strchr(q1 + 1, '\'') : nullptr;
            if(q1 && q2){
                ext_cs = MailCharset::Parse(val, (size_t)(q1 - val));
                v = q2 + 1;
            }
        }
        for(const char* c = v; *c && ext_len < sizeof(ext); c++){
            if(extended && *c == '%' && HexValue(c[1]) >= 0 && HexValue(c[2]) >= 0){
                ext[ext_len++] = (uint8_t)((HexValue(c[1]) << 4) | HexValue(c[2]));
                c += 2;
            }else{
                ext[ext_len++] = (uint8_t)*c;
            }
        }
        have_ext = true;
    }
    if(have_ext){
        BufSink sink(out, cap);
        MailCharset::DecodeAll(ext_cs, ext, ext_len, sink);
        return true;
    }
    return have_plain;
}

// ---------------------------------------------------------------------------
// TextExtractor
// ---------------------------------------------------------------------------

void TextExtractor::begin(char* out, size_t cap){
    out_ = out;
    out_cap_ = cap;
    out_len_ = 0;
    out_truncated_ = false;
    if(out_cap_) out_[0] = '\0';
    newlines_ = 0;
    at_line_start_ = true;
    line_len_ = 0;
    line_cont_ = false;
    depth_ = 0;
    boundary_count_ = 0;
    captured_ = Capture::None;
    from_[0] = to_[0] = cc_[0] = subject_[0] = date_[0] = '\0';
    attach_count_ = 0;
    attach_names_[0] = '\0';
    startPart();
}

void TextExtractor::startPart(){
    state_ = State::Headers;
    header_len_ = 0;
    strcpy(type_, "text/plain");
    part_boundary_[0] = '\0';
    part_name_[0] = '\0';
    part_attachment_ = false;
    enc_ = Encoding::Plain;
    part_charset_ = Charset::Utf8;
    part_capture_ = Capture::None;
}

void TextExtractor::feed(const uint8_t* data, size_t n){
    for(size_t i = 0; i < n; i++){
        const char c = (char)data[i];
        if(c == '\n'){
            size_t len = line_len_;
            if(len > 0 && line_[len - 1] == '\r') len--;
            onLine(line_, len, true);
            line_len_ = 0;
            line_cont_ = false;
            continue;
        }
        line_[line_len_++] = c;
        if(line_len_ == kLineCap){
            //長すぎる行は区切って渡す(続きは line_cont_ で分かる)
            onLine(line_, line_len_, false);
            line_len_ = 0;
            line_cont_ = true;
        }
    }
}

void TextExtractor::finish(){
    if(line_len_ > 0){
        size_t len = line_len_;
        if(line_[len - 1] == '\r') len--;
        onLine(line_, len, true);
        line_len_ = 0;
        line_cont_ = false;
    }
    if(state_ == State::Headers){
        finishHeader();
        endHeaders();
    }
    endPart();
    //末尾の空白と改行を落とす
    size_t n = TrimPartialUtf8(out_, out_len_);
    while(n > 0 && (out_[n - 1] == '\n' || out_[n - 1] == ' ' || out_[n - 1] == '\t')) n--;
    out_len_ = n;
    if(out_cap_) out_[out_len_] = '\0';
}

void TextExtractor::onLine(const char* s, size_t n, bool full){
    const bool cont = line_cont_;
    if(state_ == State::Headers){
        if(cont || (n > 0 && (s[0] == ' ' || s[0] == '\t'))){
            //折り返しの続き
            const size_t room = kHeaderCap - 1 - header_len_;
            const size_t take = n < room ? n : room;
            memcpy(header_ + header_len_, s, take);
            header_len_ += take;
            return;
        }
        if(n == 0 && full){
            finishHeader();
            endHeaders();
            return;
        }
        finishHeader();
        const size_t take = n < kHeaderCap - 1 ? n : kHeaderCap - 1;
        memcpy(header_, s, take);
        header_len_ = take;
        return;
    }

    if(!cont && boundary_count_ > 0 && n >= 2 && s[0] == '-' && s[1] == '-' && checkBoundary(s, n)) return;
    if(state_ == State::Body && part_capture_ != Capture::None) bodyBytes(s, n, full);
}

void TextExtractor::finishHeader(){
    if(header_len_ == 0) return;
    header_[header_len_] = '\0';
    header_len_ = 0;

    char* colon = strchr(header_, ':');
    if(!colon) return;
    size_t name_len = (size_t)(colon - header_);
    while(name_len > 0 && IsSpace(header_[name_len - 1])) name_len--;
    const char* value = colon + 1;
    while(IsSpace(*value)) value++;
    const size_t value_len = strlen(value);

    if(EqualsNoCase(header_, name_len, "content-type")){
        size_t t = 0;
        while(value[t] && value[t] != ';' && !IsSpace(value[t]) && t + 1 < sizeof(type_)){
            type_[t] = Lower(value[t]);
            t++;
        }
        type_[t] = '\0';
        char buf[96];
        if(GetParam(value, "charset", buf, sizeof(buf))) part_charset_ = MailCharset::Parse(buf);
        if(!GetParam(value, "boundary", part_boundary_, sizeof(part_boundary_))) part_boundary_[0] = '\0';
        if(!part_name_[0] && GetParam(value, "name", buf, sizeof(buf))){
            DecodeHeader(buf, strlen(buf), part_name_, sizeof(part_name_));
        }
    }else if(EqualsNoCase(header_, name_len, "content-transfer-encoding")){
        if(StartsNoCase(value, "base64")) enc_ = Encoding::Base64;
        else if(StartsNoCase(value, "quoted-printable")) enc_ = Encoding::QuotedPrintable;
        else enc_ = Encoding::Plain;
    }else if(EqualsNoCase(header_, name_len, "content-disposition")){
        if(StartsNoCase(value, "attachment")) part_attachment_ = true;
        char buf[96];
        if(GetParam(value, "filename", buf, sizeof(buf))){
            DecodeHeader(buf, strlen(buf), part_name_, sizeof(part_name_));
        }
    }else if(depth_ == 0){
        if(EqualsNoCase(header_, name_len, "from")) DecodeHeader(value, value_len, from_, sizeof(from_));
        else if(EqualsNoCase(header_, name_len, "to")) DecodeHeader(value, value_len, to_, sizeof(to_));
        else if(EqualsNoCase(header_, name_len, "cc")) DecodeHeader(value, value_len, cc_, sizeof(cc_));
        else if(EqualsNoCase(header_, name_len, "subject")) DecodeHeader(value, value_len, subject_, sizeof(subject_));
        else if(EqualsNoCase(header_, name_len, "date")) DecodeHeader(value, value_len, date_, sizeof(date_));
    }
}

void TextExtractor::endHeaders(){
    state_ = State::Body;
    part_capture_ = Capture::None;

    if(strncmp(type_, "multipart/", 10) == 0){
        if(part_boundary_[0] && boundary_count_ < kMaxDepth){
            strcpy(boundaries_[boundary_count_++], part_boundary_);
        }
        //前置き(最初の境界まで)は読み捨てる
        state_ = State::Skip;
        return;
    }

    const bool plain = strcmp(type_, "text/plain") == 0;
    const bool html = strcmp(type_, "text/html") == 0;
    if(!part_attachment_ && !part_name_[0] && plain && captured_ != Capture::Plain){
        if(captured_ == Capture::Html){
            //HTMLしか無いと思って読んでいたが、後ろにテキストがあった(alternative の並びが逆)
            out_len_ = 0;
            out_truncated_ = false;
            if(out_cap_) out_[0] = '\0';
            newlines_ = 0;
            at_line_start_ = true;
        }
        part_capture_ = Capture::Plain;
    }else if(!part_attachment_ && !part_name_[0] && html && captured_ == Capture::None){
        part_capture_ = Capture::Html;
    }else if(part_attachment_ || part_name_[0] || strncmp(type_, "text/", 5) != 0){
        //添付(名前が無ければ型で)
        const char* name = part_name_[0] ? part_name_ : type_;
        attach_count_++;
        const size_t cur = strlen(attach_names_);
        const size_t add = strlen(name) + (cur ? 2 : 0);
        if(cur + add + 1 <= sizeof(attach_names_)){
            if(cur) strcat(attach_names_, ", ");
            strcat(attach_names_, name);
        }
    }

    if(part_capture_ == Capture::None) return;
    captured_ = part_capture_;
    charset_.begin(part_charset_);
    b64_acc_ = 0;
    b64_bits_ = 0;
    qp_pending_ = 0;
    html_ = HtmlState::Text;
    html_skip_ = false;
    html_space_ = false;
    tag_len_ = 0;
    entity_len_ = 0;
}

bool TextExtractor::checkBoundary(const char* s, size_t n){
    while(n > 2 && (s[n - 1] == ' ' || s[n - 1] == '\t')) n--;
    for(int k = boundary_count_ - 1; k >= 0; k--){
        const char* b = boundaries_[k];
        const size_t bl = strlen(b);
        if(n < 2 + bl || memcmp(s + 2, b, bl) != 0) continue;
        const size_t rest = n - 2 - bl;
        if(rest == 0){
            endPart();
            boundary_count_ = k + 1;
            depth_ = 1;
            startPart();
            return true;
        }
        if(rest == 2 && s[2 + bl] == '-' && s[3 + bl] == '-'){
            endPart();
            boundary_count_ = k;
            state_ = State::Skip;
            part_capture_ = Capture::None;
            return true;
        }
    }
    return false;
}

void TextExtractor::endPart(){
    if(part_capture_ == Capture::None) return;
    charset_.finish(*this);
    if(part_capture_ == Capture::Html && html_ == HtmlState::Entity && !html_skip_){
        //閉じていない & はそのまま出す
        html_ = HtmlState::Text;
        textUnitHtml("&", 1);
        textUnitHtml(entity_, entity_len_);
    }
    part_capture_ = Capture::None;
}

void TextExtractor::bodyBytes(const char* s, size_t n, bool newline){
    if(out_truncated_) return;
    uint8_t buf[128];
    size_t bn = 0;
    auto push = [&](uint8_t b){
        buf[bn++] = b;
        if(bn == sizeof(buf)){ decodedBytes(buf, bn); bn = 0; }
    };

    switch(enc_){
        case Encoding::Base64:
            for(size_t i = 0; i < n; i++){
                const char c = s[i];
                if(c == '='){ b64_bits_ = 0; b64_acc_ = 0; continue; }
                const int v = Base64Value(c);
                if(v < 0) continue;
                b64_acc_ = (b64_acc_ << 6) | (uint32_t)v;
                b64_bits_ += 6;
                if(b64_bits_ >= 8){
                    b64_bits_ -= 8;
                    push((uint8_t)((b64_acc_ >> b64_bits_) & 0xFF));
                }
            }
            break;

        case Encoding::QuotedPrintable: {
            //行末の空白は送る途中で足されたものなので落とす(RFC 2045)
            if(newline) while(n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) n--;
            for(size_t i = 0; i < n; i++){
                const char c = s[i];
                if(qp_pending_ == 2){
                    if(HexValue(c) >= 0){ qp_hi_ = (uint8_t)c; qp_pending_ = 1; continue; }
                    push('=');
                    qp_pending_ = 0;
                }else if(qp_pending_ == 1){
                    qp_pending_ = 0;
                    if(HexValue(c) >= 0){
                        push((uint8_t)((HexValue((char)qp_hi_) << 4) | HexValue(c)));
                        continue;
                    }
                    push('=');
                    push(qp_hi_);
                }
                if(c == '='){ qp_pending_ = 2; continue; }
                push((uint8_t)c);
            }
            if(newline){
                if(qp_pending_ == 2){
                    //行末の = は「改行しない」印(ソフト改行)
                    qp_pending_ = 0;
                }else{
                    if(qp_pending_ == 1){ push('='); push(qp_hi_); qp_pending_ = 0; }
                    push('\n');
                }
            }
            break;
        }

        case Encoding::Plain:
            for(size_t i = 0; i < n; i++) push((uint8_t)s[i]);
            if(newline) push('\n');
            break;
    }
    if(bn > 0) decodedBytes(buf, bn);
}

void TextExtractor::decodedBytes(const uint8_t* s, size_t n){
    charset_.feed(s, n, *this);
}

void TextExtractor::outRaw(const char* s, size_t n){
    if(out_truncated_ || !out_) return;
    if(out_len_ + n + 1 > out_cap_){
        out_truncated_ = true;
        //書きかけの文字を残さない
        out_len_ = TrimPartialUtf8(out_, out_len_);
        out_[out_len_] = '\0';
        return;
    }
    memcpy(out_ + out_len_, s, n);
    out_len_ += n;
    out_[out_len_] = '\0';
}

void TextExtractor::textUnit(const char* s, size_t n){
    if(n == 1 && s[0] == '\r') return;
    if(n == 1 && s[0] == '\n'){
        if(out_len_ == 0) return;   //先頭の空行は出さない
        if(++newlines_ > 2) return; //空行は1行まで
        outRaw(s, 1);
        at_line_start_ = true;
        return;
    }
    newlines_ = 0;
    at_line_start_ = false;
    outRaw(s, n);
}

void TextExtractor::textUnitHtml(const char* s, size_t n){
    if(html_space_){
        html_space_ = false;
        if(!at_line_start_ && out_len_ > 0) textUnit(" ", 1);
    }
    textUnit(s, n);
}

void TextExtractor::put(const char* s, size_t n){
    if(part_capture_ == Capture::Html){
        for(size_t i = 0; i < n; i++) htmlChar(s[i]);
    }else{
        textUnit(s, n);
    }
}

void TextExtractor::htmlChar(char c){
    switch(html_){
        case HtmlState::Text:
            if(c == '<'){
                html_ = HtmlState::Tag;
                tag_len_ = 0;
                tag_name_done_ = false;
                tag_closing_ = false;
                return;
            }
            if(html_skip_) return;
            if(c == '&'){
                html_ = HtmlState::Entity;
                entity_len_ = 0;
                return;
            }
            if(IsSpace(c)){
                html_space_ = true;
                return;
            }
            textUnitHtml(&c, 1);
            return;

        case HtmlState::Tag:
            if(!tag_name_done_){
                if(tag_len_ == 0 && c == '/' && !tag_closing_){
                    tag_closing_ = true;
                    return;
                }
                if((IsAlnum(c) || c == '!' || c == '-') && (size_t)tag_len_ + 1 < sizeof(tag_)){
                    tag_[tag_len_++] = Lower(c);
                    if(tag_len_ == 3 && memcmp(tag_, "!--", 3) == 0){
                        html_ = HtmlState::Comment;
                        comment_dashes_ = 0;
                    }
                    return;
                }
                tag_name_done_ = true;
            }
            if(c == '>'){
                html_ = HtmlState::Text;
                htmlTag();
            }
            return;

        case HtmlState::Comment:
            if(c == '-'){ comment_dashes_++; return; }
            if(c == '>' && comment_dashes_ >= 2){ html_ = HtmlState::Text; return; }
            comment_dashes_ = 0;
            return;

        case HtmlState::Entity:
            if(c == ';'){
                html_ = HtmlState::Text;
                htmlEntity();
                return;
            }
            if((IsAlnum(c) || c == '#') && (size_t)entity_len_ + 1 < sizeof(entity_)){
                entity_[entity_len_++] = c;
                return;
            }
            //実体参照ではなかった。& から書いてあるとおりに出し、このバイトは読み直す
            html_ = HtmlState::Text;
            textUnitHtml("&", 1);
            for(uint8_t i = 0; i < entity_len_; i++) textUnitHtml(&entity_[i], 1);
            htmlChar(c);
            return;
    }
}

void TextExtractor::htmlTag(){
    tag_[tag_len_] = '\0';
    const char* t = tag_;
    if(strcmp(t, "style") == 0 || strcmp(t, "script") == 0 || strcmp(t, "head") == 0 || strcmp(t, "title") == 0){
        html_skip_ = !tag_closing_;
        return;
    }
    if(html_skip_) return;

    auto newline = [this](){ html_space_ = false; textUnit("\n", 1); };
    if(strcmp(t, "br") == 0){ newline(); return; }
    const bool heading = t[0] == 'h' && t[1] >= '1' && t[1] <= '6' && t[2] == '\0';
    if(strcmp(t, "p") == 0 || heading || strcmp(t, "blockquote") == 0 || strcmp(t, "table") == 0
       || strcmp(t, "ul") == 0 || strcmp(t, "ol") == 0 || strcmp(t, "pre") == 0){
        newline();
        if(tag_closing_) newline();
        return;
    }
    //行の頭にいるなら改行を重ねない(<div><div>… の入れ子で空行だらけにしない)
    if(strcmp(t, "div") == 0 || strcmp(t, "tr") == 0 || strcmp(t, "hr") == 0){
        if(!at_line_start_) newline();
        return;
    }
    if(strcmp(t, "li") == 0){
        if(tag_closing_) return;
        if(!at_line_start_) newline();
        html_space_ = false;
        textUnit("・", strlen("・"));
        return;
    }
    if(strcmp(t, "td") == 0 || strcmp(t, "th") == 0){
        html_space_ = true;
        return;
    }
}

void TextExtractor::htmlEntity(){
    entity_[entity_len_] = '\0';
    const char* e = entity_;
    uint32_t cp = 0;
    if(e[0] == '#'){
        if(e[1] == 'x' || e[1] == 'X') cp = (uint32_t)strtoul(e + 2, nullptr, 16);
        else cp = (uint32_t)strtoul(e + 1, nullptr, 10);
        if(cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = MailCharset::kReplacement;
    }else{
        struct N { const char* name; uint16_t cp; };
        static const N kNames[] = {
            {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"nbsp", ' '},
            {"copy", 0x00A9}, {"reg", 0x00AE}, {"yen", 0x00A5}, {"hellip", 0x2026}, {"mdash", 0x2014},
            {"ndash", 0x2013}, {"lsquo", 0x2018}, {"rsquo", 0x2019}, {"ldquo", 0x201C}, {"rdquo", 0x201D},
            {"middot", 0x00B7}, {"times", 0x00D7}, {"bull", 0x2022}, {"trade", 0x2122}, {"euro", 0x20AC},
        };
        for(const N& n : kNames) if(strcmp(e, n.name) == 0){ cp = n.cp; break; }
        if(cp == 0){
            //知らない名前は書いてあるとおりに
            textUnitHtml("&", 1);
            for(uint8_t i = 0; i < entity_len_; i++) textUnitHtml(&entity_[i], 1);
            textUnitHtml(";", 1);
            return;
        }
    }
    if(cp == ' '){ html_space_ = true; return; }
    char buf[4];
    const int n = MailCharset::EncodeUtf8(cp, buf);
    textUnitHtml(buf, (size_t)n);
}

}
