#include "apps/reader/Aozora_Text.hpp"

#include "apps/mail/Mail_Charset.hpp"

#include <cstring>

namespace Aozora {

namespace {
    constexpr uint32_t kRubyOpen = 0x300A;   // 《
    constexpr uint32_t kRubyClose = 0x300B;  // 》
    constexpr uint32_t kRubyBar = 0xFF5C;    // ｜
    constexpr uint32_t kNoteOpen = 0xFF3B;   // ［
    constexpr uint32_t kNoteClose = 0xFF3D;  // ］
    constexpr uint32_t kNoteMark = 0xFF03;   // ＃
    constexpr uint32_t kGaijiMark = 0x203B;  // ※

    bool StartsWith(const char* s, const char* prefix){
        return strncmp(s, prefix, strlen(prefix)) == 0;
    }

    bool EndsWith(const char* s, size_t n, const char* suffix){
        const size_t m = strlen(suffix);
        return n >= m && memcmp(s + n - m, suffix, m) == 0;
    }

    // 注記の中の最初の数(半角・全角の数字、一〜十の漢数字)。無ければ0
    int FirstNumber(const char* s){
        static const char* const kKanji[] = { "一", "二", "三", "四", "五", "六", "七", "八", "九", "十" };
        const uint8_t* p = (const uint8_t*)s;
        while(*p){
            int v = -1;
            int adv = 1;
            if(*p >= '0' && *p <= '9'){
                v = 0;
                while(*p >= '0' && *p <= '9'){ v = v * 10 + (*p - '0'); p++; }
                return v;
            }
            // 全角数字 U+FF10〜FF19 = EF BC 90〜99
            if(p[0] == 0xEF && p[1] == 0xBC && p[2] >= 0x90 && p[2] <= 0x99){
                v = 0;
                while(p[0] == 0xEF && p[1] == 0xBC && p[2] >= 0x90 && p[2] <= 0x99){
                    v = v * 10 + (p[2] - 0x90);
                    p += 3;
                }
                return v;
            }
            for(int k = 0; k < 10; k++){
                if(StartsWith((const char*)p, kKanji[k])){
                    v = k + 1;
                    break;
                }
            }
            if(v > 0) return v;
            if(*p >= 0xF0) adv = 4;
            else if(*p >= 0xE0) adv = 3;
            else if(*p >= 0xC0) adv = 2;
            for(int k = 0; k < adv && *p; k++) p++;
        }
        return 0;
    }

    int8_t Clamp8(int v){ return (int8_t)(v > 40 ? 40 : v); }

    // 外字の注記から Unicode を拾う(「U+6318」の形)。無ければ 〓
    uint32_t GaijiCodepoint(const char* s){
        const char* p = strstr(s, "U+");
        if(!p) p = strstr(s, "u+");
        if(!p) return kReplacement;
        p += 2;
        uint32_t v = 0;
        int digits = 0;
        while(digits < 6){
            const char c = *p;
            int d;
            if(c >= '0' && c <= '9') d = c - '0';
            else if(c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else if(c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else break;
            v = v * 16 + d;
            p++;
            digits++;
        }
        if(digits < 4 || v == 0 || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) return kReplacement;
        return v;
    }
}

int EncodeUtf8(uint32_t cp, char out[4]){
    return MailCharset::EncodeUtf8(cp, out);
}

// ---------------------------------------------------------------------------
// Reader
// ---------------------------------------------------------------------------

int Reader::byteAt(uint32_t pos){
    if(pos >= src_.size()) return -1;
    if(pos < win_off_ || pos >= win_off_ + win_len_){
        // 前へ戻る読み方(行の折り返しのやり直し)でも読み直しが続かないよう、少し手前から読む
        const uint32_t back = (pos < win_off_) ? (uint32_t)(kWindow / 2) : 0;
        win_off_ = pos > back ? pos - back : 0;
        win_len_ = (uint32_t)src_.readAt(win_off_, win_, kWindow);
        if(pos >= win_off_ + win_len_) return -1;
    }
    return win_[pos - win_off_];
}

uint32_t Reader::decode(uint32_t pos, int& len){
    const int b0 = byteAt(pos);
    if(b0 < 0){ len = 0; return 0; }
    len = 1;
    if(b0 < 0x80) return (uint32_t)b0;

    if(info_.enc == Encoding::ShiftJis){
        if(b0 >= 0xA1 && b0 <= 0xDF) return 0xFF61 + (b0 - 0xA1);
        if((b0 >= 0x81 && b0 <= 0x9F) || (b0 >= 0xE0 && b0 <= 0xFC)){
            const int b = byteAt(pos + 1);
            if((b >= 0x40 && b <= 0x7E) || (b >= 0x80 && b <= 0xFC)){
                len = 2;
                int row = ((b0 < 0xA0) ? (b0 - 0x81) : (b0 - 0xC1)) * 2 + 1;
                int cell;
                if(b >= 0x9F){
                    row++;
                    cell = b - 0x9E;
                }else{
                    cell = b - ((b >= 0x80) ? 0x40 : 0x3F);
                }
                const uint16_t cp = MailCharset::JisToUnicode(row, cell);
                return cp ? cp : kReplacement;
            }
        }
        return kReplacement;
    }

    // UTF-8
    int need;
    uint32_t cp;
    if(b0 >= 0xC2 && b0 <= 0xDF){ need = 1; cp = b0 & 0x1F; }
    else if(b0 >= 0xE0 && b0 <= 0xEF){ need = 2; cp = b0 & 0x0F; }
    else if(b0 >= 0xF0 && b0 <= 0xF4){ need = 3; cp = b0 & 0x07; }
    else return kReplacement;
    for(int i = 1; i <= need; i++){
        const int b = byteAt(pos + i);
        if(b < 0 || (b & 0xC0) != 0x80) return kReplacement;
        cp = (cp << 6) | (uint32_t)(b & 0x3F);
    }
    // 長すぎる書き方・サロゲート・範囲外
    if((need == 2 && cp < 0x800) || (need == 3 && (cp < 0x10000 || cp > 0x10FFFF)) || (cp >= 0xD800 && cp <= 0xDFFF)){
        return kReplacement;
    }
    len = need + 1;
    return cp;
}

uint32_t Reader::readBracket(uint32_t pos, uint32_t open, uint32_t close, int max_chars, char* buf, size_t cap, size_t& len){
    len = 0;
    if(cap) buf[0] = '\0';
    int depth = 0;
    for(int i = 0; i < max_chars; i++){
        int l;
        const uint32_t cp = decode(pos, l);
        if(l == 0 || cp == '\r' || cp == '\n') return 0;
        pos += l;
        if(cp == close){
            if(depth == 0) return pos;
            depth--;
        }else if(cp == open){
            depth++;
        }
        char u[4];
        const int n = EncodeUtf8(cp, u);
        if(len + n < cap){
            memcpy(buf + len, u, n);
            len += n;
            buf[len] = '\0';
        }
    }
    return 0;
}

void Reader::interpretNote(Token& t, const char* s, size_t n){
    t.note = Note::Other;
    t.num = 0;
    if(strstr(s, "改ページ") || strstr(s, "改丁") || strstr(s, "改見開き") || strstr(s, "改段")){
        t.note = Note::PageBreak;
        return;
    }
    // 「…」は中見出し(同行中見出し・窓中見出しも)
    if(StartsWith(s, "「") && EndsWith(s, n, "見出し")){
        const char* q = strstr(s, "」は");
        if(q){
            const char* from = s + strlen("「");
            size_t m = (size_t)(q - from);
            if(m > kTextMax) m = kTextMax;
            // UTF-8の途中で切らない
            while(m > 0 && ((uint8_t)from[m] & 0xC0) == 0x80) m--;
            memcpy(t.text, from, m);
            t.text[m] = '\0';
            t.len = (uint8_t)m;
            t.note = Note::Heading;
        }
        return;
    }
    if(strstr(s, "見出し")){
        if(EndsWith(s, n, "見出し終わり")) t.note = Note::HeadingEnd;
        else if(EndsWith(s, n, "見出し") && !strstr(s, "」")) t.note = Note::HeadingBegin;
        return;
    }
    if(strstr(s, "字下げ終わり")){
        t.note = Note::BlockIndentEnd;
        return;
    }
    if(StartsWith(s, "ここから")){
        if(strstr(s, "字下げ") || strstr(s, "天付き")){
            t.note = Note::BlockIndent;
            // 「ここから改行天付き、折り返して１字下げ」は最初の字下げが0
            t.num = StartsWith(s, "ここから改行天付き") ? 0 : Clamp8(FirstNumber(s));
        }
        return;
    }
    if(strstr(s, "字下げ")){
        t.note = Note::Indent;
        t.num = Clamp8(FirstNumber(s));
        return;
    }
    if(strcmp(s, "地付き") == 0){
        t.note = Note::AlignBottom;
        return;
    }
    if(StartsWith(s, "地から") && strstr(s, "字上げ")){
        t.note = Note::AlignBottom;
        t.num = Clamp8(FirstNumber(s));
        return;
    }
}

void Reader::next(uint32_t& pos, Token& t){
    t.kind = Tok::Eof;
    t.cp = 0;
    t.note = Note::Other;
    t.num = 0;
    t.len = 0;
    t.text[0] = '\0';

    for(;;){
        if(pos < info_.body_start) pos = info_.body_start;
        if(info_.notes_end > info_.notes_begin && pos >= info_.notes_begin && pos < info_.notes_end) pos = info_.notes_end;

        int len;
        const uint32_t cp = decode(pos, len);
        if(len == 0) return;

        if(cp == '\r'){
            pos += len;
            int l2;
            if(decode(pos, l2) == '\n' && l2) pos += l2;
            t.kind = Tok::Newline;
            return;
        }
        if(cp == '\n'){
            pos += len;
            t.kind = Tok::Newline;
            return;
        }
        if(cp == kRubyBar){
            pos += len;
            t.kind = Tok::RubyStart;
            return;
        }
        if(cp == kRubyOpen){
            size_t n;
            const uint32_t end = readBracket(pos + len, kRubyOpen, kRubyClose, kMaxRubyChars, t.text, sizeof(t.text), n);
            if(end){
                pos = end;
                t.len = (uint8_t)n;
                t.kind = Tok::Ruby;
                return;
            }
        }
        if(cp == kNoteOpen || cp == kGaijiMark){
            // ［＃ … ］(※ の後ろに付いたものは外字)
            uint32_t p = pos + len;
            int l1 = 0, l2 = 0;
            const bool gaiji = (cp == kGaijiMark);
            if(gaiji){
                if(decode(p, l1) != kNoteOpen || l1 == 0) l1 = -1;
                else p += l1;
            }
            if(l1 >= 0 && decode(p, l2) == kNoteMark && l2 > 0){
                char buf[256];
                size_t n;
                const uint32_t end = readBracket(p + l2, kNoteOpen, kNoteClose, kMaxNoteChars, buf, sizeof(buf), n);
                if(end){
                    pos = end;
                    if(gaiji){
                        t.kind = Tok::Char;
                        t.cp = GaijiCodepoint(buf);
                    }else{
                        t.kind = Tok::Note;
                        interpretNote(t, buf, n);
                    }
                    return;
                }
            }
        }
        pos += len;
        // 制御文字(NUL等)は捨てる。タブは空白1つ
        if(cp == '\t'){
            t.kind = Tok::Char;
            t.cp = ' ';
            return;
        }
        if(cp < 0x20 || cp == 0x7F || cp == 0xFEFF) continue;
        t.kind = Tok::Char;
        t.cp = cp;
        return;
    }
}

// ---------------------------------------------------------------------------
// Analyze
// ---------------------------------------------------------------------------

namespace {
    // 先頭が UTF-8 として正しいか。ASCIIだけでも true
    bool LooksUtf8(Source& src, uint32_t from, uint32_t limit){
        uint8_t buf[128];
        int need = 0;
        uint32_t pos = from;
        while(pos < limit){
            size_t want = limit - pos;
            if(want > sizeof(buf)) want = sizeof(buf);
            const size_t got = src.readAt(pos, buf, want);
            if(got == 0) break;
            for(size_t i = 0; i < got; i++){
                const uint8_t b = buf[i];
                if(need > 0){
                    if((b & 0xC0) != 0x80) return false;
                    need--;
                    continue;
                }
                if(b < 0x80) continue;
                if(b >= 0xC2 && b <= 0xDF) need = 1;
                else if(b >= 0xE0 && b <= 0xEF) need = 2;
                else if(b >= 0xF0 && b <= 0xF4) need = 3;
                else return false;
            }
            pos += (uint32_t)got;
        }
        // 読んだ範囲の終わりで文字が途切れているのは問題にしない
        return true;
    }

    bool IsDashLine(const char* s, size_t n){
        if(n < 10) return false;
        for(size_t i = 0; i < n; i++) if(s[i] != '-') return false;
        return true;
    }
}

void Analyze(Source& src, Info& out){
    out = Info();
    const uint32_t size = src.size();
    uint8_t bom[3] = {};
    if(size >= 3 && src.readAt(0, bom, 3) == 3 && bom[0] == 0xEF && bom[1] == 0xBB && bom[2] == 0xBF){
        out.enc = Encoding::Utf8;
        out.body_start = 3;
    }else{
        const uint32_t limit = size < kProbeBytes ? size : (uint32_t)kProbeBytes;
        out.enc = LooksUtf8(src, 0, limit) ? Encoding::Utf8 : Encoding::ShiftJis;
    }

    // 行ごとに見て、題名・著者と説明の区切り線を探す
    Reader r(src, out);
    Token t;
    uint32_t pos = out.body_start;
    FixedString<PICO_STR_LL> line;
    FixedString<PICO_STR_L> first, last;
    int header_lines = 0;
    bool header_done = false;
    uint32_t line_start = pos;
    uint32_t dash_start = 0;
    enum { Before, ExpectHeading, InNotes } state = Before;
    const uint32_t probe_end = size < kProbeBytes * 2 ? size : (uint32_t)(kProbeBytes * 2);

    while(pos < probe_end){
        r.next(pos, t);
        if(t.kind == Tok::Char){
            char u[4];
            const int n = EncodeUtf8(t.cp, u);
            (void)line.append(u, n);  // 長い行は題名にならないので切れてよい
            continue;
        }
        if(t.kind != Tok::Newline && t.kind != Tok::Eof) continue;

        const bool dash = IsDashLine(line.c_str(), line.length());
        if(state == Before){
            if(dash && line_start < kProbeBytes){
                dash_start = line_start;
                state = ExpectHeading;
                header_done = true;
            }else if(!header_done){
                if(line.empty()){
                    if(header_lines > 0) header_done = true;
                }else{
                    if(header_lines == 0) first = line;
                    last = line;
                    header_lines++;
                }
            }
        }else if(state == ExpectHeading){
            // 区切り線の次の行が説明の見出しでなければ、ただの線
            state = strstr(line.c_str(), "テキスト中に現れる記号について") ? InNotes : Before;
        }else if(dash){
            // 2本目の区切り線(と、その後ろの改行)まで
            out.notes_begin = dash_start;
            out.notes_end = pos;
            break;
        }
        if(t.kind == Tok::Eof) break;
        line.clear();
        line_start = pos;
    }

    // 青空文庫の形と分かったときだけ、冒頭の行を題名・著者として使う
    if(out.notes_end > out.notes_begin && header_lines > 0){
        out.title = first;
        if(header_lines >= 2) out.author = last;
    }
}

}
